#pragma once

#include <string>
#include <vector>

#include "timeshiftx/isource_parser.hpp"

namespace timeshiftx {

// M3U 解析器：支持本地文本解析与网络 URL 下载解析。
class M3UParser final : public ISourceParser {
public:
    // 解析 M3U 文本内容（支持 #EXTINF 扩展属性）。
    Error parse(const std::string& raw_data) override;

    // 从 URL 拉取 M3U 并解析（依赖 HttpClient/libcurl）。
    Error parseFromUrl(const std::string& url, long timeout_seconds = 10L);

    // 读取已解析结果。
    std::vector<Channel> getChannels() const override;

private:
    // 解析单行 #EXTINF 并填充 channel 元信息。
    static Channel parseExtInfLine(const std::string& extinf_line);

    // 对属性字符串进行 key="value" 扫描。
    static std::string extractQuotedAttr(const std::string& line, const std::string& key);

    // 去除左右空白字符。
    static std::string trim(const std::string& input);

private:
    std::vector<Channel> channels_;
};

} // namespace timeshiftx
