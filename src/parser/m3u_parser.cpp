#include "timeshiftx/m3u_parser.hpp"
#include "timeshiftx/network_service.hpp"

#include <cctype>
#include <cstdlib>
#include <unordered_map>
#include <sstream>
#include <utility>


namespace timeshiftx {

namespace {

std::size_t findUnquotedComma(const std::string& line) {
    char quote = '\0';
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if ((c == '"' || c == '\'') && (i == 0 || line[i - 1] != '\\')) {
            quote = quote == '\0' ? c : (quote == c ? '\0' : quote);
            continue;
        }
        if (c == ',' && quote == '\0') return i;
    }
    return std::string::npos;
}

std::string unescapeAttributeValue(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    bool escaped = false;
    for (const char c : value) {
        if (escaped) {
            out.push_back(c);
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else {
            out.push_back(c);
        }
    }
    if (escaped) out.push_back('\\');
    return out;
}

std::unordered_map<std::string, std::string> parseAttributes(const std::string& text) {
    std::unordered_map<std::string, std::string> attrs;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) ++i;
        const std::size_t key_start = i;
        while (i < text.size() && text[i] != '=' && std::isspace(static_cast<unsigned char>(text[i])) == 0 && text[i] != ',') ++i;
        if (i == key_start) {
            ++i;
            continue;
        }

        std::string key = text.substr(key_start, i - key_start);
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) ++i;
        if (i >= text.size() || text[i] != '=') continue;
        ++i;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) ++i;
        if (i >= text.size()) break;

        std::string value;
        if (text[i] == '"' || text[i] == '\'') {
            const char quote = text[i++];
            bool escaped = false;
            while (i < text.size()) {
                const char c = text[i++];
                if (escaped) {
                    value.push_back('\\');
                    value.push_back(c);
                    escaped = false;
                    continue;
                }
                if (c == '\\') {
                    escaped = true;
                    continue;
                }
                if (c == quote) break;
                value.push_back(c);
            }
            if (escaped) value.push_back('\\');
        } else {
            const std::size_t value_start = i;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) ++i;
            value = text.substr(value_start, i - value_start);
        }
        attrs[std::move(key)] = unescapeAttributeValue(value);
    }
    return attrs;
}

int parsePositiveInt(const std::string& value) {
    if (value.empty()) return 0;
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    return end != value.c_str() && parsed > 0 ? static_cast<int>(parsed) : 0;
}

void addWarning(ParseDiagnostics& diagnostics, std::size_t position, std::string code, std::string message) {
    diagnostics.warnings.push_back({position, std::move(code), std::move(message)});
}

} // namespace

