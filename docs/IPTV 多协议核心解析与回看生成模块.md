# IPTV 多协议核心解析与回看生成模块 (PRD & System Design)

## 1. 产品背景与概述 (Background & Overview)
在数字电视和流媒体领域，IPTV（交互式网络电视）播放器需要能够处理来自全球各地服务商提供的直播源数据。当前市面上的源格式主要分为以静态文本为主的 `M3U/M3U8` 和以动态接口为主的 `Xtream Codes API (JSON)`。

本项目的目标是：**使用 C/C++ 研发一套轻量级、高性能的 IPTV 核心中间件（Middleware）**。该中间件能够向下兼容多种直播源协议和 EPG（电子节目单）格式，向上为 UI 播放层提供统一的频道列表、时间轴匹配和历史回看 URL 生成服务。

## 2. 行业标准与核心术语 (Terminology)
* **M3U/M3U8**：一种多媒体播放列表的纯文本文件格式，IPTV 行业通过 `#EXTINF` 标签的扩展属性（如 `tvg-id`, `catchup`）来承载频道元数据。
* **XMLTV (e.xml)**：基于 XML 格式的电子节目单行业标准，包含各个频道的 ID 及对应的节目时间轴（Programme）和简介。
* **Xtream Codes API (XC API)**：IPTV 行业最广泛使用的面板系统接口。通过 HTTP GET 请求返回 JSON 数据，天然结构化，区分 Live（直播）、VOD（点播）、Series（剧集）和 Catchup（回看）。
* **Catch-up (回看/时移)**：流媒体服务器录制过去的直播流，允许客户端通过特定的时间戳参数拼接 URL，请求播放过去某一时段的视频流。

---

## 3. 总体功能需求 (Functional Requirements)

### 3.1 多协议直播源解析 (Source Parsing)
* **[F-01] M3U 本地/网络加载**：支持读取本地 `.m3u` 文件或通过 HTTP 下载解析，提取频道名、台标、分组（group-title）和原始流链接。
* **[F-02] M3U 扩展标签支持**：提取用于 EPG 匹配的 `tvg-id`、`tvg-name`，以及用于回看的 `catchup` 模式和 `catchup-source` 模板。
* **[F-03] Xtream Codes API 接入 (拓展)**：支持通过 Server URL、Username、Password 鉴权，请求 `get_live_streams` 接口，解析返回的 JSON 频道列表。

### 3.2 节目单解析与匹配 (EPG & Matching)
* **[F-04] XMLTV 快速解析**：高效解析体积庞大的 XML 文件，提取 `<channel>` 字典和 `<programme>` 时间轴。
* **[F-05] 智能模糊匹配算法**：当 M3U 直播源与 EPG 无法通过严格的 ID (`tvg-id`) 匹配时，系统应支持按 `tvg-name`、甚至剔除特殊字符后的频道名（如将 "CCTV-1 FHD" 归一化为 "CCTV1"）进行降级匹配。

### 3.3 回看链接引擎 (Catch-up Engine)
* **[F-06] 基于模板的 M3U 回看生成**：识别 `${(b)yyyyMMddHHmmss}` 等时间模板，结合用户选择的历史节目时间，动态生成回看 URL。
* **[F-07] 基于流 ID 的 XC API 回看生成**：根据 Xtream Codes 标准，按 `http://server:port/timeshift/user/pass/duration/YYYY-MM-DD:HH-MM/stream_id.ts` 格式生成链接。

---

## 4. 系统架构与核心模块设计 (Architecture)

系统采用模块化设计，屏蔽底层协议差异，向业务层暴露统一的数据结构。

### 4.1 技术栈与第三方库建议
* **语言标准**：C++11 / C++14 或以上
* **XML 解析**：`pugixml` (处理 EPG 极速且内存占用低)
* **JSON 解析**：`nlohmann/json` (用于处理 Xtream Codes API 的响应，语法优雅)
* **网络通信**：`libcurl` (用于下载 M3U、XML 以及请求 API)
* **正则与时间**：`<regex>`, `<chrono>`, `<iomanip>`

### 4.2 核心数据结构 (Unified Data Structures)

为了兼容 M3U 和 XC API，需要一个统一的 `Channel` 结构体：

```cpp
#include <string>
#include <vector>
#include <ctime>

// 统一的频道数据结构
struct Channel {
    std::string internal_id;     // 内部唯一ID
    std::string name;            // 频道显示名称
    std::string group_name;      // 分组名称 (如: "央视频道")
    std::string logo_url;        // 台标链接
    
    // 播放与源信息
    enum class SourceType { M3U, XTREAM_CODES };
    SourceType source_type;
    std::string live_url;        // 直播推流地址
    std::string xc_stream_id;    // XC 协议特有的流ID
    
    // EPG 与 回看元数据
    std::string epg_match_id;    // 用于去 XMLTV 中匹配的 ID (tvg-id)
    bool supports_catchup;       // 是否支持回看
    int catchup_days;            // 支持回看的天数 (XC协议提供)
    std::string catchup_type;    // M3U回看类型 (append, shift, flussonic等)
    std::string catchup_template;// M3U回看模板 (catchup-source)
};

// 节目单条目
struct Programme {
    std::string title;           // 节目名称
    std::string description;     // 节目简介
    std::time_t start_time;      // 开始时间 (Unix Timestamp)
    std::time_t end_time;        // 结束时间 (Unix Timestamp)
};
```

