#include "timeshiftx/epg_manager.hpp"
#include "timeshiftx/network_service.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

namespace timeshiftx {

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

bool isNameBoundary(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0 || c == '>' || c == '/';
}

std::size_t findTagEnd(const std::string& text, std::size_t open_pos) {
    char quote = '\0';
    for (std::size_t i = open_pos; i < text.size(); ++i) {
        const char c = text[i];
        if ((c == '"' || c == '\'') && (i == 0 || text[i - 1] != '\\')) {
            quote = quote == '\0' ? c : (quote == c ? '\0' : quote);
            continue;
        }
        if (c == '>' && quote == '\0') return i;
    }
    return std::string::npos;
}

std::size_t findStartTag(const std::string& text, const std::string& tag_name, std::size_t pos) {
    const std::string needle = "<" + tag_name;
    while (true) {
        const std::size_t found = text.find(needle, pos);
        if (found == std::string::npos) return std::string::npos;
        const std::size_t boundary = found + needle.size();
        if (boundary < text.size() && isNameBoundary(text[boundary])) return found;
        pos = boundary;
    }
}

std::unordered_map<std::string, std::string> parseXmlAttributes(const std::string& tag_text) {
    std::unordered_map<std::string, std::string> attrs;
    std::size_t i = tag_text.find_first_of(" \t\r\n");
    if (i == std::string::npos) return attrs;

    while (i < tag_text.size()) {
        while (i < tag_text.size() && std::isspace(static_cast<unsigned char>(tag_text[i])) != 0) ++i;
        if (i >= tag_text.size() || tag_text[i] == '>' || tag_text[i] == '/') break;

        const std::size_t key_start = i;
        while (i < tag_text.size() && tag_text[i] != '=' && std::isspace(static_cast<unsigned char>(tag_text[i])) == 0 && tag_text[i] != '>' && tag_text[i] != '/') ++i;
        if (i == key_start) break;
        std::string key = tag_text.substr(key_start, i - key_start);

        while (i < tag_text.size() && std::isspace(static_cast<unsigned char>(tag_text[i])) != 0) ++i;
        if (i >= tag_text.size() || tag_text[i] != '=') continue;
        ++i;
        while (i < tag_text.size() && std::isspace(static_cast<unsigned char>(tag_text[i])) != 0) ++i;
        if (i >= tag_text.size()) break;

        std::string value;
        if (tag_text[i] == '"' || tag_text[i] == '\'') {
            const char quote = tag_text[i++];
            const std::size_t value_start = i;
            while (i < tag_text.size() && tag_text[i] != quote) ++i;
            value = tag_text.substr(value_start, i - value_start);
            if (i < tag_text.size()) ++i;
        } else {
            const std::size_t value_start = i;
            while (i < tag_text.size() && std::isspace(static_cast<unsigned char>(tag_text[i])) == 0 && tag_text[i] != '>' && tag_text[i] != '/') ++i;
            value = tag_text.substr(value_start, i - value_start);
        }

        attrs[std::move(key)] = value;
    }

    return attrs;
}

std::string decodeXmlEntities(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            continue;
        }

        const std::size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos) {
            out.push_back(text[i]);
            continue;
        }

        const std::string entity = text.substr(i + 1, semi - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (!entity.empty() && entity[0] == '#') {
            try {
                const int base = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X') ? 16 : 10;
                const std::size_t offset = base == 16 ? 2 : 1;
                const unsigned long code = std::stoul(entity.substr(offset), nullptr, base);
                if (code <= 0x7F) {
                    out.push_back(static_cast<char>(code));
                } else if (code <= 0x7FF) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else if (code <= 0xFFFF) {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else if (code <= 0x10FFFF) {
                    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out.append(text.substr(i, semi - i + 1));
                }
            } catch (...) {
                out.append(text.substr(i, semi - i + 1));
            }
        } else {
            out.append(text.substr(i, semi - i + 1));
        }
        i = semi;
    }
    return out;
}

std::string normalizeXmlText(const std::string& text) {
    std::string out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t cdata_start = text.find("<![CDATA[", pos);
        if (cdata_start == std::string::npos) {
            out += decodeXmlEntities(text.substr(pos));
            break;
        }
        out += decodeXmlEntities(text.substr(pos, cdata_start - pos));
        const std::size_t cdata_end = text.find("]]>", cdata_start + 9);
        if (cdata_end == std::string::npos) {
            out += text.substr(cdata_start + 9);
            break;
        }
        out += text.substr(cdata_start + 9, cdata_end - cdata_start - 9);
        pos = cdata_end + 3;
    }
    return trim(out);
}

