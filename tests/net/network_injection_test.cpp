#include "timeshiftx/catchup_engine.hpp"
#include "timeshiftx/inetwork_plugin.hpp"
#include "timeshiftx/m3u_parser.hpp"
#include "timeshiftx/xtream_codes_parser.hpp"

#include <cstdlib>

using namespace timeshiftx;

namespace {

class InjectedPlugin final : public INetworkPlugin {
public:
    NetworkResponse perform(const NetworkRequest& request) override {
        if (request.method == NetworkMethod::HEAD) {
            return {Error{ErrorCode::ERR_CATCHUP_EXPIRED, "expired"}, 404, {}, {}, 0};
        }

        if (request.url.find("player_api.php") != std::string::npos) {
            return {
                Error{ErrorCode::OK, "ok"},
                200,
                R"([{"stream_id":"1001","name":"CCTV-1","epg_channel_id":"cctv1","stream_url":"http://demo/live/1001.m3u8","tv_archive":"1","tv_archive_duration":"3"}])",
                {},
                0,
            };
        }

        return {
            Error{ErrorCode::OK, "ok"},
            200,
            "#EXTM3U\n#EXTINF:-1 tvg-id=\"cctv1\",CCTV-1\nhttp://demo/live/cctv1.m3u8\n",
            {},
            0,
        };
    }

    std::string name() const override {
        return "injected";
    }
};

} // namespace

int main() {
    InjectedPlugin plugin;

    M3UParser m3u;
    if (!m3u.parseFromUrl("http://source/m3u", 3, &plugin).ok()) return EXIT_FAILURE;
    const auto m3u_channels = m3u.getChannels();
    if (m3u_channels.size() != 1) return EXIT_FAILURE;

    XtreamCodesParser xc;
    if (!xc.parseFromApi("http://server", "u", "p", 3, &plugin).ok()) return EXIT_FAILURE;
    const auto xc_channels = xc.getChannels();
    if (xc_channels.size() != 1) return EXIT_FAILURE;

    const Error probe = CatchupEngine::probeAvailability("http://catchup/probe", 3, 1, &plugin);
    if (probe.code != ErrorCode::ERR_CATCHUP_EXPIRED) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