Error M3UParser::parse(const std::string& raw_data) {
    channels_.clear();
    diagnostics_ = {};
    epg_url_.clear();

    if (raw_data.empty()) {
        return {ErrorCode::ERR_PARSE_M3U_FAILED, "M3U text is empty"};
    }

    std::istringstream iss(raw_data);
    std::string line;
    Channel pending_channel;
    bool has_pending_extinf = false;
    std::string pending_user_agent;
    std::string pending_referer;

    // 重置全局catchup配置
    global_catchup_type_.clear();
    global_catchup_template_.clear();
    global_catchup_days_ = 0;
    std::size_t line_number = 0;

    while (std::getline(iss, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        // Ignore M3U header information.
        if (line.rfind("#EXTM3U", 0) == 0) {
            // 尝试提取 x-tvg-url 属性
            std::string url = extractQuotedAttr(line, "x-tvg-url");
            if (url.empty()) url = extractQuotedAttr(line, "url-tvg");
            if (!url.empty()) {
                epg_url_ = url;
            }
            // 提取全局 catchup 配置
            global_catchup_type_ = extractQuotedAttr(line, "catchup");
            global_catchup_template_ = extractQuotedAttr(line, "catchup-source");
            global_catchup_days_ = parsePositiveInt(extractQuotedAttr(line, "catchup-days"));
            if (global_catchup_days_ == 0) global_catchup_days_ = parsePositiveInt(extractQuotedAttr(line, "timeshift"));
            // 继续跳过该行
            continue;
        }

        // Process channel description line: extract attributes and display name.
        if (line.rfind("#EXTINF:", 0) == 0) {
            if (has_pending_extinf) {
                ++diagnostics_.skipped_entries;
                addWarning(diagnostics_,
                           line_number,
                           "pending_extinf_replaced",
                           "A new #EXTINF appeared before the previous channel URL; previous channel was skipped");
            }
            pending_channel = parseExtInfLineWithGlobalCatchup(line);
            pending_channel.source_type = Channel::SourceType::M3U;
            ++diagnostics_.total_entries;

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
                if (has_pending_extinf) {
                    if (k == "http-user-agent") pending_channel.user_agent = v;
                    if (k == "http-referrer" || k == "http-referer") pending_channel.referer = v;
                } else {
                    if (k == "http-user-agent") pending_user_agent = v;
                    if (k == "http-referrer" || k == "http-referer") pending_referer = v;
                }
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

            if (pending_channel.name.empty()) {
                pending_channel.name = pending_channel.live_url;
                addWarning(diagnostics_, line_number, "missing_channel_name", "Channel display name is empty; live URL was used as fallback");
            }

            pending_channel.internal_id = pending_channel.name + "|" + pending_channel.live_url;
            channels_.push_back(pending_channel);
            ++diagnostics_.valid_entries;
            has_pending_extinf = false;

            // Single consumption to avoid leakage to subsequent unrelated channels.
            pending_user_agent.clear();
            pending_referer.clear();
        } else {
            addWarning(diagnostics_, line_number, "orphan_url", "URL line ignored because it has no preceding #EXTINF");
        }
    }

    if (has_pending_extinf) {
        ++diagnostics_.skipped_entries;
        addWarning(diagnostics_, line_number, "missing_channel_url", "Last #EXTINF entry has no following URL and was skipped");
    }

    diagnostics_.skipped_entries += diagnostics_.total_entries >= diagnostics_.valid_entries + diagnostics_.skipped_entries
        ? diagnostics_.total_entries - diagnostics_.valid_entries - diagnostics_.skipped_entries
        : 0;

    if (channels_.empty()) {
        return {ErrorCode::ERR_PARSE_M3U_FAILED, "No valid channels parsed"};
    }

    return {ErrorCode::OK, "M3U parsing successful"};
}

Error M3UParser::parseFromUrl(const std::string& url, long timeout_seconds, INetworkPlugin* injected_plugin) {
    std::string body;
    Error net_rc = NetworkService::get(url, body, timeout_seconds, 1, injected_plugin);
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
    const std::size_t comma_pos = findUnquotedComma(extinf_line);
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
    ch.catchup_days = parsePositiveInt(extractQuotedAttr(extinf_line, "catchup-days"));
    if (ch.catchup_days == 0) ch.catchup_days = parsePositiveInt(extractQuotedAttr(extinf_line, "timeshift"));
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
    ch.catchup_declared = !ch.catchup_type.empty() || !ch.catchup_template.empty() || ch.catchup_days > 0;

    return ch;
}

Channel M3UParser::parseExtInfLineWithGlobalCatchup(const std::string& extinf_line) const {
    Channel ch = parseExtInfLine(extinf_line);
    
    // 实现fallback逻辑：自身 > 全局 > 无支持
    // 如果channel自身没有catchup类型，使用全局的
    if (ch.catchup_type.empty()) {
        ch.catchup_type = global_catchup_type_;
    }
    // 如果channel自身没有catchup模板，使用全局的
    if (ch.catchup_template.empty()) {
        ch.catchup_template = global_catchup_template_;
    }
    if (ch.catchup_days == 0) {
        ch.catchup_days = global_catchup_days_;
    }
    // 更新catchup_declared：只要自身或全局任意一个定义了，就认为支持回看
    ch.catchup_declared = !ch.catchup_type.empty() || !ch.catchup_template.empty() || ch.catchup_days > 0;
    
    return ch;
}

std::string M3UParser::extractQuotedAttr(const std::string& line, const std::string& key) {
    const auto attrs = parseAttributes(line);
    const auto it = attrs.find(key);
    return it == attrs.end() ? std::string{} : it->second;
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