### 4.3 模块接口定义 (Module Interfaces)

#### 1. 解析策略接口 (`ISourceParser`)
定义一个基类接口，无论是 M3U 还是 XC，都实现该接口，输出统一的 `Channel` 列表。
```cpp
class ISourceParser {
public:
    virtual ~ISourceParser() = default;
    virtual bool parse(const std::string& data) = 0;
    virtual std::vector<Channel> getChannels() = 0;
};

class M3UParser : public ISourceParser { /* 实现 M3U 解析细节 */ };
class XtreamCodesParser : public ISourceParser { /* 实现 JSON 解析细节 */ };
```

#### 2. EPG 管理器 (`EPGManager`)
负责加载 XMLTV 数据，并与 `Channel` 列表进行绑定。
```cpp
class EPGManager {
public:
    bool loadXMLTV(const std::string& xml_content);
    // 为指定的频道获取当天的节目单
    std::vector<Programme> getTimelineForChannel(const std::string& epg_match_id, std::time_t target_date);
    // 频道名称降级匹配算法
    std::string fuzzyMatchChannelName(const std::string& raw_name);
};
```

#### 3. 回看 URL 引擎 (`CatchupEngine`)
**系统的核心算力所在**。根据频道的协议类型和用户的点播行为，计算最终播放地址。
```cpp
struct ServerCredentials {
    std::string server_url;
    std::string username;
    std::string password;
};

class CatchupEngine {
public:
    // 传入频道信息和目标节目，返回可播放的回看链接
    static std::string buildUrl(const Channel& channel, const Programme& target_prog, const ServerCredentials& creds = {}) {
        if (!channel.supports_catchup) return channel.live_url;

        if (channel.source_type == Channel::SourceType::M3U) {
            return _buildM3UCatchup(channel, target_prog);
        } else if (channel.source_type == Channel::SourceType::XTREAM_CODES) {
            return _buildXCCatchup(channel, target_prog, creds);
        }
        return channel.live_url;
    }

private:
    static std::string _buildM3UCatchup(const Channel& ch, const Programme& prog) {
        // 1. 提取 channel.catchup_template
        // 2. 将 prog.start_time 和 prog.end_time 转换为模板所需的字符串 (如 yyyyMMddHHmmss)
        // 3. 执行 std::regex_replace
        // 4. 判断 channel.catchup_type 是否为 append，返回 ch.live_url + replaced_template
        return ""; // Placeholder
    }

    static std::string _buildXCCatchup(const Channel& ch, const Programme& prog, const ServerCredentials& creds) {
        // XC 标准时移格式: http://server:port/timeshift/user/pass/时长(分钟)/开始时间(YYYY-MM-DD:HH-MM)/流ID.ts
        // 1. 计算 duration = (prog.end_time - prog.start_time) / 60
        // 2. 格式化 prog.start_time
        // 3. 拼接字符串并返回
        return ""; // Placeholder
    }
};
```

---

## 5. 关键业务流程 (Workflow)

以 **[用户点击昨天晚上8点的"CCTV1"新闻联播]** 为例，内部流转如下：
1. **UI 层发起请求**：获取到被点击的 `Programme` 对象和所属的 `Channel` 对象。
2. **逻辑层拦截**：判断该节目是否已经结束（`prog.end_time < 当前时间`）。
3. **调用 CatchupEngine**：
   * 引擎检查 `Channel.supports_catchup == true`。
   * 引擎检查其底层类型。发现是 M3U。
   * 读取 `catchup_template="?playseek=${(b)yyyyMMddHHmmss}-${(e)yyyyMMddHHmmss}"`。
   * 执行时间转换：将时间戳替换入模板。
4. **生成完成**：生成带有时间戳后缀的长 URL，交还给播放核心（如 FFmpeg / 你的播放器引擎）发起真正的网络流媒体请求。

---

## 6. 健壮性与边界条件考虑 (Robustness & Edge Cases)

1. **时区地狱 (Timezone Hell)**：
   * M3U 的服务器可能在欧洲，XMLTV 提供的时间带有 `+0800`，而设备本地可能是美东时间。
   * **设计要求**：系统内部必须一律将所有解析到的时间转换为 **UTC 零时区 Unix Timestamp (time_t)** 进行存储。只有在最后拼接 URL 模板或在 UI 展示时，才根据服务器要求或本地系统设置进行格式化。
2. **特殊字符与 URL 编码**：
   * M3U 的模板中如果包含非法字符，或者拼接时参数重复了 `?` 符号，需要编写一个轻量级的 URL Sanitizer 过滤多余的 `?` 和 `&`。
3. **超大 XMLTV 内存溢出**：
   * 聚合类 XMLTV 文件可能高达 100MB。使用 `pugixml` 时，建议采用按需加载或先过滤无用频道标签再解析的策略，避免占用过多内存。
4. **无缝回退机制**：
   * 当用户点击回看，但生成的 URL 报 404（服务器已清理该录像）时，中间件需捕获该网络错误，并向 UI 抛出特定的异常（如 `ERR_CATCHUP_EXPIRED`），避免播放器无限卡加载（Loading）。