std::string readAttribute(const std::string& tag_text, const std::string& attr) {
    const auto attrs = parseXmlAttributes(tag_text);
    const auto it = attrs.find(attr);
    return it == attrs.end() ? std::string{} : decodeXmlEntities(it->second);
}

void appendUnique(std::vector<std::string>& values, const std::string& value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

} // namespace

void EPGManager::setChannelFilter(std::unordered_set<std::string> allowed_epg_ids) {
    std::unique_lock<std::shared_mutex> lk(rw_mutex_);
    filter_channel_ids_ = std::move(allowed_epg_ids);
}

Error EPGManager::loadFromUrl(const std::string &url, long timeout_seconds, INetworkPlugin *injected_plugin)
{
    if (url.empty()) {
        return {ErrorCode::ERR_INVALID_ARGUMENT, "EPG URL is empty"};
    }

    std::string xml_content;
    Error net_err = NetworkService::get(url, xml_content, timeout_seconds, 2, injected_plugin);
    if (!net_err.ok()) {
        return net_err;
    }

    return loadXMLTV(xml_content);
}

Error EPGManager::loadXMLTV(const std::string& xml_content) {
    if (xml_content.empty()) {
        return {ErrorCode::ERR_PARSE_XMLTV_FAILED, "XMLTV content is empty"};
    }

    const std::size_t tv_start = findStartTag(xml_content, "tv", 0);
    if (tv_start == std::string::npos || xml_content.find("</tv>", tv_start) == std::string::npos) {
        return {ErrorCode::ERR_PARSE_XMLTV_FAILED, "XMLTV parsing failed: missing <tv> root"};
    }

    const std::string& raw = xml_content;
    std::unordered_set<std::string> next_channel_ids;
    std::unordered_map<std::string, std::vector<Programme>> next_timelines;
    std::unordered_map<std::string, std::vector<std::string>> next_display_names;
    std::unordered_map<std::string, std::vector<std::string>> next_norm_to_ids;
    EpgTimelineStats next_timeline_stats;
    std::unordered_set<std::string> filter_snapshot;
    {
        std::shared_lock<std::shared_mutex> lk(rw_mutex_);
        filter_snapshot = filter_channel_ids_;
    }

    // 解析 channel 块。
    std::size_t pos = 0;
    while (true) {
        const std::size_t start = findStartTag(raw, "channel", pos);
        if (start == std::string::npos) break;
        const std::size_t open_end = findTagEnd(raw, start);
        const std::size_t close = raw.find("</channel>", open_end);
        if (open_end == std::string::npos || close == std::string::npos) break;

        const std::string open_tag = raw.substr(start, open_end - start + 1);
        const std::string block = raw.substr(open_end + 1, close - open_end - 1);
        const std::string channel_id = readAttribute(open_tag, "id");
        if (!channel_id.empty()) {
            next_channel_ids.insert(channel_id);

            std::size_t dp = 0;
            while (true) {
                const std::size_t ds = findStartTag(block, "display-name", dp);
                if (ds == std::string::npos) break;
                const std::size_t de = findTagEnd(block, ds);
                const std::size_t dc = block.find("</display-name>", de);
                if (de == std::string::npos || dc == std::string::npos) break;
                const std::string name = normalizeXmlText(block.substr(de + 1, dc - de - 1));
                if (!name.empty()) {
                    next_display_names[channel_id].push_back(name);
                    const std::string norm = normalizeChannelName(name);
                    if (!norm.empty()) {
                        appendUnique(next_norm_to_ids[norm], channel_id);
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
        const std::size_t start = findStartTag(raw, "programme", pos);
        if (start == std::string::npos) break;
        const std::size_t open_end = findTagEnd(raw, start);
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
        return {ErrorCode::ERR_PARSE_XMLTV_FAILED, "XMLTV parsing failed: no channels or programs extracted"};
    }

    for (auto& kv : next_timelines) {
        auto& vec = kv.second;
        std::sort(vec.begin(), vec.end(), [](const Programme& a, const Programme& b) { return a.start_time < b.start_time; });
        if (!vec.empty()) {
            if (next_timeline_stats.first_start_time == 0 || vec.front().start_time < next_timeline_stats.first_start_time) {
                next_timeline_stats.first_start_time = vec.front().start_time;
            }
            if (vec.back().end_time > next_timeline_stats.last_end_time) {
                next_timeline_stats.last_end_time = vec.back().end_time;
            }
        }
    }

    const std::size_t parsed_channel_count = next_channel_ids.size();
    next_timeline_stats.channel_count = parsed_channel_count;
    next_timeline_stats.programme_count = programme_count;

    // Double buffering hot update: switch all at once after new data is fully built.
    {
        std::unique_lock<std::shared_mutex> lk(rw_mutex_);
        channel_ids_ = std::move(next_channel_ids);
        timelines_ = std::move(next_timelines);
        channel_display_names_ = std::move(next_display_names);
        normalized_name_to_epg_ids_ = std::move(next_norm_to_ids);
        timeline_stats_ = next_timeline_stats;
    }

    return {ErrorCode::OK,
            "XMLTV parsing successful, channels: " + std::to_string(parsed_channel_count) + ", programs: " + std::to_string(programme_count)};
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

    auto first_in_day = std::lower_bound(it->second.begin(), it->second.end(), day_start, [](const Programme& programme, std::time_t ts) {
        return programme.start_time < ts;
    });

    std::vector<Programme> result;
    while (first_in_day != it->second.begin()) {
        auto previous = std::prev(first_in_day);
        if (previous->end_time <= day_start) break;
        first_in_day = previous;
    }

    for (auto p = first_in_day; p != it->second.end() && p->start_time < day_end; ++p) {
        if (p->end_time > day_start) result.push_back(*p);
    }
    return result;
}

EpgTimelineStats EPGManager::getTimelineStats() const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    return timeline_stats_;
}

EpgTimelineStats EPGManager::getTimelineStatsForChannel(const std::string& epg_match_id) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    EpgTimelineStats stats;
    auto it = timelines_.find(epg_match_id);
    if (it == timelines_.end() || it->second.empty()) return stats;

    stats.channel_count = 1;
    stats.programme_count = it->second.size();
    stats.first_start_time = it->second.front().start_time;
    stats.last_end_time = it->second.back().end_time;
    return stats;
}

std::string EPGManager::resolveStrictEpgId(const Channel& channel) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    if (channel.epg_match_id.empty()) return {};
    return channel_ids_.find(channel.epg_match_id) != channel_ids_.end() ? channel.epg_match_id : std::string{};
}

EpgMatchResult EPGManager::resolveChannelEpgId(const Channel& channel) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    if (!channel.epg_match_id.empty() && channel_ids_.find(channel.epg_match_id) != channel_ids_.end()) {
        return {channel.epg_match_id, EpgMatchMethod::StrictId, 100, channel.epg_match_id};
    }

    EpgMatchResult match = fuzzyMatchChannelNameLocked(channel.epg_match_id, EpgMatchMethod::FuzzyEpgMatchId, 86);
    if (match.matched()) return match;

    return fuzzyMatchChannelNameLocked(channel.name, EpgMatchMethod::FuzzyName, 82);
}

