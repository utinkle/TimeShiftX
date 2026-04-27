#pragma once

#include <future>
#include <memory>
#include <cstdint>
#include <string>
#include <thread>           
#include <atomic>

#include "timeshiftx/network_types.hpp"

namespace timeshiftx {

class INetworkPlugin {
public:
    virtual ~INetworkPlugin() = default;

    virtual std::string name() const = 0;
    virtual NetworkResponse perform(const NetworkRequest& request) = 0;

    virtual void performAsync(const NetworkRequest& request, NetworkCallback callback) {
        std::thread([this, request, callback = std::move(callback)]() mutable {
            callback(perform(request));
        }).detach();
    }

    virtual std::uint64_t performAsyncTracked(const NetworkRequest& request, NetworkCallback callback) {
        static std::atomic<std::uint64_t> nextId{1};
        std::uint64_t id = nextId.fetch_add(1);
        performAsync(request, std::move(callback));
        return id;
    }

    virtual bool cancel(std::uint64_t) { return false; }

    virtual std::future<NetworkResponse> performAsyncFuture(const NetworkRequest& request) {
        auto promise = std::make_shared<std::promise<NetworkResponse>>();
        std::future<NetworkResponse> fut = promise->get_future();
        performAsync(request, [promise](NetworkResponse resp) mutable {
            promise->set_value(std::move(resp));
        });
        return fut;
    }
};

} // namespace timeshiftx
