#include "timeshiftx/m3u_parser.hpp"

#include <cctype>
#include <sstream>

#include "timeshiftx/http_client.hpp"

namespace timeshiftx {

Error M3UParser::parse(const std::string& raw_data) {
    channels_.clear();

    if (raw_data.empty()) {
        return {ErrorCode::ERR_PARSE_M3U_FAILED, "M3U text is empty"};
    }

    std::istringstream iss(raw_data);
    std::string line;
    Channel pending_channel;
    bool has_pending_extinf = false;
    std::string pending_user_agent;
    std::string pending_referer;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        // Ignore M3U header information.
        if (line.rfind("#EXTM3U", 0) == 0) {
            continue;
        }

        // Process channel description line: extract attributes and display name.
        if (line.rfind("#EXTINF:", 0) == 0) {
            pending_channel = parseExtInfLine(line);
            pending_channel.source_type = Channel::SourceType::M3U;

            // If #EXTINF is not defined, inherit the latest EXTVLCOPT.
            if (pending_channel.user_agent.empty()) pending_channel.user_agent = pending_user_agent;
            if (pending_channel.referer.empty()) pending_channel.referer = pending_referer;

            has_pending_extinf = true;
            continue;
        }

        // Compatible with VLC header field writing (applies to "next channel").
        if (line.rfind("#EXTVLCOPT:", 0) == 0) {
            const std::string opt = line.substr(std::string("#EXTVLCOPT:").size());
            const std::size_t eq = opt.find('=');
            if (eq != std::string::npos) {
                const std::string k = trim(opt.substr(0, eq));
                const std::string v = trim(opt.substr(eq + 1));
                if (k == "http-user-agent") pending_user_agent = v;
                if (k == "http-referrer" || k == "http-referer") pending_referer = v;
            }
            continue;
        }

        // Ignore other comment lines.
        if (!line.empty() && line[0] == '#') {
            continue;
        }

        // Encounter URL line: pair with previous #EXTINF to form complete channel.
        if (has_pending_extinf) {
            pending_channel.live_url = line;

            // If tvg-id is missing, downgrade to use tvg-name as EPG matching entry.
            if (pending_channel.epg_match_id.empty()) {
                pending_channel.epg_match_id = pending_channel.name;
            }

            pending_channel.internal_id = pending_channel.name + "|" + pending_channel.live_url;
            channels_.push_back(pending_channel);
            has_pending_extinf = false;

            // Single consumption to avoid leakage to subsequent unrelated channels.
            pending_user_agent.clear();
            pending_referer.clear();
        }
    }

    if (channels_.empty()) {
        return {ErrorCode::ERR_PARSE_M3U_FAILED, "No valid channels parsed"};
    }

    return {ErrorCode::OK, "M3U parsing successful"};
}

Error M3UParser::parseFromUrl(const std::string& url, long timeout_seconds) {
    std::string body;
    Error net_rc = HttpClient::get(url, body, timeout_seconds);
    if (!net_rc.ok()) {
        return net_rc;
    }
    return parse(body);
}

std::vector<Channel> M3UParser::getChannels() const {
    return channels_;
}

Channel M3UParser::parseExtInfLine(const std::string& extinf_line) {
    Channel ch;

    // Channel display name is after the first comma.
    const std::size_t comma_pos = extinf_line.find(',');
    if (comma_pos != std::string::npos && comma_pos + 1 < extinf_line.size()) {
        ch.name = trim(extinf_line.substr(comma_pos + 1));
    }

    // Parse common M3U extended attributes.
    const std::string tvg_id = extractQuotedAttr(extinf_line, "tvg-id");
    const std::string tvg_name = extractQuotedAttr(extinf_line, "tvg-name");

    ch.group_name = extractQuotedAttr(extinf_line, "group-title");
    ch.logo_url = extractQuotedAttr(extinf_line, "tvg-logo");

    ch.catchup_type = extractQuotedAttr(extinf_line, "catchup");
    ch.catchup_template = extractQuotedAttr(extinf_line, "catchup-source");
    ch.user_agent = extractQuotedAttr(extinf_line, "http-user-agent");
    ch.referer = extractQuotedAttr(extinf_line, "http-referrer");
    if (ch.referer.empty()) ch.referer = extractQuotedAttr(extinf_line, "http-referer");

    // EPG matching prioritizes tvg-id; fallback to tvg-name if missing; fallback to channel name if still missing.
    if (!tvg_id.empty()) {
        ch.epg_match_id = tvg_id;
    } else if (!tvg_name.empty()) {
        ch.epg_match_id = tvg_name;
    }

    // Channel name priority: display name > tvg-name.
    if (ch.name.empty() && !tvg_name.empty()) {
        ch.name = tvg_name;
    }

    // Simplified rule: as long as catchup or catchup-source exists, it is considered catchup-capable.
    ch.supports_catchup = !ch.catchup_type.empty() || !ch.catchup_template.empty();

    return ch;
}

std::string M3UParser::extractQuotedAttr(const std::string& line, const std::string& key) {
    const std::string key_token = key + "=";
    const std::size_t key_pos = line.find(key_token);
    if (key_pos == std::string::npos) return {};

    std::size_t p = key_pos + key_token.size();
    if (p >= line.size()) return {};

    // 兼容三种格式：
    // 1) key="value"
    // 2) key='value'
    // 3) key=value（以空白或行尾结束）
    if (line[p] == '"' || line[p] == '\'') {
        const char quote = line[p++];
        const std::size_t end = line.find(quote, p);
        if (end == std::string::npos || end <= p) return {};
        return line.substr(p, end - p);
    }

    std::size_t end = p;
    while (end < line.size() && std::isspace(static_cast<unsigned char>(line[end])) == 0) {
        ++end;
    }
    if (end <= p) return {};
    return line.substr(p, end - p);
}

std::string M3UParser::trim(const std::string& input) {
    std::size_t left = 0;
    while (left < input.size() && std::isspace(static_cast<unsigned char>(input[left])) != 0) {
        ++left;
    }

    if (left == input.size()) {
        return {};
    }

    std::size_t right = input.size() - 1;
    while (right > left && std::isspace(static_cast<unsigned char>(input[right])) != 0) {
        --right;
    }

    return input.substr(left, right - left + 1);
}

} // namespace timeshiftx
