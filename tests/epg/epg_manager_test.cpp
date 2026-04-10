#include <cstdlib>
#include <ctime>
#include <string>

#include "chronosstream/epg/epg_manager.hpp"

int main() {
    const std::string xmltv = R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<tv>
  <channel id="cctv1">
    <display-name>CCTV-1 FHD</display-name>
  </channel>
  <channel id="cctv2">
    <display-name>CCTV-2 HD</display-name>
  </channel>
  <programme start="20260410000000 +0000" stop="20260410010000 +0000" channel="cctv1">
    <title>新闻联播</title>
    <desc>晚间新闻节目</desc>
  </programme>
  <programme start="20260410230000 +0000" stop="20260411003000 +0000" channel="cctv1">
    <title>跨天节目</title>
    <desc>覆盖跨天场景</desc>
  </programme>
</tv>
)XML";

    chronosstream::EPGManager epg;
    // 3.5：验证按需过滤，仅保留 cctv1 节目。
    epg.setChannelFilter({"cctv1"});
    const auto rc = epg.loadXMLTV(xmltv);
    if (!rc.ok()) {
        return EXIT_FAILURE;
    }

    chronosstream::Channel ch;
    ch.epg_match_id = "cctv1";

    // 3.3 严格匹配：epg_match_id 命中时应直接返回。
    const std::string strict_id = epg.resolveStrictEpgId(ch);
    if (strict_id != "cctv1") {
        return EXIT_FAILURE;
    }

    // 3.4 模糊匹配：CCTV-1 FHD / CCTV1 应归一化到同一个 epg_id。
    const std::string fuzzy_id = epg.fuzzyMatchChannelName("CCTV1");
    if (fuzzy_id != "cctv1") {
        return EXIT_FAILURE;
    }

    // 过滤器应生效：cctv2 不应有节目时间轴。
    const auto cctv2_timeline = epg.getTimelineForChannel("cctv2", std::time(nullptr));
    if (!cctv2_timeline.empty()) {
        return EXIT_FAILURE;
    }

    // 以 2026-04-10 UTC 查询，当天应包含两个节目（第二个节目跨天但与当天有交集）。
    std::tm day_tm {};
    day_tm.tm_year = 2026 - 1900;
    day_tm.tm_mon = 3;
    day_tm.tm_mday = 10;
    day_tm.tm_hour = 12;
    day_tm.tm_min = 0;
    day_tm.tm_sec = 0;
#if defined(_WIN32)
    const std::time_t target = _mkgmtime(&day_tm);
#else
    const std::time_t target = timegm(&day_tm);
#endif

    const auto timeline = epg.getTimelineForChannel("cctv1", target);
    if (timeline.size() != 2) {
        return EXIT_FAILURE;
    }

    if (timeline[0].title != "新闻联播") {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
