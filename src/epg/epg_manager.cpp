#include "chronosstream/epg/epg_manager.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <shared_mutex>
#include <utility>

#include "pugixml/pugixml.hpp"

namespace chronosstream {

namespace {

std::time_t timegmPortable(std::tm* tm_utc) {
#if defined(_WIN32)
    return _mkgmtime(tm_utc);
#else
    return timegm(tm_utc);
#endif
}

std::string trim(const std::string& s) {
    std::size_t l = 0;
    while (l < s.size() && std::isspace(static_cast<unsigned char>(s[l])) != 0) ++l;
    if (l == s.size()) return {};
    std::size_t r = s.size() - 1;
    while (r > l && std::isspace(static_cast<unsigned char>(s[r])) != 0) --r;
    return s.substr(l, r - l + 1);
}

std::string readAttribute(const std::string& tag_text, const std::string& attr) {
    const std::string key = attr + "=\"";
    std::size_t p = tag_text.find(key);
    if (p == std::string::npos) return {};
    p += key.size();
    std::size_t e = tag_text.find('"', p);
    if (e == std::string::npos) return {};
    return tag_text.substr(p, e - p);
}

} // namespace

void EPGManager::setChannelFilter(std::unordered_set<std::string> allowed_epg_ids) {
    std::unique_lock<std::shared_mutex> lk(rw_mutex_);
    filter_channel_ids_ = std::move(allowed_epg_ids);
}

Error EPGManager::loadXMLTV(const std::string& xml_content) {
    if (xml_content.empty()) {
        return {ErrorCode::ERR_PARSE_XMLTV_FAILED, "XMLTV 内容为空"};
    }

    // 使用 pugixml 进行文档加载与合法性检查。
    pugi::xml_document doc;
    auto result = doc.load_string(xml_content.c_str());
    if (!result) {
        return {ErrorCode::ERR_PARSE_XMLTV_FAILED, std::string("XMLTV 解析失败: ") + result.description()};
    }

    const std::string& raw = doc.raw_text();
    std::unordered_set<std::string> next_channel_ids;
    std::unordered_map<std::string, std::vector<Programme>> next_timelines;
    std::unordered_map<std::string, std::vector<std::string>> next_display_names;
    std::unordered_map<std::string, std::string> next_norm_to_id;
    std::unordered_set<std::string> filter_snapshot;
    {
        std::shared_lock<std::shared_mutex> lk(rw_mutex_);
        filter_snapshot = filter_channel_ids_;
    }

    // 解析 channel 块。
    std::size_t pos = 0;
    while (true) {
        const std::size_t start = raw.find("<channel", pos);
        if (start == std::string::npos) break;
        const std::size_t open_end = raw.find('>', start);
        const std::size_t close = raw.find("</channel>", open_end);
        if (open_end == std::string::npos || close == std::string::npos) break;

        const std::string open_tag = raw.substr(start, open_end - start + 1);
        const std::string block = raw.substr(open_end + 1, close - open_end - 1);
        const std::string channel_id = readAttribute(open_tag, "id");
        if (!channel_id.empty()) {
            next_channel_ids.insert(channel_id);

            std::size_t dp = 0;
            while (true) {
                const std::size_t ds = block.find("<display-name", dp);
                if (ds == std::string::npos) break;
                const std::size_t de = block.find('>', ds);
                const std::size_t dc = block.find("</display-name>", de);
                if (de == std::string::npos || dc == std::string::npos) break;
                const std::string name = trim(block.substr(de + 1, dc - de - 1));
                if (!name.empty()) {
                    next_display_names[channel_id].push_back(name);
                    const std::string norm = normalizeChannelName(name);
                    if (!norm.empty() && next_norm_to_id.find(norm) == next_norm_to_id.end()) {
                        next_norm_to_id[norm] = channel_id;
                    }
                }
                dp = dc + 15;
            }
        }

        pos = close + 10;
    }

    // 解析 programme 块。
    std::size_t programme_count = 0;
    pos = 0;
    while (true) {
        const std::size_t start = raw.find("<programme", pos);
        if (start == std::string::npos) break;
        const std::size_t open_end = raw.find('>', start);
        const std::size_t close = raw.find("</programme>", open_end);
        if (open_end == std::string::npos || close == std::string::npos) break;

        const std::string open_tag = raw.substr(start, open_end - start + 1);
        const std::string block = raw.substr(open_end + 1, close - open_end - 1);
        const std::string channel_id = trim(readAttribute(open_tag, "channel"));

        if (!filter_snapshot.empty() && filter_snapshot.find(channel_id) == filter_snapshot.end()) {
            pos = close + 12;
            continue;
        }

        Programme p;
        p.start_time = parseXmltvTimeToUtc(readAttribute(open_tag, "start"));
        p.end_time = parseXmltvTimeToUtc(readAttribute(open_tag, "stop"));
        if (p.start_time <= 0 || p.end_time <= 0 || p.end_time <= p.start_time) {
            pos = close + 12;
            continue;
        }

        p.title = extractTagText(block, "title");
        p.description = extractTagText(block, "desc");

        next_timelines[channel_id].push_back(std::move(p));
        ++programme_count;
        pos = close + 12;
    }

    if (next_channel_ids.empty() || programme_count == 0) {
        return {ErrorCode::ERR_PARSE_XMLTV_FAILED, "XMLTV 解析失败：未提取到频道或节目"};
    }

    for (auto& kv : next_timelines) {
        auto& vec = kv.second;
        std::sort(vec.begin(), vec.end(), [](const Programme& a, const Programme& b) { return a.start_time < b.start_time; });
    }

    const std::size_t parsed_channel_count = next_channel_ids.size();

    // 双缓冲热更新：新数据完整构建后再一次性切换。
    {
        std::unique_lock<std::shared_mutex> lk(rw_mutex_);
        channel_ids_ = std::move(next_channel_ids);
        timelines_ = std::move(next_timelines);
        channel_display_names_ = std::move(next_display_names);
        normalized_name_to_epg_id_ = std::move(next_norm_to_id);
    }

    return {ErrorCode::OK,
            "XMLTV 解析成功，频道数: " + std::to_string(parsed_channel_count) + "，节目数: " + std::to_string(programme_count)};
}

std::vector<Programme> EPGManager::getTimelineForChannel(const std::string& epg_match_id, std::time_t target_date) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    auto it = timelines_.find(epg_match_id);
    if (it == timelines_.end()) return {};

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
    for (const auto& p : it->second) {
        if (p.end_time > day_start && p.start_time < day_end) result.push_back(p);
    }
    return result;
}

