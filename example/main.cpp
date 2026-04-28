#include <ctime>
#include <iostream>

#include "timeshiftx/catchup_engine.hpp"
#include "timeshiftx/inetwork_plugin.hpp"
#include "timeshiftx/m3u_parser.hpp"
#include "timeshiftx/network_plugin_manager.hpp"
#include "timeshiftx/playback_facade.hpp"
#include "timeshiftx/types.hpp"
#include "timeshiftx/xtream_codes_parser.hpp"

namespace {

class DemoInjectedPlugin final : public timeshiftx::INetworkPlugin {
public:
    std::string name() const override { return "demo-injected"; }

    timeshiftx::NetworkResponse perform(const timeshiftx::NetworkRequest& request) override {
        using namespace timeshiftx;
        if (request.method == NetworkMethod::HEAD) {
            return {Error{ErrorCode::OK, "mock head ok"}, 200, {}, {}, 0};
        }

        if (request.url.find("player_api.php") != std::string::npos) {
            return {
                Error{ErrorCode::OK, "mock xc ok"},
                200,
                R"([{"stream_id":"1001","name":"CCTV-1","epg_channel_id":"cctv1","stream_url":"http://demo/live/1001.m3u8","tv_archive":"1","tv_archive_duration":"3"}])",
                {},
                0,
            };
        }

        return {
            timeshiftx::Error{timeshiftx::ErrorCode::OK, "mock m3u ok"},
            200,
            "#EXTM3U\n#EXTINF:-1 tvg-id=\"cctv1\",CCTV-1\nhttp://demo/live/cctv1.m3u8\n",
            {},
            0,
        };
    }
};

const char* compiledBackend() {
#if defined(TIMESHIFTX_NETWORK_BACKEND_QT)
    return "qt";
#else
    return "libcurl";
#endif
}

} // namespace

int main() {
    using namespace timeshiftx;

    std::cout << "[Demo] compiled backend = " << compiledBackend() << '\n';
    std::cout << "[Demo] preferred plugin = " << NetworkPluginManager::instance().preferredPluginName() << '\n';

    DemoInjectedPlugin injected;

    // 示例1：M3U 解析（通过 injected plugin，不依赖外网）
    M3UParser m3u;
    Error m3u_rc = m3u.parseFromUrl("http://demo/m3u", 3, &injected);
    std::cout << "[M3U] rc=" << static_cast<int>(m3u_rc.code) << ", channels=" << m3u.getChannels().size() << '\n';

    // 示例2：Xtream Codes 解析（通过 injected plugin）
    XtreamCodesParser xc;
    Error xc_rc = xc.parseFromApi("http://server", "user", "pass", 3, &injected);
    std::cout << "[XC] rc=" << static_cast<int>(xc_rc.code) << ", channels=" << xc.getChannels().size() << '\n';

    // 示例3：回看 URL 生成 + 可用性探测（probe 使用 injected plugin）
    Channel channel;
    channel.name = "CCTV1";
    channel.source_type = Channel::SourceType::XTREAM_CODES;
    channel.catchup_declared = true;
    channel.catchup_days = 3;
    channel.xc_stream_id = "1001";
    channel.live_url = "http://demo/live/cctv1.m3u8";

    std::time_t now = std::time(nullptr);
    Programme prog;
    prog.title = "新闻联播";
    prog.start_time = now - 1800;
    prog.end_time = now - 1;

    ServerCredentials creds;
    creds.server_url = "http://demo.server:8080";
    creds.username = "demo_user";
    creds.password = "demo_pass";

    auto decision = PlaybackFacade::resolveProgrammePlayback(channel, prog, creds, false);
    Error probe = CatchupEngine::probeAvailability(decision.url, 3, 1, &injected);

    std::cout << "[Catchup] mode=" << (decision.mode == PlaybackMode::CATCHUP ? "CATCHUP" : "LIVE")
              << ", probe_rc=" << static_cast<int>(probe.code)
              << ", url=" << decision.url << '\n';

    return 0;
}
