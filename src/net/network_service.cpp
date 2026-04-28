#include "timeshiftx/network_service.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "timeshiftx/inetwork_plugin.hpp"
#include "timeshiftx/network_plugin_manager.hpp"

namespace timeshiftx {

namespace {

NetworkResponse noPluginResponse() {
    return {
        Error{ErrorCode::ERR_INTERNAL, "No network plugin available"},
        0,
        {},
        {},
        0,
    };
}

std::shared_ptr<INetworkPlugin> resolveManagerPlugin(const std::string& plugin_name) {
    auto& manager = NetworkPluginManager::instance();

    if (!plugin_name.empty()) {
        return manager.plugin(plugin_name);
    }

    if (!manager.preferredPluginName().empty()) {
        return manager.preferredPlugin();
    }

    return manager.plugin({});
}

bool shouldRetry(const NetworkResponse& resp, int attempt, int max_retries) {
    if (attempt >= max_retries) {
        return false;
    }
    return !resp.error.ok();
}

} // namespace

NetworkResponse NetworkService::requestOnce(const NetworkRequest& request,
                                            const std::string& plugin_name,
                                            INetworkPlugin* injected_plugin) {
    if (injected_plugin != nullptr) {
        return injected_plugin->perform(request);
    }

    std::shared_ptr<INetworkPlugin> net_plugin = resolveManagerPlugin(plugin_name);
    if (!net_plugin) {
        return noPluginResponse();
    }

    return net_plugin->perform(request);
}

NetworkResponse NetworkService::request(const NetworkRequest& request,
                                        const std::string& plugin_name,
                                        INetworkPlugin* injected_plugin) {
    const int retries = (request.max_retries < 1) ? 1 : request.max_retries;

    NetworkResponse last = noPluginResponse();
    for (int attempt = 1; attempt <= retries; ++attempt) {
        last = requestOnce(request, plugin_name, injected_plugin);
        if (!shouldRetry(last, attempt, retries)) {
            return last;
        }

        if (request.retry_delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(request.retry_delay_ms));
        }
    }

    return last;
}

void NetworkService::requestAsync(const NetworkRequest& request,
                                  NetworkCallback callback,
                                  const std::string& plugin_name,
                                  INetworkPlugin* injected_plugin) {
    std::thread([request, callback = std::move(callback), plugin_name, injected_plugin]() mutable {
        callback(NetworkService::request(request, plugin_name, injected_plugin));
    }).detach();
}

std::uint64_t NetworkService::requestAsyncTracked(const NetworkRequest& request,
                                                  NetworkCallback callback,
                                                  const std::string& plugin_name,
                                                  INetworkPlugin* injected_plugin) {
    if (injected_plugin != nullptr) {
        return injected_plugin->performAsyncTracked(request, std::move(callback));
    }

    std::shared_ptr<INetworkPlugin> net_plugin = resolveManagerPlugin(plugin_name);
    if (!net_plugin) {
        std::thread([cb = std::move(callback)]() mutable {
            cb(noPluginResponse());
        }).detach();
        return 0;
    }

    return net_plugin->performAsyncTracked(request, std::move(callback));
}

bool NetworkService::cancel(std::uint64_t request_id,
                            const std::string& plugin_name,
                            INetworkPlugin* injected_plugin) {
    if (injected_plugin != nullptr) {
        return injected_plugin->cancel(request_id);
    }

    std::shared_ptr<INetworkPlugin> net_plugin = resolveManagerPlugin(plugin_name);
    if (!net_plugin) {
        return false;
    }
    return net_plugin->cancel(request_id);
}

std::future<NetworkResponse> NetworkService::requestAsyncFuture(const NetworkRequest& request,
                                                                const std::string& plugin_name,
                                                                INetworkPlugin* injected_plugin) {
    auto promise = std::make_shared<std::promise<NetworkResponse>>();
    std::future<NetworkResponse> fut = promise->get_future();

    requestAsync(
        request,
        [promise](NetworkResponse resp) {
            promise->set_value(std::move(resp));
        },
        plugin_name,
        injected_plugin);

    return fut;
}

Error NetworkService::get(const std::string& url,
                          std::string& out_body,
                          long timeout_seconds,
                          int max_retries,
                          INetworkPlugin* injected_plugin) {
    NetworkRequest req;
    req.method = NetworkMethod::GET;
    req.url = url;
    req.timeout_seconds = timeout_seconds;
    req.max_retries = max_retries;

    const NetworkResponse resp = request(req, {}, injected_plugin);
    if (!resp.error.ok()) {
        return resp.error;
    }

    out_body = resp.body;
    return resp.error;
}

Error NetworkService::head(const std::string& url, long timeout_seconds, int max_retries, INetworkPlugin* injected_plugin) {
    NetworkRequest req;
    req.method = NetworkMethod::HEAD;
    req.url = url;
    req.timeout_seconds = timeout_seconds;
    req.max_retries = max_retries;

    const NetworkResponse resp = request(req, {}, injected_plugin);
    return resp.error;
}

void NetworkService::getAsync(const std::string& url,
                              NetworkCallback callback,
                              long timeout_seconds,
                              int max_retries,
                              INetworkPlugin* injected_plugin) {
    NetworkRequest req;
    req.method = NetworkMethod::GET;
    req.url = url;
    req.timeout_seconds = timeout_seconds;
    req.max_retries = max_retries;

    requestAsync(req, std::move(callback), {}, injected_plugin);
}

void NetworkService::headAsync(const std::string& url,
                               NetworkCallback callback,
                               long timeout_seconds,
                               int max_retries,
                               INetworkPlugin* injected_plugin) {
    NetworkRequest req;
    req.method = NetworkMethod::HEAD;
    req.url = url;
    req.timeout_seconds = timeout_seconds;
    req.max_retries = max_retries;

    requestAsync(req, std::move(callback), {}, injected_plugin);
}

} // namespace timeshiftx
