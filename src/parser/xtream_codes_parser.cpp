#include "timeshiftx/xtream_codes_parser.hpp"
#include "timeshiftx/http_client.hpp"

#include <sstream>


namespace timeshiftx {

Error XtreamCodesParser::parse(const std::string& raw_data) {
    channels_.clear();

    if (raw_data.empty()) {
        return {ErrorCode::ERR_PARSE_XC_JSON_FAILED, "Xtream JSON is empty"};
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(raw_data);
    } catch (const std::exception& ex) {
        return {ErrorCode::ERR_PARSE_XC_JSON_FAILED, std::string("Xtream JSON parsing failed: ") + ex.what()};
    }

    if (!root.is_array()) {
        return {ErrorCode::ERR_PARSE_XC_JSON_FAILED, "Xtream get_live_streams response is not an array"};
    }

    std::size_t invalid_count = 0;
    for (const auto& item : root) {
        if (!item.is_object()) {
            ++invalid_count;
            continue;
        }

        Channel ch;
        std::string err_msg;
        if (!mapStreamToChannel(item, ch, err_msg)) {
            ++invalid_count;
            continue;
        }

        channels_.push_back(std::move(ch));
    }

    if (channels_.empty()) {
        return {ErrorCode::ERR_PARSE_XC_JSON_FAILED,
                "No valid channels after Xtream parsing (possible missing fields or format errors), invalid entries: " + std::to_string(invalid_count)};
    }

    std::string msg = "Xtream parsing successful, valid channels: " + std::to_string(channels_.size());
    if (invalid_count > 0) {
        msg += ", skipped invalid entries: " + std::to_string(invalid_count);
    }

    return {ErrorCode::OK, msg};
}

Error XtreamCodesParser::parseFromApi(const std::string& server_url,
                                      const std::string& username,
                                      const std::string& password,
                                      long timeout_seconds) {
    if (server_url.empty() || username.empty() || password.empty()) {
        return {ErrorCode::ERR_INVALID_ARGUMENT, "Xtream authentication parameters are empty"};
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

std::string XtreamCodesParser::getStringField(const nlohmann::json& obj, const char* key) {
    if (!obj.contains(key) || obj[key].is_null()) {
        return {};
    }

    const auto& v = obj[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) {
        std::ostringstream oss;
        oss << v.get<double>();
        return oss.str();
    }
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return {};
}

bool XtreamCodesParser::mapStreamToChannel(const nlohmann::json& stream,
                                           Channel& out,
                                           std::string& err_msg) {
    out.xc_stream_id = getStringField(stream, "stream_id");
    out.name = getStringField(stream, "name");
    if (out.xc_stream_id.empty() || out.name.empty()) {
        err_msg = "stream_id or name missing";
        return false;
    }

    out.source_type = Channel::SourceType::XTREAM_CODES;
    out.group_name = getStringField(stream, "category_name");
    out.logo_url = getStringField(stream, "stream_icon");

    out.epg_match_id = getStringField(stream, "epg_channel_id");
    if (out.epg_match_id.empty()) out.epg_match_id = out.name;

    out.live_url = getStringField(stream, "stream_url");

    const std::string archive_flag = getStringField(stream, "tv_archive");
    const std::string archive_days = getStringField(stream, "tv_archive_duration");
    out.supports_catchup = (archive_flag == "1" || archive_flag == "true");

    if (!archive_days.empty()) {
        try {
            out.catchup_days = std::stoi(archive_days);
            if (out.catchup_days > 0) out.supports_catchup = true;
        } catch (...) {
            out.catchup_days = 0;
        }
    }

    out.internal_id = "xc|" + out.xc_stream_id;
    return true;
}

} // namespace timeshiftx
