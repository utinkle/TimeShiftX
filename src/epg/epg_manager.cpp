#include "chronosstream/epg/epg_manager.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <unordered_set>
#include <utility>

namespace chronosstream {

namespace {

// 在 Windows/Linux 上统一使用 UTC 语义把 tm 转为 time_t。
std::time_t timegmPortable(std::tm* tm_utc) {
#if defined(_WIN32)
    return _mkgmtime(tm_utc);
#else
    return timegm(tm_utc);
#endif
}

// 去除字符串两端空白。
std::string trim(const std::string& s) {
    std::size_t l = 0;
    while (l < s.size() && std::isspace(static_cast<unsigned char>(s[l])) != 0) {
        ++l;
    }
    if (l == s.size()) {
        return {};
    }

    std::size_t r = s.size() - 1;
    while (r > l && std::isspace(static_cast<unsigned char>(s[r])) != 0) {
        --r;
    }

    return s.substr(l, r - l + 1);
}

} // namespace

Error EPGManager::loadXMLTV(const std::string& xml_content) {
    channel_ids_.clear();
    timelines_.clear();
    channel_display_names_.clear();
    normalized_name_to_epg_id_.clear();

    if (xml_content.empty()) {
        return {ErrorCode::ERR_PARSE_XMLTV_FAILED, "XMLTV 内容为空"};
    }

    // 3.1/3.4: 解析完整 channel 块，提取 id 与 display-name。
    // 示例：<channel id="cctv1"> <display-name>CCTV-1 FHD</display-name> ... </channel>
    const std::regex channel_block_regex(
        R"re(<channel\s+[^>]*id\s*=\s*"([^"]+)"[^>]*>([\s\S]*?)</channel>)re",
        std::regex::icase);
    const std::regex display_name_regex(R"re(<display-name(\s+[^>]*)?>([\s\S]*?)</display-name>)re", std::regex::icase);

    for (std::sregex_iterator it(xml_content.begin(), xml_content.end(), channel_block_regex), end; it != end; ++it) {
        const std::string channel_id = (*it)[1].str();
        const std::string channel_block = (*it)[2].str();

        channel_ids_.insert(channel_id);

        // 收集 display-name（用于模糊匹配）。
        for (std::sregex_iterator dit(channel_block.begin(), channel_block.end(), display_name_regex), dend; dit != dend; ++dit) {
            const std::string display_name = trim((*dit)[2].str());
            if (display_name.empty()) {
                continue;
            }
            channel_display_names_[channel_id].push_back(display_name);

            const std::string norm = normalizeChannelName(display_name);
            if (!norm.empty() && normalized_name_to_epg_id_.find(norm) == normalized_name_to_epg_id_.end()) {
                normalized_name_to_epg_id_[norm] = channel_id;
            }
        }
    }

    // 3.1: 解析 programme 块。
    // 示例：<programme start="..." stop="..." channel="..."> ... </programme>
    const std::regex programme_regex(
        R"re(<programme\s+[^>]*start\s*=\s*"([^"]+)"[^>]*stop\s*=\s*"([^"]+)"[^>]*channel\s*=\s*"([^"]+)"[^>]*>([\s\S]*?)</programme>)re",
        std::regex::icase);

    std::size_t programme_count = 0;
    for (std::sregex_iterator it(xml_content.begin(), xml_content.end(), programme_regex), end; it != end; ++it) {
        const std::string start_raw = (*it)[1].str();
        const std::string stop_raw = (*it)[2].str();
        const std::string channel_id = trim((*it)[3].str());
        const std::string block = (*it)[4].str();

        // 3.5: 预过滤策略——只保留目标频道时间轴，降低大 XMLTV 内存占用。
        if (!filter_channel_ids_.empty() && filter_channel_ids_.find(channel_id) == filter_channel_ids_.end()) {
            continue;
        }

        Programme p;
        p.start_time = parseXmltvTimeToUtc(start_raw);
        p.end_time = parseXmltvTimeToUtc(stop_raw);

        // 基础有效性检查：时间戳异常则跳过。
        if (p.start_time <= 0 || p.end_time <= 0 || p.end_time <= p.start_time) {
            continue;
        }

        p.title = extractTagText(block, "title");
        p.description = extractTagText(block, "desc");

        timelines_[channel_id].push_back(std::move(p));
        ++programme_count;
    }

    if (channel_ids_.empty() || programme_count == 0) {
        return {ErrorCode::ERR_PARSE_XMLTV_FAILED, "XMLTV 解析失败：未提取到频道或节目"};
    }

    // 3.2: 每个频道时间轴按开始时间排序，保证后续查询稳定。
    for (auto& kv : timelines_) {
        auto& vec = kv.second;
        std::sort(vec.begin(), vec.end(), [](const Programme& a, const Programme& b) {
            return a.start_time < b.start_time;
        });
    }

    return {ErrorCode::OK,
            "XMLTV 解析成功，频道数: " + std::to_string(channel_ids_.size()) +
                "，节目数: " + std::to_string(programme_count)};
}

std::vector<Programme> EPGManager::getTimelineForChannel(const std::string& epg_match_id, std::time_t target_date) const {
    auto it = timelines_.find(epg_match_id);
    if (it == timelines_.end()) {
        return {};
    }

    // 3.2: 按日期过滤（UTC 自然日）。
    std::tm day_tm {};
#if defined(_WIN32)
    gmtime_s(&day_tm, &target_date);
#else
    gmtime_r(&target_date, &day_tm);
#endif
    day_tm.tm_hour = 0;
    day_tm.tm_min = 0;
    day_tm.tm_sec = 0;
    std::time_t day_start = timegmPortable(&day_tm);
    const std::time_t day_end = day_start + 24 * 60 * 60;

    std::vector<Programme> result;
    result.reserve(it->second.size());

    for (const auto& p : it->second) {
        // 与目标自然日有交集的节目都返回（覆盖跨天节目）。
        if (p.end_time > day_start && p.start_time < day_end) {
            result.push_back(p);
        }
    }

    return result;
}

std::string EPGManager::resolveStrictEpgId(const Channel& channel) const {
    // 3.3: 严格匹配只接受 epg_match_id 与 channel id 完全相等。
    if (channel.epg_match_id.empty()) {
        return {};
    }

    if (channel_ids_.find(channel.epg_match_id) != channel_ids_.end()) {
        return channel.epg_match_id;
    }

    return {};
}

std::string EPGManager::fuzzyMatchChannelName(const std::string& raw_name) const {
    const std::string norm = normalizeChannelName(raw_name);
    if (norm.empty()) {
        return {};
    }

    auto it = normalized_name_to_epg_id_.find(norm);
    if (it != normalized_name_to_epg_id_.end()) {
        return it->second;
    }
    return {};
}

std::time_t EPGManager::parseXmltvTimeToUtc(const std::string& xmltv_time) {
    // 支持格式：YYYYMMDDHHMMSS +0800 / YYYYMMDDHHMMSS -0500
    // 若无时区，默认按 UTC 处理。
    if (xmltv_time.size() < 14) {
        return 0;
    }

    std::tm tm_utc {};
    try {
        tm_utc.tm_year = std::stoi(xmltv_time.substr(0, 4)) - 1900;
        tm_utc.tm_mon = std::stoi(xmltv_time.substr(4, 2)) - 1;
        tm_utc.tm_mday = std::stoi(xmltv_time.substr(6, 2));
        tm_utc.tm_hour = std::stoi(xmltv_time.substr(8, 2));
        tm_utc.tm_min = std::stoi(xmltv_time.substr(10, 2));
        tm_utc.tm_sec = std::stoi(xmltv_time.substr(12, 2));
    } catch (const std::exception&) {
        return 0;
    }

    std::time_t utc_ts = timegmPortable(&tm_utc);

    // 解析时区偏移并回调到 UTC。
    // 例：+0800 表示本地时间 = UTC+8，所以 UTC = 本地 - 8h。
    const std::size_t tz_pos = xmltv_time.find_first_of("+-", 14);
    if (tz_pos != std::string::npos && tz_pos + 4 < xmltv_time.size()) {
        const int sign = (xmltv_time[tz_pos] == '+') ? 1 : -1;
        try {
            const int hh = std::stoi(xmltv_time.substr(tz_pos + 1, 2));
            const int mm = std::stoi(xmltv_time.substr(tz_pos + 3, 2));
            const int offset_seconds = sign * (hh * 3600 + mm * 60);
            utc_ts -= offset_seconds;
        } catch (const std::exception&) {
            // 时区解析失败时回退：保持已解析 UTC，不额外修正。
        }
    }

    return utc_ts;
}

std::string EPGManager::extractTagText(const std::string& block, const std::string& tag_name) {
    // 支持 <tag>xxx</tag> 或 <tag lang="xx">xxx</tag>
    const std::regex tag_regex("<" + tag_name + R"((\s+[^>]*)?>([\s\S]*?)</)" + tag_name + ">",
                               std::regex::icase);
    std::smatch m;
    if (!std::regex_search(block, m, tag_regex)) {
        return {};
    }

    if (m.size() < 3) {
        return {};
    }

    return trim(m[2].str());
}

std::string EPGManager::normalizeChannelName(const std::string& raw_name) {
    // 步骤：
    // 1) 转大写
    // 2) 去空格与分隔符
    // 3) 去常见清晰度/噪声后缀
    std::string upper;
    upper.reserve(raw_name.size());
    for (char c : raw_name) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    // 移除常见噪声词（按长度优先，避免部分替换冲突）。
    const std::vector<std::string> noise_tokens = {
        "ULTRAHD", "FULLHD", "FHD", "UHD", "HD", "SD", "4K", "1080P", "720P",
    };
    for (const auto& token : noise_tokens) {
        std::size_t pos = 0;
        while ((pos = upper.find(token, pos)) != std::string::npos) {
            upper.erase(pos, token.size());
        }
    }

    // 仅保留字母数字，去掉 '-' '_' 空格等符号。
    std::string normalized;
    normalized.reserve(upper.size());
    for (char c : upper) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
            normalized.push_back(c);
        }
    }

    return normalized;
}

void EPGManager::setChannelFilter(std::unordered_set<std::string> allowed_epg_ids) {
    filter_channel_ids_ = std::move(allowed_epg_ids);
}

} // namespace chronosstream
