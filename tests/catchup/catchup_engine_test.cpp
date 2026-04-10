#include <cstdlib>
#include <ctime>
#include <string>

#include "chronosstream/catchup/catchup_engine.hpp"

int main() {
    // 4.1: 验证 M3U 模板替换与 URL 清洗。
    chronosstream::Channel m3u;
    m3u.source_type = chronosstream::Channel::SourceType::M3U;
    m3u.supports_catchup = true;
    m3u.live_url = "http://demo/live.m3u8";
    m3u.catchup_type = "append";
    m3u.catchup_template = "??playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}&&";

    chronosstream::Programme prog;
    prog.start_time = 1712707200; // 2024-04-10 00:00:00 UTC
    prog.end_time = 1712710800;   // 2024-04-10 01:00:00 UTC

    const std::string m3u_url = chronosstream::CatchupEngine::buildUrl(m3u, prog);
    if (m3u_url.find("20240410000000-20240410010000") == std::string::npos) {
        return EXIT_FAILURE;
    }
    if (m3u_url.find("&&") != std::string::npos || m3u_url.find("??") != std::string::npos) {
        return EXIT_FAILURE;
    }

    // flussonic 模式。
    chronosstream::Channel flus = m3u;
    flus.live_url = "http://server/live/cctv1/index.m3u8";
    flus.catchup_type = "flussonic";
    flus.catchup_template = "-${(b)}-${(duration)}";
    const std::string flus_url = chronosstream::CatchupEngine::buildUrl(flus, prog);
    if (flus_url.find("index-1712707200-3600.m3u8") == std::string::npos) {
        return EXIT_FAILURE;
    }

    // shift 模式。
    chronosstream::Channel shift = m3u;
    shift.catchup_type = "shift";
    shift.catchup_template = "?utc=${(b)}&lutc=${(e)}";
    const std::string shift_url = chronosstream::CatchupEngine::buildUrl(shift, prog);
    if (shift_url.find("utc=1712707200") == std::string::npos || shift_url.find("lutc=1712710800") == std::string::npos) {
        return EXIT_FAILURE;
    }

    // 4.2: 验证 Xtream 回看 URL 规则。
    chronosstream::Channel xc;
    xc.source_type = chronosstream::Channel::SourceType::XTREAM_CODES;
    xc.supports_catchup = true;
    xc.xc_stream_id = "1001";
    xc.live_url = "http://demo/live/1001.m3u8";

    chronosstream::ServerCredentials creds;
    creds.server_url = "http://server:8080/";
    creds.username = "user";
    creds.password = "pass";

    const std::string xc_url = chronosstream::CatchupEngine::buildUrl(xc, prog, creds);
    if (xc_url != "http://server:8080/timeshift/user/pass/60/2024-04-10:00-00/1001.ts") {
        return EXIT_FAILURE;
    }

    // 4.4: 非法时长（end<=start）应回退直播地址。
    chronosstream::Programme invalid_prog = prog;
    invalid_prog.end_time = invalid_prog.start_time;
    const std::string fallback_invalid = chronosstream::CatchupEngine::buildUrl(xc, invalid_prog, creds);
    if (fallback_invalid != xc.live_url) {
        return EXIT_FAILURE;
    }

    // 4.4: 超大时长应被裁剪（这里 catchup_days=1 -> 最大 1440 分钟）。
    xc.catchup_days = 1;
    chronosstream::Programme huge_prog {};
    huge_prog.start_time = std::time(nullptr) - 3600;              // 1小时前，确保在回看窗口内
    huge_prog.end_time = huge_prog.start_time + 7 * 24 * 60 * 60;  // 7天
    const std::string clamped = chronosstream::CatchupEngine::buildUrl(xc, huge_prog, creds);
    if (clamped.find("/1440/") == std::string::npos) {
        return EXIT_FAILURE;
    }

    // 4.5: 参数缺失或构造失败必须回退直播地址。
    chronosstream::ServerCredentials bad_creds;
    bad_creds.server_url = "http://server:8080";
    const std::string fallback_missing_creds = chronosstream::CatchupEngine::buildUrl(xc, prog, bad_creds);
    if (fallback_missing_creds != xc.live_url) {
        return EXIT_FAILURE;
    }

    // 5.3: 可用性探测参数校验。
    const chronosstream::Error probe_err = chronosstream::CatchupEngine::probeAvailability("");
    if (probe_err.code != chronosstream::ErrorCode::ERR_INVALID_ARGUMENT) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
