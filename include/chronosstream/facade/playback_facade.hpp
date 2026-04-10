#pragma once

#include <ctime>
#include <string>

#include "chronosstream/catchup/catchup_engine.hpp"
#include "chronosstream/core/errors.hpp"
#include "chronosstream/core/types.hpp"

namespace chronosstream {

// 播放模式：直播 or 回看。
enum class PlaybackMode {
    LIVE,
    CATCHUP,
};

// Facade 输出：向 UI/播放器层提供统一播放决策结果。
struct PlaybackDecision {
    std::string url;
    PlaybackMode mode {PlaybackMode::LIVE};
    Error status {ErrorCode::OK, "ok"};
};

// 统一播放入口：封装直播直出、历史节目回看生成、可用性探测与失败回退。
class PlaybackFacade {
public:
    // 直播播放：直接返回 live_url。
    static PlaybackDecision resolveLive(const Channel& channel);

    // 历史/当前节目播放决策：
    // - 节目未结束 => 直播
    // - 节目已结束 => 尝试回看
    // - 回看构造/探测失败 => 回退直播并返回错误状态
    static PlaybackDecision resolveProgrammePlayback(const Channel& channel,
                                                     const Programme& programme,
                                                     std::time_t now_utc,
                                                     const ServerCredentials& creds = {},
                                                     bool probe_catchup_availability = true);
};

} // namespace chronosstream
