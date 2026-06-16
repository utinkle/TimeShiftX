#pragma once

#include <ctime>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "timeshiftx/errors.hpp"
#include "timeshiftx/types.hpp"

namespace timeshiftx {

class INetworkPlugin;

enum class EpgMatchMethod {
    None,
    StrictId,
    FuzzyEpgMatchId,
    FuzzyName,
};

struct EpgMatchResult {
    std::string epg_id;
    EpgMatchMethod method {EpgMatchMethod::None};
    int confidence {0};
    std::string matched_name;

    bool matched() const { return !epg_id.empty(); }
};

struct EpgTimelineStats {
    std::size_t channel_count {0};
    std::size_t programme_count {0};
    std::time_t first_start_time {0};
    std::time_t last_end_time {0};

    bool hasProgrammes() const { return programme_count > 0; }
};

// EPG 管理器：负责 XMLTV 解析、时间轴索引与严格匹配。
class EPGManager {
public:
    // 设置按需解析过滤器（3.5）：仅保留过滤集合内的频道节目，降低大 XMLTV 内存占用。
    void setChannelFilter(std::unordered_set<std::string> allowed_epg_ids);

    Error loadFromUrl(const std::string& url, long timeout_seconds = 10L, INetworkPlugin* injected_plugin = nullptr);

    // 加载 XMLTV 文本并建立索引（3.1）。
    Error loadXMLTV(const std::string& xml_content);

    // 获取指定 epg_id 在目标日期的节目单（3.2）。
    std::vector<Programme> getTimelineForChannel(const std::string& epg_match_id, std::time_t target_date) const;

    // 获取 EPG 全局节目时间范围，用于诊断节目清单是否覆盖当前日期。
    EpgTimelineStats getTimelineStats() const;

    // 获取单个 EPG channel 的节目数量和时间范围。
    EpgTimelineStats getTimelineStatsForChannel(const std::string& epg_match_id) const;

    // 严格匹配：仅按 channel.epg_match_id 与 XMLTV channel id 直连（3.3）。
    std::string resolveStrictEpgId(const Channel& channel) const;

    // 统一匹配入口：strict id -> epg_match_id 名称匹配 -> channel.name 名称匹配。
    EpgMatchResult resolveChannelEpgId(const Channel& channel) const;

    // 模糊匹配（3.4）：按归一化频道名映射到最可能的 epg_id。
    std::string fuzzyMatchChannelName(const std::string& raw_name) const;

private:
    // 将 XMLTV 时间格式（YYYYMMDDHHMMSS +/-ZZZZ）转换为 UTC time_t。
    static std::time_t parseXmltvTimeToUtc(const std::string& xmltv_time);

    // 提取标签内容（例如 <title>xxx</title>）。
    static std::string extractTagText(const std::string& block, const std::string& tag_name);

    // 频道名归一化：去符号、统一大写、清理 HD/FHD/UHD/4K 等噪音后缀。
    static std::string normalizeChannelName(const std::string& raw_name);

    EpgMatchResult fuzzyMatchChannelNameLocked(const std::string& raw_name, EpgMatchMethod method, int base_confidence) const;

private:
    mutable std::shared_mutex rw_mutex_;

    // 已存在的 EPG channel id 集合，用于快速判定严格匹配是否存在。
    std::unordered_set<std::string> channel_ids_;

    // EPG 时间轴：key=epg_channel_id，value=按开始时间排序的节目列表。
    std::unordered_map<std::string, std::vector<Programme>> timelines_;

    // channel 的 display-name 索引：用于名称匹配。
    std::unordered_map<std::string, std::vector<std::string>> channel_display_names_;

    // 归一化名称到 epg_id 的反向索引。一个名称可能对应多个 XMLTV channel，匹配时降低置信度。
    std::unordered_map<std::string, std::vector<std::string>> normalized_name_to_epg_ids_;

    // 3.5：按需过滤集合（空集合=不过滤）。
    std::unordered_set<std::string> filter_channel_ids_;

    EpgTimelineStats timeline_stats_;
};

} // namespace timeshiftx
