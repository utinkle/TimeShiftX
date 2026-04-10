#include <cstdlib>
#include <string>

#include "chronosstream/parser/xtream_codes_parser.hpp"

int main() {
    // 包含 1 条有效数据 + 2 条无效数据，验证 2.5 的“容错跳过 + 保留有效数据”策略。
    const std::string json_payload = R"JSON(
[
  {
    "stream_id": 1001,
    "name": "CCTV1",
    "category_name": "央视频道",
    "stream_icon": "http://logo/cctv1.png",
    "epg_channel_id": "cctv1",
    "tv_archive": 1,
    "tv_archive_duration": "3"
  },
  {
    "name": "missing_stream_id"
  },
  {
    "stream_id": 1003
  }
]
)JSON";

    chronosstream::XtreamCodesParser parser;
    const chronosstream::Error rc = parser.parse(json_payload);
    if (!rc.ok()) {
        return EXIT_FAILURE;
    }

    const auto channels = parser.getChannels();
    if (channels.size() != 1) {
        return EXIT_FAILURE;
    }

    const auto& ch = channels.front();
    if (ch.xc_stream_id != "1001") return EXIT_FAILURE;
    if (ch.name != "CCTV1") return EXIT_FAILURE;
    if (ch.epg_match_id != "cctv1") return EXIT_FAILURE;
    if (!ch.supports_catchup) return EXIT_FAILURE;
    if (ch.catchup_days != 3) return EXIT_FAILURE;

    // 空列表/全无效列表应返回错误。
    const std::string invalid_payload = R"JSON([{"name":"only_name"}])JSON";
    const chronosstream::Error rc_invalid = parser.parse(invalid_payload);
    if (rc_invalid.ok()) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
