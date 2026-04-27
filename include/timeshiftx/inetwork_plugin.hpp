#pragma once

#include <future>
#include <string>

#include "timeshiftx/network_types.hpp"

namespace timeshiftx {

class INetworkPlugin {
public:
    virtual ~INetworkPlugin() = default;

    virtual std::string name() const = 0;
    virtual NetworkResponse perform(const NetworkRequest& request) = 0;

    virtual std::future<NetworkResponse> performAsync(const NetworkRequest& request) {
        return std::async(std::launch::async, [this, request]() {
            return perform(request);
        });
    }
};

} // namespace timeshiftx
