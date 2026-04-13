#include <string>

#include "gtest/gtest.h"
#include "timeshiftx/m3u_parser.hpp"

TEST(M3UParserTest, ParseWithMixedAttributeQuotes) {
    const std::string sample =
        "#EXTM3U\n"
        "#EXTVLCOPT:http-user-agent=AptvPlayer-UA\n"
        "#EXTVLCOPT:http-referrer=https://example.com\n"
        "#EXTINF:-1 tvg-id='cctv1' tvg-name=CN1 group-title=\"央视频道\" http-user-agent='Inline-UA' catchup='append' catchup-source=?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss},CCTV-1\n"
        "http://demo/live/cctv1.m3u8\n";

    timeshiftx::M3UParser parser;
    const auto rc = parser.parse(sample);
    ASSERT_TRUE(rc.ok());

    const auto channels = parser.getChannels();
    ASSERT_EQ(channels.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(channels[0].epg_match_id, "cctv1");
    EXPECT_EQ(channels[0].group_name, "央视频道");
    EXPECT_TRUE(channels[0].supports_catchup);
    EXPECT_EQ(channels[0].user_agent, "Inline-UA");
    EXPECT_EQ(channels[0].referer, "https://example.com");
}

TEST(M3UParserTest, ParseInvalidInputShouldFail) {
    timeshiftx::M3UParser parser;
    const auto rc = parser.parse("#EXTM3U\n# just comments only\n");
    EXPECT_FALSE(rc.ok());
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return testing::RUN_ALL_TESTS();
}
