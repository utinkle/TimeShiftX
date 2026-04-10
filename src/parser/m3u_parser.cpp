#include "chronosstream/parser/m3u_parser.hpp"

#include <cctype>
#include <sstream>

#include "chronosstream/net/http_client.hpp"

namespace chronosstream {

Error M3UParser::parse(const std::string& raw_data) {
    channels_.clear();

    if (raw_data.empty()) {
        return {ErrorCode::ERR_PARSE_M3U_FAILED, "M3U 文本为空"};
    }

    std::istringstream iss(raw_data);
    std::string line;
    Channel pending_channel;
    bool has_pending_extinf = false;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        // 忽略 M3U 头信息。
        if (line.rfind("#EXTM3U", 0) == 0) {
            continue;
        }

        // 处理频道描述行：抽取属性与展示名称。
        if (line.rfind("#EXTINF:", 0) == 0) {
            pending_channel = parseExtInfLine(line);
            pending_channel.source_type = Channel::SourceType::M3U;
            has_pending_extinf = true;
            continue;
        }

        // 忽略其它注释行。
        if (!line.empty() && line[0] == '#') {
            continue;
        }

        // 遇到 URL 行：与上一个 #EXTINF 配对形成完整频道。
        if (has_pending_extinf) {
            pending_channel.live_url = line;

            // 若 tvg-id 缺失，则降级使用 tvg-name 作为 EPG 匹配入口。
            if (pending_channel.epg_match_id.empty()) {
                pending_channel.epg_match_id = pending_channel.name;
            }

            pending_channel.internal_id = pending_channel.name + "|" + pending_channel.live_url;
            channels_.push_back(pending_channel);
            has_pending_extinf = false;
        }
    }

    if (channels_.empty()) {
        return {ErrorCode::ERR_PARSE_M3U_FAILED, "未解析出有效频道"};
    }

    return {ErrorCode::OK, "M3U 解析成功"};
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

    // 频道显示名称位于第一个逗号之后。
    const std::size_t comma_pos = extinf_line.find(',');
    if (comma_pos != std::string::npos && comma_pos + 1 < extinf_line.size()) {
        ch.name = trim(extinf_line.substr(comma_pos + 1));
    }

    // 解析 M3U 常见扩展属性。
    const std::string tvg_id = extractQuotedAttr(extinf_line, "tvg-id");
    const std::string tvg_name = extractQuotedAttr(extinf_line, "tvg-name");

    ch.group_name = extractQuotedAttr(extinf_line, "group-title");
    ch.logo_url = extractQuotedAttr(extinf_line, "tvg-logo");

    ch.catchup_type = extractQuotedAttr(extinf_line, "catchup");
    ch.catchup_template = extractQuotedAttr(extinf_line, "catchup-source");

    // EPG 匹配优先 tvg-id；缺失时回退 tvg-name；再缺失回退频道名。
    if (!tvg_id.empty()) {
        ch.epg_match_id = tvg_id;
    } else if (!tvg_name.empty()) {
        ch.epg_match_id = tvg_name;
    }

    // 频道名优先级：显示名 > tvg-name。
    if (ch.name.empty() && !tvg_name.empty()) {
        ch.name = tvg_name;
    }

    // 简化规则：只要存在 catchup 或 catchup-source，即视为可回看。
    ch.supports_catchup = !ch.catchup_type.empty() || !ch.catchup_template.empty();

    return ch;
}

std::string M3UParser::extractQuotedAttr(const std::string& line, const std::string& key) {
    const std::string token = key + "=\"";
    const std::size_t start = line.find(token);
    if (start == std::string::npos) {
        return {};
    }

    const std::size_t value_start = start + token.size();
    const std::size_t value_end = line.find('"', value_start);
    if (value_end == std::string::npos || value_end <= value_start) {
        return {};
    }

    return line.substr(value_start, value_end - value_start);
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

} // namespace chronosstream
