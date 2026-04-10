#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string>

#include "chronosstream/epg/epg_manager.hpp"

int main() {
    // 6.3 边界1：时区换算 +0800 -> UTC 日期命中。
    const std::string xml_tz = R"XML(
<tv>
  <channel id="tz_ch"><display-name>CCTV-1 FHD</display-name></channel>
  <programme start="20260410080000 +0800" stop="20260410090000 +0800" channel="tz_ch">
    <title>TZ Program</title>
    <desc>timezone</desc>
  </programme>
</tv>
)XML";

    chronosstream::EPGManager epg_tz;
    if (!epg_tz.loadXMLTV(xml_tz).ok()) {
        return EXIT_FAILURE;
    }

    std::tm d {};
    d.tm_year = 2026 - 1900;
    d.tm_mon = 3;
    d.tm_mday = 10;
#if defined(_WIN32)
    const std::time_t target = _mkgmtime(&d);
#else
    const std::time_t target = timegm(&d);
#endif

    const auto tz_timeline = epg_tz.getTimelineForChannel("tz_ch", target);
    if (tz_timeline.size() != 1) {
        return EXIT_FAILURE;
    }

    // 边界2：模糊匹配归一化（去符号、清晰度词）。
    const std::string fuzzy_id = epg_tz.fuzzyMatchChannelName("CCTV1");
    if (fuzzy_id != "tz_ch") {
        return EXIT_FAILURE;
    }

    // 边界3：大 XML + 过滤策略，验证按需加载。
    std::ostringstream oss;
    oss << "<tv>\n";
    oss << "<channel id=\"keep\"><display-name>KEEP HD</display-name></channel>\n";
    oss << "<channel id=\"drop\"><display-name>DROP HD</display-name></channel>\n";
    oss << "<programme start=\"20260410000000 +0000\" stop=\"20260410010000 +0000\" channel=\"keep\"><title>K</title></programme>\n";
    for (int i = 0; i < 1000; ++i) {
        oss << "<programme start=\"20260410000000 +0000\" stop=\"20260410010000 +0000\" channel=\"drop\"><title>D</title></programme>\n";
    }
    oss << "</tv>\n";

    chronosstream::EPGManager epg_big;
    epg_big.setChannelFilter({"keep"});
    if (!epg_big.loadXMLTV(oss.str()).ok()) {
        return EXIT_FAILURE;
    }

    const auto keep_timeline = epg_big.getTimelineForChannel("keep", target);
    const auto drop_timeline = epg_big.getTimelineForChannel("drop", target);
    if (keep_timeline.empty()) {
        return EXIT_FAILURE;
    }
    if (!drop_timeline.empty()) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
