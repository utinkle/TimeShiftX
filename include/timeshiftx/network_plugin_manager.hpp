#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "timeshiftx/inetwork_plugin.hpp"

namespace timeshiftx {

class NetworkPluginManager {
public:
    static NetworkPluginManager& instance();

    bool registerPlugin(const std::shared_ptr<INetworkPlugin>& plugin, bool make_preferred = false);
    bool unregisterPlugin(const std::string& plugin_name);

    bool setPreferredPlugin(const std::string& plugin_name);
    std::string preferredPluginName() const;

    std::shared_ptr<INetworkPlugin> preferredPlugin() const;
    std::shared_ptr<INetworkPlugin> plugin(const std::string& plugin_name) const;

private:
    NetworkPluginManager() = default;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<INetworkPlugin>> plugins_;
    std::string preferred_plugin_;
};

} // namespace timeshiftx
