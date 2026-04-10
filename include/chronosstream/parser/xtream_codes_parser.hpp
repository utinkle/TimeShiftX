#pragma once

#include <string>
#include <vector>

#include "chronosstream/parser/isource_parser.hpp"

namespace chronosstream {

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
    // 从对象文本中读取字段值（兼容 "str"/number/bool/null）。
    static std::string getFieldValue(const std::string& object_text, const std::string& key);

    // 将 Xtream 单条对象文本转换为 Channel；失败返回 false 并写入错误信息。
    static bool mapStreamToChannel(const std::string& object_text,
                                   Channel& out,
                                   std::string& err_msg);

    // 将 JSON 数组中的每个对象切分成字符串片段。
    static std::vector<std::string> splitJsonObjects(const std::string& json_array_text);

private:
    std::vector<Channel> channels_;
};

} // namespace chronosstream
