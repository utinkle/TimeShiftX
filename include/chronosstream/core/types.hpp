#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace chronosstream {

// 统一频道数据结构：用于屏蔽 M3U 与 Xtream Codes 的协议差异。
struct Channel {
    // 内部唯一标识：可由解析器生成（例如 hash 或拼接来源+stream_id）。
    std::string internal_id;

    // 面向 UI 展示的频道名称，例如 "CCTV-1 综合"。
    std::string name;

    // 分组名称，例如 "央视频道"、"体育"、"新闻"。
    std::string group_name;

    // 台标 URL：供上层 UI 显示。
    std::string logo_url;

    // 频道来源类型：统一处理多协议。
    enum class SourceType {
        M3U,
        XTREAM_CODES,
    };

    SourceType source_type {SourceType::M3U};

    // 直播原始 URL。
    std::string live_url;

    // Xtream Codes 特有字段：流 ID。
    std::string xc_stream_id;

    // EPG 匹配字段：优先使用 tvg-id / epg channel id。
    std::string epg_match_id;

    // 是否支持回看。
    bool supports_catchup {false};

    // 可回看天数（部分来源提供）。
    int catchup_days {0};

    // M3U 回看类型，例如 append / shift / flussonic。
    std::string catchup_type;

    // M3U 回看模板，例如 "?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}"。
    std::string catchup_template;
};

// 节目单条目：以 UTC 时间戳统一存储，避免多时区比较混乱。
struct Programme {
    std::string title;
    std::string description;

    // 开始/结束时间：统一使用 UTC Unix Timestamp（秒）。
    std::time_t start_time {0};
    std::time_t end_time {0};
};

// Xtream Codes 鉴权信息。
struct ServerCredentials {
    std::string server_url;
    std::string username;
    std::string password;
};

} // namespace chronosstream
