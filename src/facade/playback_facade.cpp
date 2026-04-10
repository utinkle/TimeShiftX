#include "chronosstream/facade/playback_facade.hpp"

namespace chronosstream {

PlaybackDecision PlaybackFacade::resolveLive(const Channel& channel) {
    PlaybackDecision d;
    d.url = channel.live_url;
    d.mode = PlaybackMode::LIVE;
    d.status = {ErrorCode::OK, "live"};
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
    return d;
}

} // namespace chronosstream
