#include <cstdlib>
#include <ctime>
#include <string>

#include "timeshiftx/epg_manager.hpp"
#include "timeshiftx/playback_facade.hpp"
#include "timeshiftx/m3u_parser.hpp"

int main() {
    // 6.2 端到端：频道加载 -> 节目匹配 -> 回看生成。
    const std::string m3u_text =
        "#EXTM3U\n"
        "#EXTINF:-1 tvg-id=\"cctv1\" tvg-name=\"CCTV1\" catchup=\"append\" catchup-source=\"?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}\",CCTV-1 FHD\n"
        "http://demo/live/cctv1.m3u8\n";

    timeshiftx::M3UParser parser;
    if (!parser.parse(m3u_text).ok()) {
        return EXIT_FAILURE;
    }
    const auto channels = parser.getChannels();
    if (channels.empty()) {
        return EXIT_FAILURE;
    }

    const std::string xmltv = R"XML(
<tv>
  <channel id="cctv1"><display-name>CCTV1</display-name></channel>
  <programme start="20260410000000 +0000" stop="20260410010000 +0000" channel="cctv1">
    <title>News</title>
    <desc>demo</desc>
  </programme>
</tv>
)XML";

    timeshiftx::EPGManager epg;
    if (!epg.loadXMLTV(xmltv).ok()) {
        return EXIT_FAILURE;
    }

    const std::string strict_id = epg.resolveStrictEpgId(channels.front());
    if (strict_id != "cctv1") {
        return EXIT_FAILURE;
    }

    std::tm day_tm {};
    day_tm.tm_year = 2026 - 1900;
    day_tm.tm_mon = 3;
    day_tm.tm_mday = 10;
#if defined(_WIN32)
    const std::time_t target_day = _mkgmtime(&day_tm);
#else
    const std::time_t target_day = timegm(&day_tm);
#endif

    const auto timeline = epg.getTimelineForChannel(strict_id, target_day);
    if (timeline.empty()) {
        return EXIT_FAILURE;
    }

    // now 设置为节目结束后，触发历史回看。
    const std::time_t now = timeline.front().end_time + 60;
    const auto decision = timeshiftx::PlaybackFacade::resolveProgrammePlayback(channels.front(), timeline.front(), now, {}, false);

    if (decision.mode != timeshiftx::PlaybackMode::CATCHUP) {
        return EXIT_FAILURE;
    }
    if (decision.url.find("playseek=") == std::string::npos) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
