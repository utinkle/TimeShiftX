#include <ctime>
#include <iostream>

#include "chronosstream/catchup/catchup_engine.hpp"
#include "chronosstream/facade/playback_facade.hpp"
#include "chronosstream/core/logger.hpp"
#include "chronosstream/core/types.hpp"
#include "chronosstream/utils/time_utils.hpp"

int main() {
    using namespace chronosstream;

    Logger::log(LogLevel::INFO, "ChronosStream Core demo start");

    Channel channel;
    channel.name = "CCTV1";
    channel.source_type = Channel::SourceType::XTREAM_CODES;
    channel.supports_catchup = true;
    channel.xc_stream_id = "1001";
    channel.live_url = "http://example.com/live/cctv1.m3u8";

    std::time_t now = std::time(nullptr);
    Programme prog;
    prog.title = "新闻联播";
    prog.start_time = now - 1800;
    prog.end_time = now - 1;

    ServerCredentials creds;
    creds.server_url = "http://demo.server:8080";
    creds.username = "demo_user";
    creds.password = "demo_pass";

    // 5.5 示例流程1：历史节目点击 -> 尝试回看（此处关闭可用性探测，避免 demo 依赖外网）。
    auto historical = PlaybackFacade::resolveProgrammePlayback(channel, prog, now, creds, false);
    std::cout << "[Historical] mode=" << (historical.mode == PlaybackMode::CATCHUP ? "CATCHUP" : "LIVE")
              << ", url=" << historical.url << '\n';

    // 5.5 示例流程2：当前直播节目 -> 直接直播。
    Programme on_air = prog;
    on_air.end_time = now + 120;
    auto live = PlaybackFacade::resolveProgrammePlayback(channel, on_air, now, creds, false);
    std::cout << "[OnAir] mode=" << (live.mode == PlaybackMode::CATCHUP ? "CATCHUP" : "LIVE")
              << ", url=" << live.url << '\n';

    // 5.5 示例流程3：构造失败 -> 回退直播并给出错误。
    ServerCredentials bad_creds;
    bad_creds.server_url = "http://demo.server:8080";
    auto fallback = PlaybackFacade::resolveProgrammePlayback(channel, prog, now, bad_creds, false);
    std::cout << "[Fallback] mode=" << (fallback.mode == PlaybackMode::CATCHUP ? "CATCHUP" : "LIVE")
              << ", err=" << static_cast<int>(fallback.status.code) << ", msg=" << fallback.status.message << '\n';

    Logger::log(LogLevel::INFO, "ChronosStream Core demo done");
    return 0;
}
