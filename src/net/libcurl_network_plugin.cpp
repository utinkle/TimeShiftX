#include "timeshiftx/libcurl_network_plugin.hpp"

#include <curl/curl.h>

#include <chrono>
#include <memory>
#include <utility>

namespace timeshiftx {
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

class LibcurlNetworkPlugin final : public INetworkPlugin {
public:
    std::string name() const override {
        return "libcurl";
    }

    NetworkResponse perform(const NetworkRequest& request) override {
        if (request.url.empty()) {
            return {
                Error{ErrorCode::ERR_INVALID_ARGUMENT, "HTTP URL is empty"},
                0,
                {},
                {},
                0,
            };
        }

        const int retries = (request.max_retries < 1) ? 1 : request.max_retries;

        NetworkResponse last_resp;
        last_resp.error = {ErrorCode::ERR_INTERNAL, "Request not executed"};

        for (int attempt = 1; attempt <= retries; ++attempt) {
            const auto begin = std::chrono::steady_clock::now();

            CURL* curl = curl_easy_init();
            if (!curl) {
                return {
                    Error{ErrorCode::ERR_INTERNAL, "curl_easy_init failed"},
                    0,
                    {},
                    {},
                    0,
                };
            }

            std::string body;
            curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, request.follow_redirect ? 1L : 0L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, request.timeout_seconds);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, request.timeout_seconds);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, request.user_agent.c_str());

            struct curl_slist* header_list = nullptr;
            for (const auto& kv : request.headers) {
                const std::string header = kv.first + ": " + kv.second;
                header_list = curl_slist_append(header_list, header.c_str());
            }
            if (header_list) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
            }

            if (request.method == NetworkMethod::HEAD) {
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            } else {
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
            }

            const CURLcode rc = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

            if (header_list) {
                curl_slist_free_all(header_list);
            }
            curl_easy_cleanup(curl);

            const auto end = std::chrono::steady_clock::now();
            const auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

            if (rc != CURLE_OK) {
                last_resp = {
                    Error{ErrorCode::ERR_NETWORK_TIMEOUT,
                          std::string("Network request failed (attempt ") + std::to_string(attempt) + "): " + curl_easy_strerror(rc)},
                    http_code,
                    {},
                    {},
                    cost,
                };
                continue;
            }

            const ErrorCode mapped = mapHttpStatusToError(http_code);
            if (mapped != ErrorCode::OK) {
                last_resp = {
                    Error{mapped,
                          std::string("HTTP status code abnormal: ") + std::to_string(http_code) +
                              " (attempt " + std::to_string(attempt) + ")"},
                    http_code,
                    {},
                    {},
                    cost,
                };
                continue;
            }

            return {
                Error{ErrorCode::OK, "HTTP request successful"},
                http_code,
                std::move(body),
                {},
                cost,
            };
        }

        return last_resp;
    }
};

} // namespace

std::shared_ptr<INetworkPlugin> createLibcurlNetworkPlugin() {
    return std::make_shared<LibcurlNetworkPlugin>();
}

} // namespace timeshiftx
