#include <cstdlib>
#include <ctime>

#include "chronosstream/facade/playback_facade.hpp"

int main() {
    const std::time_t now = std::time(nullptr);

    // 场景1：节目未结束 -> 直播。
    chronosstream::Channel live_ch;
    live_ch.live_url = "http://demo/live.m3u8";

    chronosstream::Programme on_air;
    on_air.start_time = now - 300;
    on_air.end_time = now + 300;

    auto live_decision = chronosstream::PlaybackFacade::resolveProgrammePlayback(live_ch, on_air, now);
    if (live_decision.mode != chronosstream::PlaybackMode::LIVE) return EXIT_FAILURE;
    if (live_decision.url != live_ch.live_url) return EXIT_FAILURE;

    // 场景2：历史节目 + 可构造回看（关闭探测，避免外网依赖）-> 回看。
    chronosstream::Channel m3u;
    m3u.source_type = chronosstream::Channel::SourceType::M3U;
    m3u.supports_catchup = true;
    m3u.live_url = "http://demo/live.m3u8";
    m3u.catchup_type = "append";
    m3u.catchup_template = "?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}";

    chronosstream::Programme ended;
    ended.start_time = 1712707200; // 2024-04-10 00:00:00 UTC
    ended.end_time = 1712710800;   // 2024-04-10 01:00:00 UTC

    auto catchup_decision = chronosstream::PlaybackFacade::resolveProgrammePlayback(m3u, ended, now, {}, false);
    if (catchup_decision.mode != chronosstream::PlaybackMode::CATCHUP) return EXIT_FAILURE;
    if (catchup_decision.url.find("playseek=") == std::string::npos) return EXIT_FAILURE;

    // 场景3：历史节目 + 回看构造失败 -> 回退直播并上报错误。
    chronosstream::Channel broken_xc;
    broken_xc.source_type = chronosstream::Channel::SourceType::XTREAM_CODES;
    broken_xc.supports_catchup = true;
    broken_xc.live_url = "http://demo/live/1001.m3u8";
    broken_xc.xc_stream_id = "1001";

    chronosstream::ServerCredentials bad_creds; // 空用户名/密码 -> 构造失败
    bad_creds.server_url = "http://server:8080";

    auto fallback_decision = chronosstream::PlaybackFacade::resolveProgrammePlayback(broken_xc, ended, now, bad_creds, false);
    if (fallback_decision.mode != chronosstream::PlaybackMode::LIVE) return EXIT_FAILURE;
    if (fallback_decision.status.code != chronosstream::ErrorCode::ERR_CATCHUP_BUILD_FAILED) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
