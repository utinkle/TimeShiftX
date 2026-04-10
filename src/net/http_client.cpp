#include "chronosstream/net/http_client.hpp"

#include <curl/curl.h>
#include <future>
#include <utility>

namespace chronosstream {

namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t total = size * nmemb;
    auto* body = static_cast<std::string*>(userp);
    body->append(static_cast<char*>(contents), total);
    return total;
}

ErrorCode mapHttpStatusToError(long http_code) {
    if (http_code >= 200 && http_code < 300) {
        return ErrorCode::OK;
    }
    if (http_code == 404) {
        return ErrorCode::ERR_CATCHUP_EXPIRED;
    }
    return ErrorCode::ERR_NETWORK_HTTP_STATUS;
}

Error performRequest(const std::string& url,
                     std::string* out_body,
                     long timeout_seconds,
                     int max_retries,
                     bool head_only) {
    if (url.empty()) {
        return {ErrorCode::ERR_INVALID_ARGUMENT, "HTTP URL 为空"};
    }

    if (max_retries < 1) {
        max_retries = 1;
    }

    Error last_error {ErrorCode::ERR_INTERNAL, "未执行请求"};

    for (int attempt = 1; attempt <= max_retries; ++attempt) {
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            return {ErrorCode::ERR_INTERNAL, "curl_easy_init 失败"};
        }

        std::string local_body;
        if (out_body != nullptr) {
            out_body->clear();
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, timeout_seconds);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "ChronosStreamCore/0.1");

        if (head_only) {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        } else {
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &local_body);
        }

        const CURLcode rc = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) {
            last_error = {ErrorCode::ERR_NETWORK_TIMEOUT,
                          std::string("网络请求失败(第") + std::to_string(attempt) + "次): " + curl_easy_strerror(rc)};
            continue;
        }

        const ErrorCode code = mapHttpStatusToError(http_code);
        if (code != ErrorCode::OK) {
            last_error = {code,
                          std::string("HTTP 状态码异常: ") + std::to_string(http_code) +
                              " (第" + std::to_string(attempt) + "次)"};
            continue;
        }

        if (out_body != nullptr) {
            *out_body = std::move(local_body);
        }
        return {ErrorCode::OK, "HTTP 请求成功"};
    }

    return last_error;
}

} // namespace

Error HttpClient::get(const std::string& url, std::string& out_body, long timeout_seconds, int max_retries) {
    return performRequest(url, &out_body, timeout_seconds, max_retries, false);
}

Error HttpClient::head(const std::string& url, long timeout_seconds, int max_retries) {
    return performRequest(url, nullptr, timeout_seconds, max_retries, true);
}

std::future<HttpClient::HttpResponse> HttpClient::getAsync(const std::string& url, long timeout_seconds, int max_retries) {
    return std::async(std::launch::async, [url, timeout_seconds, max_retries]() {
        HttpResponse resp;
        resp.error = HttpClient::get(url, resp.body, timeout_seconds, max_retries);
        return resp;
    });
}

std::future<Error> HttpClient::headAsync(const std::string& url, long timeout_seconds, int max_retries) {
    return std::async(std::launch::async, [url, timeout_seconds, max_retries]() {
        return HttpClient::head(url, timeout_seconds, max_retries);
    });
}

} // namespace chronosstream
