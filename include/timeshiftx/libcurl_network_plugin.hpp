#pragma once

#include <memory>

#include "timeshiftx/inetwork_plugin.hpp"

namespace timeshiftx {

std::shared_ptr<INetworkPlugin> createLibcurlNetworkPlugin();

} // namespace timeshiftx
