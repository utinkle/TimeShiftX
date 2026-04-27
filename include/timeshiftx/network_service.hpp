#pragma once

#include <future>
#include <string>

#include "timeshiftx/network_types.hpp"

namespace timeshiftx {

class NetworkService {
public:
    static NetworkResponse request(const NetworkRequest& request, const std::string& plugin_name = {});
    static std::future<NetworkResponse> requestAsync(const NetworkRequest& request, const std::string& plugin_name = {});

    static Error get(const std::string& url, std::string& out_body, long timeout_seconds = 10L, int max_retries = 1);
    static Error head(const std::string& url, long timeout_seconds = 5L, int max_retries = 1);
};

} // namespace timeshiftx
