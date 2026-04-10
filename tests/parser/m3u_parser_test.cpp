#include <cstdlib>
#include <string>

#include "chronosstream/parser/m3u_parser.hpp"

int main() {
    const std::string sample =
        "#EXTM3U\n"
        "#EXTINF:-1 tvg-id=\"cctv1\" tvg-name=\"CCTV1\" group-title=\"央视频道\" tvg-logo=\"http://logo.png\" catchup=\"append\" catchup-source=\"?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}\",CCTV-1 综合\n"
        "http://demo/live/cctv1.m3u8\n";

    chronosstream::M3UParser parser;
    const chronosstream::Error rc = parser.parse(sample);
    if (!rc.ok()) {
        return EXIT_FAILURE;
    }

    const auto channels = parser.getChannels();
    if (channels.size() != 1) {
        return EXIT_FAILURE;
    }

    const auto& ch = channels.front();
    if (ch.name != "CCTV-1 综合") return EXIT_FAILURE;
    if (ch.epg_match_id != "cctv1") return EXIT_FAILURE;
    if (ch.group_name != "央视频道") return EXIT_FAILURE;
    if (ch.logo_url != "http://logo.png") return EXIT_FAILURE;
    if (!ch.supports_catchup) return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
