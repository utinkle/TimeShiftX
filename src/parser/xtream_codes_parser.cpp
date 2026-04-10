#include "chronosstream/parser/xtream_codes_parser.hpp"

#include <cctype>
#include <sstream>

#include "chronosstream/net/http_client.hpp"

namespace chronosstream {

Error XtreamCodesParser::parse(const std::string& raw_data) {
    channels_.clear();

    if (raw_data.empty()) {
        return {ErrorCode::ERR_PARSE_XC_JSON_FAILED, "Xtream JSON 为空"};
    }

    const auto objects = splitJsonObjects(raw_data);
    if (objects.empty()) {
        return {ErrorCode::ERR_PARSE_XC_JSON_FAILED, "Xtream get_live_streams 响应为空或格式非法"};
    }

    std::size_t invalid_count = 0;
    for (const auto& obj_text : objects) {
        Channel ch;
        std::string err_msg;
        if (!mapStreamToChannel(obj_text, ch, err_msg)) {
            ++invalid_count;
            continue;
        }
        channels_.push_back(std::move(ch));
    }

    if (channels_.empty()) {
        return {ErrorCode::ERR_PARSE_XC_JSON_FAILED,
                "Xtream 解析后无有效频道（可能字段缺失或格式错误），无效条目数: " + std::to_string(invalid_count)};
    }

    std::string msg = "Xtream 解析成功，有效频道: " + std::to_string(channels_.size());
    if (invalid_count > 0) {
        msg += "，跳过无效条目: " + std::to_string(invalid_count);
    }
    return {ErrorCode::OK, msg};
}

Error XtreamCodesParser::parseFromApi(const std::string& server_url,
                                      const std::string& username,
                                      const std::string& password,
                                      long timeout_seconds) {
    if (server_url.empty() || username.empty() || password.empty()) {
        return {ErrorCode::ERR_INVALID_ARGUMENT, "Xtream 鉴权参数为空"};
    }

    const std::string url = server_url +
                            "/player_api.php?username=" + username +
                            "&password=" + password +
                            "&action=get_live_streams";

    std::string body;
    Error net_rc = HttpClient::get(url, body, timeout_seconds);
    if (!net_rc.ok()) {
        return net_rc;
    }

    return parse(body);
}

std::vector<Channel> XtreamCodesParser::getChannels() const {
    return channels_;
}

std::string XtreamCodesParser::getFieldValue(const std::string& object_text, const std::string& key) {
    const std::string pattern = "\"" + key + "\"";
    const std::size_t key_pos = object_text.find(pattern);
    if (key_pos == std::string::npos) {
        return {};
    }

    std::size_t colon_pos = object_text.find(':', key_pos + pattern.size());
    if (colon_pos == std::string::npos) {
        return {};
    }

    std::size_t value_start = colon_pos + 1;
    while (value_start < object_text.size() && std::isspace(static_cast<unsigned char>(object_text[value_start])) != 0) {
        ++value_start;
    }

    if (value_start >= object_text.size()) {
        return {};
    }

    // 字符串值
    if (object_text[value_start] == '"') {
        ++value_start;
        std::size_t value_end = value_start;
        while (value_end < object_text.size()) {
            if (object_text[value_end] == '"' && object_text[value_end - 1] != '\\') {
                break;
            }
            ++value_end;
        }
        if (value_end >= object_text.size()) {
            return {};
        }
        return object_text.substr(value_start, value_end - value_start);
    }

    // number / bool / null: 读取到逗号或对象结尾
    std::size_t value_end = value_start;
    while (value_end < object_text.size() && object_text[value_end] != ',' && object_text[value_end] != '}') {
        ++value_end;
    }

    std::string raw = object_text.substr(value_start, value_end - value_start);

    // 去掉尾部空白
    while (!raw.empty() && std::isspace(static_cast<unsigned char>(raw.back())) != 0) {
        raw.pop_back();
    }

    // null 视为无值
    if (raw == "null") {
        return {};
    }

    return raw;
}

bool XtreamCodesParser::mapStreamToChannel(const std::string& object_text,
                                           Channel& out,
                                           std::string& err_msg) {
    out.xc_stream_id = getFieldValue(object_text, "stream_id");
    out.name = getFieldValue(object_text, "name");

    if (out.xc_stream_id.empty() || out.name.empty()) {
        err_msg = "stream_id 或 name 缺失";
        return false;
    }

    out.source_type = Channel::SourceType::XTREAM_CODES;
    out.group_name = getFieldValue(object_text, "category_name");
    out.logo_url = getFieldValue(object_text, "stream_icon");

    out.epg_match_id = getFieldValue(object_text, "epg_channel_id");
    if (out.epg_match_id.empty()) {
        out.epg_match_id = out.name;
    }

    out.live_url = getFieldValue(object_text, "stream_url");

    const std::string archive_flag = getFieldValue(object_text, "tv_archive");
    const std::string archive_days = getFieldValue(object_text, "tv_archive_duration");

    out.supports_catchup = (archive_flag == "1" || archive_flag == "true");
    if (!archive_days.empty()) {
        try {
            out.catchup_days = std::stoi(archive_days);
            if (out.catchup_days > 0) {
                out.supports_catchup = true;
            }
        } catch (const std::exception&) {
            out.catchup_days = 0;
        }
    }

    out.internal_id = "xc|" + out.xc_stream_id;
    return true;
}

std::vector<std::string> XtreamCodesParser::splitJsonObjects(const std::string& json_array_text) {
    std::vector<std::string> objects;

    int depth = 0;
    std::size_t obj_start = std::string::npos;
    bool in_string = false;

    for (std::size_t i = 0; i < json_array_text.size(); ++i) {
        const char c = json_array_text[i];

        if (c == '"' && (i == 0 || json_array_text[i - 1] != '\\')) {
            in_string = !in_string;
            continue;
        }

        if (in_string) {
            continue;
        }

        if (c == '{') {
            if (depth == 0) {
                obj_start = i;
            }
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && obj_start != std::string::npos) {
                objects.push_back(json_array_text.substr(obj_start, i - obj_start + 1));
                obj_start = std::string::npos;
            }
        }
    }

    return objects;
}

} // namespace chronosstream
