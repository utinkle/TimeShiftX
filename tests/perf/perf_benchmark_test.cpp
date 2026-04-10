#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

#include "chronosstream/epg/epg_manager.hpp"
#include "chronosstream/parser/m3u_parser.hpp"

int main() {
    using clock = std::chrono::steady_clock;

    // 6.4 性能压测（轻量版）：构造较大样本，测量解析耗时。
    constexpr int kChannelCount = 2000;
    constexpr int kProgrammeCount = 6000;

    std::ostringstream m3u;
    m3u << "#EXTM3U\n";
    for (int i = 0; i < kChannelCount; ++i) {
        m3u << "#EXTINF:-1 tvg-id=\"ch" << i << "\" tvg-name=\"CH" << i << "\" group-title=\"G\",CH" << i << "\n";
        m3u << "http://demo/live/" << i << ".m3u8\n";
    }

    chronosstream::M3UParser m3u_parser;
    auto t0 = clock::now();
    const auto m3u_rc = m3u_parser.parse(m3u.str());
    auto t1 = clock::now();
    if (!m3u_rc.ok()) {
        return EXIT_FAILURE;
    }

    std::ostringstream xml;
    xml << "<tv>\n";
    for (int i = 0; i < kChannelCount; ++i) {
        xml << "<channel id=\"ch" << i << "\"><display-name>CH" << i << "</display-name></channel>\n";
    }
    for (int i = 0; i < kProgrammeCount; ++i) {
        const int ch = i % kChannelCount;
        xml << "<programme start=\"20260410000000 +0000\" stop=\"20260410010000 +0000\" channel=\"ch"
            << ch << "\"><title>P" << i << "</title></programme>\n";
    }
    xml << "</tv>\n";

    chronosstream::EPGManager epg;
    auto t2 = clock::now();
    const auto epg_rc = epg.loadXMLTV(xml.str());
    auto t3 = clock::now();
    if (!epg_rc.ok()) {
        return EXIT_FAILURE;
    }

    const auto m3u_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    const auto epg_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

    std::cout << "[Perf] M3U channels=" << kChannelCount << ", elapsed_ms=" << m3u_ms << '\n';
    std::cout << "[Perf] XML programmes=" << kProgrammeCount << ", elapsed_ms=" << epg_ms << '\n';

    // 软阈值（避免 CI 抖动过严）：若异常慢则失败，作为性能退化告警。
    if (m3u_ms > 5000 || epg_ms > 8000) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
