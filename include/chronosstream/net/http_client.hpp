#pragma once

#include <future>
#include <string>

#include "chronosstream/core/errors.hpp"

namespace chronosstream {

// 轻量网络客户端接口：Phase 2 用于下载 M3U 文本。
class HttpClient {
public:
    struct HttpResponse {
        Error error;
        std::string body;
    };

    // 发起 HTTP GET 请求并将响应体写入 out_body。
    // timeout_seconds：超时时间（秒），避免网络阻塞主线程。
    static Error get(const std::string& url, std::string& out_body, long timeout_seconds = 10L, int max_retries = 1);

    // 发起 HTTP HEAD 请求：用于回看 URL 可用性快速探测（不下载实体内容）。
    static Error head(const std::string& url, long timeout_seconds = 5L, int max_retries = 1);

    // 异步 GET：适用于 UI 线程非阻塞调用。
    static std::future<HttpResponse> getAsync(const std::string& url, long timeout_seconds = 10L, int max_retries = 1);

    // 异步 HEAD：用于非阻塞可用性探测。
    static std::future<Error> headAsync(const std::string& url, long timeout_seconds = 5L, int max_retries = 1);
};

} // namespace chronosstream
