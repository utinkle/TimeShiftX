#include "timeshiftx/http_client.hpp"
#include "timeshiftx/request_queue.hpp"

#include <mutex>
#include <utility>

#include "timeshiftx/libcurl_network_plugin.hpp"
#include "timeshiftx/network_plugin_manager.hpp"
#include "timeshiftx/network_service.hpp"

namespace timeshiftx {
namespace {

void ensureDefaultNetworkPluginRegistered() {
    static std::once_flag once;
    std::call_once(once, [] {
        NetworkPluginManager::instance().registerPlugin(createLibcurlNetworkPlugin(), true);
    });
}

} // namespace

Error HttpClient::get(const std::string& url, std::string& out_body, long timeout_seconds, int max_retries) {
    ensureDefaultNetworkPluginRegistered();
    return NetworkService::get(url, out_body, timeout_seconds, max_retries);
}

Error HttpClient::head(const std::string& url, long timeout_seconds, int max_retries) {
    ensureDefaultNetworkPluginRegistered();
    return NetworkService::head(url, timeout_seconds, max_retries);
}

std::future<HttpClient::HttpResponse> HttpClient::getAsync(const std::string& url, long timeout_seconds, int max_retries) {
    ensureDefaultNetworkPluginRegistered();
    return RequestQueue::instance().enqueueGet(url, timeout_seconds, max_retries);
}

std::future<Error> HttpClient::headAsync(const std::string& url, long timeout_seconds, int max_retries) {
    ensureDefaultNetworkPluginRegistered();
    return RequestQueue::instance().enqueueHead(url, timeout_seconds, max_retries);
}

} // namespace timeshiftx