std::string EPGManager::fuzzyMatchChannelName(const std::string& raw_name) const {
    std::shared_lock<std::shared_mutex> lk(rw_mutex_);
    return fuzzyMatchChannelNameLocked(raw_name, EpgMatchMethod::FuzzyName, 82).epg_id;
}

EpgMatchResult EPGManager::fuzzyMatchChannelNameLocked(const std::string& raw_name, EpgMatchMethod method, int base_confidence) const {
    const std::string norm = normalizeChannelName(raw_name);
    if (norm.empty()) return {};

    const auto it = normalized_name_to_epg_ids_.find(norm);
    if (it == normalized_name_to_epg_ids_.end() || it->second.empty()) return {};

    int confidence = base_confidence;
    if (it->second.size() > 1) {
        confidence = std::max(50, base_confidence - 20);
    }

    return {it->second.front(), method, confidence, raw_name};
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
    const std::size_t start = findStartTag(block, tag_name, 0);
    if (start == std::string::npos) return {};

    const std::size_t open_end = findTagEnd(block, start);
    if (open_end == std::string::npos) return {};

    const std::string close_tag = "</" + tag_name + ">";
    const std::size_t close = block.find(close_tag, open_end + 1);
    if (close == std::string::npos) return {};

    return normalizeXmlText(block.substr(open_end + 1, close - open_end - 1));
}

std::string EPGManager::normalizeChannelName(const std::string& raw_name) {
    std::string upper;
    upper.reserve(raw_name.size());
    for (char c : raw_name) {
        upper.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    const std::vector<std::string> noise_tokens = {"ULTRAHD", "FULLHD", "FHD", "UHD", "HD", "SD", "4K", "1080P", "720P"};
    for (const auto& token : noise_tokens) {
        std::size_t pos = 0;
        while ((pos = upper.find(token, pos)) != std::string::npos) {
            upper.erase(pos, token.size());
        }
    }

    std::string normalized;
    for (char c : upper) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) || (uc & 0x80)) {
            normalized.push_back(c);
        }
    }
    return normalized;
}

} // namespace timeshiftx
