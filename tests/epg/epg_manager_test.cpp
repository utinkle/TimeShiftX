#include <ctime>
#include <string>

#include "gtest/gtest.h"
#include "timeshiftx/epg_manager.hpp"

TEST(EPGManagerTest, LoadXmlAndTimelineSuccess) {
    const std::string xmltv = R"XML(
<tv>
  <channel id="cctv1"><display-name>CCTV-1 FHD</display-name></channel>
  <programme start="20260410080000 +0800" stop="20260410090000 +0800" channel="cctv1">
    <title>新闻联播</title><desc>desc</desc>
  </programme>
</tv>
)XML";

    timeshiftx::EPGManager epg;
    const auto rc = epg.loadXMLTV(xmltv);
    ASSERT_TRUE(rc.ok());

    std::tm d {};
    d.tm_year = 2026 - 1900;
    d.tm_mon = 3;
    d.tm_mday = 10;
#if defined(_WIN32)
    std::time_t target = _mkgmtime(&d);
#else
    std::time_t target = timegm(&d);
#endif

    const auto timeline = epg.getTimelineForChannel("cctv1", target);
    ASSERT_EQ(timeline.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(timeline[0].title, "新闻联播");
    EXPECT_EQ(epg.fuzzyMatchChannelName("CCTV1"), "cctv1");
}

TEST(EPGManagerTest, InvalidXmlShouldFail) {
    timeshiftx::EPGManager epg;
    const auto rc = epg.loadXMLTV("<bad>");
    EXPECT_FALSE(rc.ok());
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return testing::RUN_ALL_TESTS();
}
