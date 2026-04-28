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

PlaybackDecision PlaybackFacade::resolveProgrammePlayback(const Channel& channel, const Programme& programme, const ServerCredentials& creds, bool probe_catchup_availability) {
    const std::string catchup_url = CatchupEngine::buildUrl(channel, programme, creds);

    if (catchup_url.empty() || catchup_url == channel.live_url) {
        PlaybackDecision d = resolveLive(channel);
        if (channel.catchup_declared) {
            d.status = {ErrorCode::ERR_CATCHUP_BUILD_FAILED, "Catchup address construction failed, fallen back to live"};
        }
        return d;
    }

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

std::future<PlaybackDecision> PlaybackFacade::resolveProgrammePlaybackAsync(const Channel& channel, const Programme& programme, const ServerCredentials& creds, bool probe_catchup_availability) {
    return std::async(std::launch::async, [channel, programme, creds, probe_catchup_availability]() {
        return resolveProgrammePlayback(channel, programme, creds, probe_catchup_availability);
    });
}

} // namespace timeshiftx
