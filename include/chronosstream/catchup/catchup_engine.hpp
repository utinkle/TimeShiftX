#pragma once

#include <string>

#include "chronosstream/core/errors.hpp"
#include "chronosstream/core/types.hpp"

namespace chronosstream {

// 回看 URL 引擎：统一处理 M3U 模板回看与 Xtream Codes 回看拼接。
class CatchupEngine {
public:
    // 构建最终播放地址：根据频道类型自动分发到具体策略。
    static std::string buildUrl(const Channel& channel, const Programme& target_prog, const ServerCredentials& creds = {});

    // 5.3: 回看可用性探测（HEAD）。失败时返回可供 UI 识别的错误码。
    static Error probeAvailability(const std::string& url, long timeout_seconds = 5L, int max_retries = 1);

private:
    // M3U 回看构建：解析 ${(b)...}/${(e)...} 模板并拼接。
    static std::string buildM3UCatchup(const Channel& channel, const Programme& prog);

    // Xtream 回看构建：timeshift/user/pass/duration/start/stream_id.ts。
    static std::string buildXCCatchup(const Channel& channel, const Programme& prog, const ServerCredentials& creds);

    // 将 Java 风格时间模板转换为 strftime 模板后格式化。
    static std::string formatTimeWithTemplate(std::time_t ts, const std::string& java_like_fmt);

    // URL 清洗：清理重复 ?/&，并做最基础的空白编码。
    static std::string sanitizeUrl(const std::string& raw_url);
};

} // namespace chronosstream