std::string EPGManager::resolveStrictEpgId(const Channel& channel) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    if (channel.epg_match_id.empty()) return {};
    return channel_ids_.find(channel.epg_match_id) != channel_ids_.end() ? channel.epg_match_id : std::string{};
}

std::string EPGManager::fuzzyMatchChannelName(const std::string& raw_name) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    const std::string norm = normalizeChannelName(raw_name);
    if (norm.empty()) return {};
    auto it = normalized_name_to_epg_id_.find(norm);
    return it == normalized_name_to_epg_id_.end() ? std::string{} : it->second;
}

std::time_t EPGManager::parseXmltvTimeToUtc(const std::string& xmltv_time) {
    if (xmltv_time.size() < 14) return 0;

    std::tm tm_utc {};
    try {
        tm_utc.tm_year = std::stoi(xmltv_time.substr(0, 4)) - 1900;
        tm_utc.tm_mon = std::stoi(xmltv_time.substr(4, 2)) - 1;
        tm_utc.tm_mday = std::stoi(xmltv_time.substr(6, 2));
        tm_utc.tm_hour = std::stoi(xmltv_time.substr(8, 2));
        tm_utc.tm_min = std::stoi(xmltv_time.substr(10, 2));
        tm_utc.tm_sec = std::stoi(xmltv_time.substr(12, 2));
    } catch (...) {
        return 0;
    }

    std::time_t utc_ts = timegmPortable(&tm_utc);
    const std::size_t tz_pos = xmltv_time.find_first_of("+-", 14);
    if (tz_pos != std::string::npos && tz_pos + 4 < xmltv_time.size()) {
        try {
            const int sign = (xmltv_time[tz_pos] == '+') ? 1 : -1;
            const int hh = std::stoi(xmltv_time.substr(tz_pos + 1, 2));
            const int mm = std::stoi(xmltv_time.substr(tz_pos + 3, 2));
            utc_ts -= sign * (hh * 3600 + mm * 60);
        } catch (...) {
        }
    }
    return utc_ts;
}

std::string EPGManager::extractTagText(const std::string& block, const std::string& tag_name) {
    const std::string open1 = "<" + tag_name + ">";
    const std::string close = "</" + tag_name + ">";

    std::size_t s = block.find(open1);
    std::size_t off = open1.size();
    if (s == std::string::npos) {
        const std::string open2 = "<" + tag_name + " ";
        s = block.find(open2);
        if (s == std::string::npos) return {};
        s = block.find('>', s);
        if (s == std::string::npos) return {};
        ++s;
        off = 0;
    } else {
        s += off;
        off = 0;
    }

    const std::size_t e = block.find(close, s + off);
    if (e == std::string::npos) return {};
    return trim(block.substr(s + off, e - s - off));
}

std::string EPGManager::normalizeChannelName(const std::string& raw_name) {
    std::string upper;
    upper.reserve(raw_name.size());
    for (char c : raw_name) upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

    const std::vector<std::string> noise_tokens = {"ULTRAHD", "FULLHD", "FHD", "UHD", "HD", "SD", "4K", "1080P", "720P"};
    for (const auto& token : noise_tokens) {
        std::size_t pos = 0;
        while ((pos = upper.find(token, pos)) != std::string::npos) upper.erase(pos, token.size());
    }

    std::string normalized;
    for (char c : upper) {
        if (std::isalnum(static_cast<unsigned char>(c)) != 0) normalized.push_back(c);
    }
    return normalized;
}

} // namespace chronosstream
