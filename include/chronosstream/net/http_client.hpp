#pragma once

#include <string>

#include "chronosstream/core/errors.hpp"

namespace chronosstream {

// 轻量网络客户端接口：Phase 2 用于下载 M3U 文本。
class HttpClient {
public:
    // 发起 HTTP GET 请求并将响应体写入 out_body。
    // timeout_seconds：超时时间（秒），避免网络阻塞主线程。
    static Error get(const std::string& url, std::string& out_body, long timeout_seconds = 10L, int max_retries = 1);

    // 发起 HTTP HEAD 请求：用于回看 URL 可用性快速探测（不下载实体内容）。
    static Error head(const std::string& url, long timeout_seconds = 5L, int max_retries = 1);
};

} // namespace chronosstream
