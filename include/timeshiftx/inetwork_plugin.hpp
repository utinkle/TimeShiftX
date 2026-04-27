#pragma once

#include <future>
#include <memory>
#include <cstdint>
#include <string>

#include "timeshiftx/network_types.hpp"

namespace timeshiftx {

class INetworkPlugin {
public:
    virtual ~INetworkPlugin() = default;

    virtual std::string name() const = 0;
    virtual NetworkResponse perform(const NetworkRequest& request) = 0;
    virtual void performAsync(const NetworkRequest& request, NetworkCallback callback) {
        std::async(std::launch::async, [this, request, callback = std::move(callback)]() mutable {
            callback(perform(request));
        });
    }
    virtual std::uint64_t performAsyncTracked(const NetworkRequest& request, NetworkCallback callback) {
        performAsync(request, std::move(callback));
        return 0;
    }
    virtual bool cancel(std::uint64_t) { return false; }

    virtual std::future<NetworkResponse> performAsyncFuture(const NetworkRequest& request) {
        auto promise = std::make_shared<std::promise<NetworkResponse>>();
        std::future<NetworkResponse> fut = promise->get_future();
        performAsync(request, [promise](NetworkResponse resp) {
            promise->set_value(std::move(resp));
        });
        return fut;
    }
};

} // namespace timeshiftx
