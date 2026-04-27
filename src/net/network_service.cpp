#include "timeshiftx/network_service.hpp"

#include <future>

#include "timeshiftx/network_plugin_manager.hpp"

namespace timeshiftx {

NetworkResponse NetworkService::request(const NetworkRequest& request, const std::string& plugin_name) {
    std::shared_ptr<INetworkPlugin> net_plugin;
    auto& manager = NetworkPluginManager::instance();

    if (!plugin_name.empty()) {
        net_plugin = manager.plugin(plugin_name);
    } else {
        net_plugin = manager.preferredPlugin();
    }

    if (!net_plugin) {
        return {
            Error{ErrorCode::ERR_INTERNAL, "No network plugin available"},
            0,
            {},
            {},
            0,
        };
    }

    return net_plugin->perform(request);
}

std::future<NetworkResponse> NetworkService::requestAsync(const NetworkRequest& request, const std::string& plugin_name) {
    std::shared_ptr<INetworkPlugin> net_plugin;
    auto& manager = NetworkPluginManager::instance();

    if (!plugin_name.empty()) {
        net_plugin = manager.plugin(plugin_name);
    } else {
        net_plugin = manager.preferredPlugin();
    }

    if (!net_plugin) {
        return std::async(std::launch::deferred, [] {
            return NetworkResponse {
                Error{ErrorCode::ERR_INTERNAL, "No network plugin available"},
                0,
                {},
                {},
                0,
            };
        });
    }

    return net_plugin->performAsync(request);
}

Error NetworkService::get(const std::string& url, std::string& out_body, long timeout_seconds, int max_retries) {
    NetworkRequest req;
    req.method = NetworkMethod::GET;
    req.url = url;
    req.timeout_seconds = timeout_seconds;
    req.max_retries = max_retries;

    const NetworkResponse resp = request(req);
    if (!resp.error.ok()) {
        return resp.error;
    }

    out_body = resp.body;
    return resp.error;
}

Error NetworkService::head(const std::string& url, long timeout_seconds, int max_retries) {
    NetworkRequest req;
    req.method = NetworkMethod::HEAD;
    req.url = url;
    req.timeout_seconds = timeout_seconds;
    req.max_retries = max_retries;

    const NetworkResponse resp = request(req);
    return resp.error;
}

} // namespace timeshiftx
