#pragma once

#include <ctime>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "chronosstream/core/errors.hpp"
#include "chronosstream/core/types.hpp"

namespace chronosstream {

// EPG 管理器：负责 XMLTV 解析、时间轴索引与严格匹配。
class EPGManager {
public:
    // 设置按需解析过滤器（3.5）：仅保留过滤集合内的频道节目，降低大 XMLTV 内存占用。
    void setChannelFilter(std::unordered_set<std::string> allowed_epg_ids);

    // 加载 XMLTV 文本并建立索引（3.1）。
    Error loadXMLTV(const std::string& xml_content);

    // 获取指定 epg_id 在目标日期的节目单（3.2）。
    std::vector<Programme> getTimelineForChannel(const std::string& epg_match_id, std::time_t target_date) const;

    // 严格匹配：仅按 channel.epg_match_id 与 XMLTV channel id 直连（3.3）。
    std::string resolveStrictEpgId(const Channel& channel) const;

    // 模糊匹配（3.4）：按归一化频道名映射到最可能的 epg_id。
    std::string fuzzyMatchChannelName(const std::string& raw_name) const;

private:
    // 将 XMLTV 时间格式（YYYYMMDDHHMMSS +/-ZZZZ）转换为 UTC time_t。
    static std::time_t parseXmltvTimeToUtc(const std::string& xmltv_time);

    // 提取标签内容（例如 <title>xxx</title>）。
    static std::string extractTagText(const std::string& block, const std::string& tag_name);

    // 频道名归一化：去符号、统一大写、清理 HD/FHD/UHD/4K 等噪音后缀。
    static std::string normalizeChannelName(const std::string& raw_name);

private:
    mutable std::shared_mutex rw_mutex_;

    // 已存在的 EPG channel id 集合，用于快速判定严格匹配是否存在。
    std::unordered_set<std::string> channel_ids_;

    // EPG 时间轴：key=epg_channel_id，value=按开始时间排序的节目列表。
    std::unordered_map<std::string, std::vector<Programme>> timelines_;

    // channel 的 display-name 索引：用于名称匹配。
    std::unordered_map<std::string, std::vector<std::string>> channel_display_names_;

    // 归一化名称到 epg_id 的反向索引（首次出现优先）。
    std::unordered_map<std::string, std::string> normalized_name_to_epg_id_;

    // 3.5：按需过滤集合（空集合=不过滤）。
    std::unordered_set<std::string> filter_channel_ids_;
};

} // namespace chronosstream
