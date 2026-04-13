#include <cstdlib>
#include <ctime>

#include "timeshiftx/playback_facade.hpp"

int main() {
    const std::time_t now = std::time(nullptr);

    // 场景1：节目未结束 -> 直播。
    timeshiftx::Channel live_ch;
    live_ch.live_url = "http://demo/live.m3u8";

    timeshiftx::Programme on_air;
    on_air.start_time = now - 300;
    on_air.end_time = now + 300;

    auto live_decision = timeshiftx::PlaybackFacade::resolveProgrammePlayback(live_ch, on_air, now);
    if (live_decision.mode != timeshiftx::PlaybackMode::LIVE) return EXIT_FAILURE;
    if (live_decision.url != live_ch.live_url) return EXIT_FAILURE;

    // 场景2：历史节目 + 可构造回看（关闭探测，避免外网依赖）-> 回看。
    timeshiftx::Channel m3u;
    m3u.source_type = timeshiftx::Channel::SourceType::M3U;
    m3u.supports_catchup = true;
    m3u.live_url = "http://demo/live.m3u8";
    m3u.catchup_type = "append";
    m3u.catchup_template = "?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}";
    m3u.user_agent = "UA-TEST";
    m3u.referer = "https://ref.example";

    timeshiftx::Programme ended;
    ended.start_time = 1712707200; // 2024-04-10 00:00:00 UTC
    ended.end_time = 1712710800;   // 2024-04-10 01:00:00 UTC

    auto catchup_decision = timeshiftx::PlaybackFacade::resolveProgrammePlayback(m3u, ended, now, {}, false);
    if (catchup_decision.mode != timeshiftx::PlaybackMode::CATCHUP) return EXIT_FAILURE;
    if (catchup_decision.url.find("playseek=") == std::string::npos) return EXIT_FAILURE;
    if (catchup_decision.http_headers["User-Agent"] != "UA-TEST") return EXIT_FAILURE;
    if (catchup_decision.http_headers["Referer"] != "https://ref.example") return EXIT_FAILURE;

    // 场景3：历史节目 + 回看构造失败 -> 回退直播并上报错误。
    timeshiftx::Channel broken_xc;
    broken_xc.source_type = timeshiftx::Channel::SourceType::XTREAM_CODES;
    broken_xc.supports_catchup = true;
    broken_xc.live_url = "http://demo/live/1001.m3u8";
    broken_xc.xc_stream_id = "1001";

    timeshiftx::ServerCredentials bad_creds; // 空用户名/密码 -> 构造失败
    bad_creds.server_url = "http://server:8080";

    auto fallback_decision = timeshiftx::PlaybackFacade::resolveProgrammePlayback(broken_xc, ended, now, bad_creds, false);
    if (fallback_decision.mode != timeshiftx::PlaybackMode::LIVE) return EXIT_FAILURE;
    if (fallback_decision.status.code != timeshiftx::ErrorCode::ERR_CATCHUP_BUILD_FAILED) return EXIT_FAILURE;

    // 异步接口：应可正常返回结果。
    auto future_decision = timeshiftx::PlaybackFacade::resolveProgrammePlaybackAsync(m3u, ended, now, {}, false);
    const auto async_decision = future_decision.get();
    if (async_decision.mode != timeshiftx::PlaybackMode::CATCHUP) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
