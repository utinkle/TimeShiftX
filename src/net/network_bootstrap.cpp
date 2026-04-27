#include "timeshiftx/network_plugin_manager.hpp"
#if defined(TIMESHIFTX_NETWORK_BACKEND_QT)
#include "timeshiftx/qt_network_plugin.hpp"
#else
#include "timeshiftx/libcurl_network_plugin.hpp"
#endif

namespace timeshiftx {
namespace {

struct DefaultNetworkPluginBootstrap {
    DefaultNetworkPluginBootstrap() {
#if defined(TIMESHIFTX_NETWORK_BACKEND_QT)
        NetworkPluginManager::instance().registerPlugin(createQtNetworkPlugin(), true);
#else
        NetworkPluginManager::instance().registerPlugin(createLibcurlNetworkPlugin(), true);
#endif
    }
};

DefaultNetworkPluginBootstrap g_default_network_plugin_bootstrap;

} // namespace
} // namespace timeshiftx
