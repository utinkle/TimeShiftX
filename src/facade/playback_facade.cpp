#include "timeshiftx/playback_facade.hpp"

#include <future>

namespace timeshiftx {

namespace {
void fillHttpHeaders(const Channel& channel, PlaybackDecision& d) {
    d.http_headers.clear();
    if (!channel.user_agent.empty()) d.http_headers["User-Agent"] = channel.user_agent;
    if (!channel.referer.empty()) d.http_headers["Referer"] = channel.referer;
}
} // namespace

PlaybackDecision PlaybackFacade::resolveLive(const Channel& channel) {
    PlaybackDecision d;
    d.url = channel.live_url;
    d.mode = PlaybackMode::LIVE;
    d.status = {ErrorCode::OK, "live"};
    fillHttpHeaders(channel, d);
    return d;
}

PlaybackDecision PlaybackFacade::resolveProgrammePlayback(const Channel& channel,
                                                          const Programme& programme,
                                                          std::time_t now_utc,
                                                          const ServerCredentials& creds,
                                                          bool probe_catchup_availability) {
    // 5.5: If the program has not ended, treat as live.
    if (programme.end_time >= now_utc) {
        return resolveLive(channel);
    }

    const std::string catchup_url = CatchupEngine::buildUrl(channel, programme, creds);

    // 5.4: If catchup construction fails, fall back to live and report error.
    if (catchup_url.empty() || catchup_url == channel.live_url) {
        PlaybackDecision d = resolveLive(channel);
        if (channel.supports_catchup) {
            d.status = {ErrorCode::ERR_CATCHUP_BUILD_FAILED, "Catchup address construction failed, fallen back to live"};
        }
        return d;
    }

    // 可选：执行可用性探测，失败时回退直播。
    if (probe_catchup_availability) {
        const Error probe = CatchupEngine::probeAvailability(catchup_url);
        if (!probe.ok()) {
            PlaybackDecision d = resolveLive(channel);
            d.status = probe;
            return d;
        }
    }

    PlaybackDecision d;
    d.url = catchup_url;
    d.mode = PlaybackMode::CATCHUP;
    d.status = {ErrorCode::OK, "catchup"};
    fillHttpHeaders(channel, d);
    return d;
}

std::future<PlaybackDecision> PlaybackFacade::resolveProgrammePlaybackAsync(const Channel& channel,
                                                                            const Programme& programme,
                                                                            std::time_t now_utc,
                                                                            const ServerCredentials& creds,
                                                                            bool probe_catchup_availability) {
    return std::async(std::launch::async, [channel, programme, now_utc, creds, probe_catchup_availability]() {
        return resolveProgrammePlayback(channel, programme, now_utc, creds, probe_catchup_availability);
    });
}

} // namespace timeshiftx
