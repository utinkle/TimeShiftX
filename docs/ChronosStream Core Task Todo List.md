# ChronosStream Core Task Todo List

## Phase 1 - 架构基线与数据模型落地
- [ ] 建立项目目录结构（core/parser/epg/catchup/net/utils/tests）。
- [ ] 定义统一数据模型：`Channel`、`Programme`、`ServerCredentials`、错误码枚举。
- [ ] 明确时间标准：统一使用 UTC `time_t` 存储，提供时区转换工具函数。
- [ ] 定义模块接口：`ISourceParser`、`EPGManager`、`CatchupEngine` 头文件与最小可编译骨架。
- [ ] 增加基础日志与错误传递约定（返回值 + 错误码/异常策略）。

## Phase 2 - 多协议源解析器实现（M3U/XC）
- [x] 实现 M3U 本地文本解析：识别 `#EXTINF` 与后续流地址行。
- [x] 支持 M3U 扩展属性提取：`tvg-id`、`tvg-name`、`group-title`、`tvg-logo`、`catchup`、`catchup-source`。
- [x] 实现网络加载器（libcurl）：支持 URL 下载 M3U 文本并交由解析器处理。
- [x] 实现 Xtream Codes `get_live_streams` JSON 解析并映射为统一 `Channel`。
- [x] 补齐解析异常处理：字段缺失、格式错误、空列表回退。

## Phase 3 - EPG 解析与频道匹配引擎
- [x] 实现 XMLTV `<channel>` 与 `<programme>` 解析（当前为轻量实现，后续可切换 `pugixml`）。
- [x] 构建 `epg_id -> programme timeline` 索引，支持按日期查询。
- [x] 实现严格匹配：优先 `tvg-id`/`epg_match_id` 直连。
- [x] 实现模糊匹配：频道名归一化（去符号、大小写、清理 FHD/HD 等后缀）后匹配。
- [x] 增加超大 XMLTV 场景优化（按需加载/预过滤策略验证）。

## Phase 4 - 回看 URL 生成引擎（核心）
- [x] 实现 M3U 模板引擎：支持 `${(b)...}`、`${(e)...}` 时间占位符替换。
- [x] 实现 XC 回看 URL 拼接：`timeshift/user/pass/duration/start/stream_id.ts`。
- [x] 增加 URL Sanitizer：处理重复 `?`、`&` 与非法字符编码。
- [x] 统一时长计算与边界检查（负时长、零时长、超回看窗口）。
- [x] 当不支持回看或构造失败时，提供直播 URL 回退策略。

## Phase 5 - 健壮性、错误治理与中间件封装
- [x] 定义并实现关键错误码（如 `ERR_CATCHUP_EXPIRED`、`ERR_EPG_NOT_FOUND`）。
- [x] 在网络请求层增加超时、重试与 HTTP 状态映射。
- [x] 增加回看可用性验证流程（可选 HEAD/探测）与失败上报。
- [x] 完成对 UI/播放器层的统一 Facade API（输入频道+节目，输出最终可播 URL）。
- [x] 编写示例流程：直播播放、历史节目点击、回看失败回退。

## Phase 6 - 测试、性能与交付
- [x] 单元测试：M3U/XC 解析、EPG 匹配、时间转换、URL 生成。
- [x] 集成测试：端到端验证“频道加载 -> 节目匹配 -> 回看生成”。
- [x] 边界测试：时区、跨天节目、乱码频道名、大文件 XMLTV。
- [x] 性能压测：大频道量与 100MB 级 EPG 的解析耗时与内存曲线。
- [x] 输出交付文档：接口说明、错误码清单、部署与运行示例。
