#pragma once

#include <future>
#include <string>

#include "timeshiftx/network_types.hpp"

namespace timeshiftx {

class INetworkPlugin;

class NetworkService {
public:
    static NetworkResponse requestOnce(const NetworkRequest& request,
                                       const std::string& plugin_name = {},
                                       INetworkPlugin* injected_plugin = nullptr);
    static NetworkResponse request(const NetworkRequest& request,
                                   const std::string& plugin_name = {},
                                   INetworkPlugin* injected_plugin = nullptr);
    static void requestAsync(const NetworkRequest& request,
                             NetworkCallback callback,
                             const std::string& plugin_name = {},
                             INetworkPlugin* injected_plugin = nullptr);
    static std::uint64_t requestAsyncTracked(const NetworkRequest& request,
                                             NetworkCallback callback,
                                             const std::string& plugin_name = {},
                                             INetworkPlugin* injected_plugin = nullptr);
    static bool cancel(std::uint64_t request_id,
                       const std::string& plugin_name = {},
                       INetworkPlugin* injected_plugin = nullptr);
    static std::future<NetworkResponse> requestAsyncFuture(const NetworkRequest& request,
                                                           const std::string& plugin_name = {},
                                                           INetworkPlugin* injected_plugin = nullptr);

    static Error get(const std::string& url,
                     std::string& out_body,
                     long timeout_seconds = 10L,
                     int max_retries = 1,
                     INetworkPlugin* injected_plugin = nullptr);
    static Error head(const std::string& url,
                      long timeout_seconds = 5L,
                      int max_retries = 1,
                      INetworkPlugin* injected_plugin = nullptr);
    static void getAsync(const std::string& url,
                         NetworkCallback callback,
                         long timeout_seconds = 10L,
                         int max_retries = 1,
                         INetworkPlugin* injected_plugin = nullptr);
    static void headAsync(const std::string& url,
                          NetworkCallback callback,
                          long timeout_seconds = 5L,
                          int max_retries = 1,
                          INetworkPlugin* injected_plugin = nullptr);
};

} // namespace timeshiftx
