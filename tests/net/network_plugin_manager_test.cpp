#include "timeshiftx/network_plugin_manager.hpp"
#include "timeshiftx/network_service.hpp"

#include <cstdlib>
#include <memory>

using namespace timeshiftx;

namespace {

class DummyPlugin final : public INetworkPlugin {
public:
    explicit DummyPlugin(std::string n, std::string body)
        : name_(std::move(n)), body_(std::move(body)) {}

    std::string name() const override { return name_; }

    NetworkResponse perform(const NetworkRequest& request) override {
        if (request.url.empty()) {
            return {Error{ErrorCode::ERR_INVALID_ARGUMENT, "empty"}, 0, {}, {}, 0};
        }
        return {Error{ErrorCode::OK, "ok"}, 200, body_, {}, 0};
    }

private:
    std::string name_;
    std::string body_;
};

} // namespace

int main() {
    auto& manager = NetworkPluginManager::instance();

    manager.unregisterPlugin("dummy-a");
    manager.unregisterPlugin("dummy-b");

    if (!manager.registerPlugin(std::make_shared<DummyPlugin>("dummy-a", "A"), true)) return EXIT_FAILURE;
    if (!manager.registerPlugin(std::make_shared<DummyPlugin>("dummy-b", "B"), false)) return EXIT_FAILURE;

    if (manager.preferredPluginName() != "dummy-a") return EXIT_FAILURE;
    if (!manager.setPreferredPlugin("dummy-b")) return EXIT_FAILURE;
    if (manager.preferredPluginName() != "dummy-b") return EXIT_FAILURE;

    NetworkRequest req;
    req.url = "http://demo";
    req.method = NetworkMethod::GET;

    const NetworkResponse resp = NetworkService::request(req);
    if (!resp.error.ok()) return EXIT_FAILURE;
    if (resp.body != "B") return EXIT_FAILURE;

    manager.unregisterPlugin("dummy-a");
    manager.unregisterPlugin("dummy-b");
    return EXIT_SUCCESS;
}
