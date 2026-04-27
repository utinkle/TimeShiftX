#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "timeshiftx/errors.hpp"

namespace timeshiftx {

enum class NetworkMethod {
    GET,
    HEAD,
};

struct NetworkRequest {
    NetworkMethod method {NetworkMethod::GET};
    std::string url;

    long timeout_seconds {10L};
    int max_retries {1};
    int retry_delay_ms {100};
    bool follow_redirect {true};
    bool strict_ssl {true};

    std::unordered_map<std::string, std::string> headers;
    std::string user_agent {"ChronosStreamCore/0.1"};
};

struct NetworkResponse {
    Error error;
    long http_status {0};
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    std::int64_t latency_ms {0};
};

using NetworkCallback = std::function<void(NetworkResponse)>;

} // namespace timeshiftx
