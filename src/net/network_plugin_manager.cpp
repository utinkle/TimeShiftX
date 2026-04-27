#include "timeshiftx/network_plugin_manager.hpp"

namespace timeshiftx {

NetworkPluginManager& NetworkPluginManager::instance() {
    static NetworkPluginManager manager;
    return manager;
}

bool NetworkPluginManager::registerPlugin(const std::shared_ptr<INetworkPlugin>& plugin, bool make_preferred) {
    if (!plugin) {
        return false;
    }

    const std::string plugin_name = plugin->name();
    if (plugin_name.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    plugins_[plugin_name] = plugin;

    if (make_preferred || preferred_plugin_.empty()) {
        preferred_plugin_ = plugin_name;
    }

    return true;
}

bool NetworkPluginManager::unregisterPlugin(const std::string& plugin_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = plugins_.find(plugin_name);
    if (it == plugins_.end()) {
        return false;
    }

    plugins_.erase(it);
    if (preferred_plugin_ == plugin_name) {
        preferred_plugin_.clear();
        if (!plugins_.empty()) {
            preferred_plugin_ = plugins_.begin()->first;
        }
    }

    return true;
}

bool NetworkPluginManager::setPreferredPlugin(const std::string& plugin_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (plugins_.find(plugin_name) == plugins_.end()) {
        return false;
    }
    preferred_plugin_ = plugin_name;
    return true;
}

std::string NetworkPluginManager::preferredPluginName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return preferred_plugin_;
}

std::shared_ptr<INetworkPlugin> NetworkPluginManager::preferredPlugin() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (preferred_plugin_.empty()) {
        return nullptr;
    }

    const auto it = plugins_.find(preferred_plugin_);
    if (it == plugins_.end()) {
        return nullptr;
    }

    return it->second;
}

std::shared_ptr<INetworkPlugin> NetworkPluginManager::plugin(const std::string& plugin_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = plugins_.find(plugin_name);
    if (it == plugins_.end()) {
        return nullptr;
    }
    return it->second;
}

} // namespace timeshiftx
