#include "chronosstream/facade/playback_facade.hpp"

#include <future>

namespace chronosstream {

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
    // 5.5: 节目未结束，按直播处理。
    if (programme.end_time >= now_utc) {
        return resolveLive(channel);
    }

    const std::string catchup_url = CatchupEngine::buildUrl(channel, programme, creds);

    // 5.4: 回看构造失败则回退直播并上报错误。
    if (catchup_url.empty() || catchup_url == channel.live_url) {
        PlaybackDecision d = resolveLive(channel);
        if (channel.supports_catchup) {
            d.status = {ErrorCode::ERR_CATCHUP_BUILD_FAILED, "回看地址构造失败，已回退直播"};
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

} // namespace chronosstream
