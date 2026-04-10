# ChronosStream Core - Phase 6 Delivery Guide

## 1. 对外接口速览（API）

### 1.1 解析层
- `M3UParser::parse(raw_data)`：解析 M3U 文本，输出统一 `Channel` 列表。
- `M3UParser::parseFromUrl(url)`：通过 `HttpClient` 下载并解析 M3U。
- `XtreamCodesParser::parse(raw_json)`：解析 `get_live_streams` JSON。
- `XtreamCodesParser::parseFromApi(server, user, pass)`：直接请求 Xtream API 并解析。

### 1.2 EPG 层
- `EPGManager::loadXMLTV(xml)`：加载 XMLTV 并建立索引。
- `EPGManager::getTimelineForChannel(epg_id, target_day)`：按 UTC 日期读取节目单。
- `EPGManager::resolveStrictEpgId(channel)`：严格匹配。
- `EPGManager::fuzzyMatchChannelName(name)`：模糊匹配（归一化后匹配）。
- `EPGManager::setChannelFilter(ids)`：大文件按需过滤。

### 1.3 回看与播放决策
- `CatchupEngine::buildUrl(channel, programme, creds)`：生成最终播放地址。
- `CatchupEngine::probeAvailability(url)`：HEAD 探测可用性。
- `PlaybackFacade::resolveProgrammePlayback(...)`：统一输出 LIVE/CATCHUP 决策。

---

## 2. 错误码清单（核心）

### 2.1 解析
- `ERR_PARSE_M3U_FAILED`
- `ERR_PARSE_XC_JSON_FAILED`
- `ERR_PARSE_XMLTV_FAILED`

### 2.2 EPG
- `ERR_EPG_NOT_FOUND`
- `ERR_EPG_TIMELINE_EMPTY`

### 2.3 回看
- `ERR_CATCHUP_NOT_SUPPORTED`
- `ERR_CATCHUP_TEMPLATE_INVALID`
- `ERR_CATCHUP_BUILD_FAILED`
- `ERR_CATCHUP_UNAVAILABLE`
- `ERR_CATCHUP_EXPIRED`

### 2.4 网络
- `ERR_NETWORK_TIMEOUT`
- `ERR_NETWORK_HTTP_STATUS`

---

## 3. 部署与运行示例

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/bin/chronosstream_demo
```

## 4. 性能压测入口

```bash
./build/bin/perf_benchmark_test
```

该测试会构造中大规模 M3U/XMLTV 样本并输出解析耗时，便于做版本间性能对比。
