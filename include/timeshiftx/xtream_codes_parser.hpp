#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "timeshiftx/isource_parser.hpp"

namespace timeshiftx {

// Xtream Codes 解析器：解析 get_live_streams JSON，并映射到统一 Channel 结构。
class XtreamCodesParser final : public ISourceParser {
public:
    // 解析 get_live_streams 返回的 JSON 字符串。
    Error parse(const std::string& raw_data) override;

    // 直接通过 server/username/password 拉取并解析。
    Error parseFromApi(const std::string& server_url,
                       const std::string& username,
                       const std::string& password,
                       long timeout_seconds = 10L);

    std::vector<Channel> getChannels() const override;

private:
    // 安全读取 JSON 字段（兼容 string/number/bool/null）。
    static std::string getStringField(const nlohmann::json& obj, const char* key);

    // 将 Xtream 单条 stream 记录转换为 Channel。
    static bool mapStreamToChannel(const nlohmann::json& stream, Channel& out, std::string& err_msg);

private:
    std::vector<Channel> channels_;
};

} // namespace timeshiftx
