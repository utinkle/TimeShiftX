# Phase 7 - 网络层插件化重构 TODO（编码前设计稿）

> 目标：在**不破坏现有解析与回看业务能力**前提下，完成“核心去 libcurl 依赖 + 网络调用插件化 + Qt 异步插件 + libcurl 异步插件”四项改造。

## 0. 基于当前代码的现状结论（作为重构边界）

- 当前核心网络入口是 `HttpClient` 静态类，`get/head/getAsync/headAsync` 直接被业务层调用。
- `HttpClient` 的同步实现直接绑定 `libcurl`，并在内部做重试、HTTP 状态码映射。
- `RequestQueue` 目前把异步包装为“线程池 + 调用同步 `HttpClient::get/head`”。
- 业务层直接依赖网络调用的点有：
  - `M3UParser::parseFromUrl`（拉取 M3U）
  - `XtreamCodesParser::parseFromApi`（拉取 JSON）
  - `CatchupEngine::probeAvailability`（HEAD 探测）
- 构建系统直接拉取并链接 `libcurl`（FetchContent + `target_link_libraries(timeshiftx PUBLIC libcurl)`）。

---

## 1. 目标架构定义（先定标准，再改代码）

### 1.1 抽象统一网络契约（插件 API）
- [ ] 新增协议层接口（建议：`INetworkPlugin`）：
  - [ ] `get(request)` / `head(request)` 同步接口（可选，兼容现有流程）。
  - [ ] `getAsync(request, callback)` / `headAsync(request, callback)` 异步接口（主推荐）。
  - [ ] 支持超时、重试、headers、user-agent、follow-redirect 等请求参数。
  - [ ] 统一返回结构 `NetworkResponse { Error, http_status, body, headers, timing }`。
- [ ] 增加 `NetworkRequest` 数据模型，避免参数散落函数签名中。
- [ ] 在接口中明确线程语义：
  - [ ] callback 在哪个线程回调（插件线程 / 调用线程 / 指定 dispatcher）。
  - [ ] 生命周期约束（插件销毁时如何取消请求）。

### 1.2 插件发现与管理机制
- [ ] 新增 `NetworkPluginManager`：
  - [ ] 注册/注销插件实例（静态注册优先，动态加载可后置）。
  - [ ] 按名称选择插件（`qt` / `libcurl`）。
  - [ ] 支持“首选插件 + 回退插件”策略（例如 Qt 失败回退 libcurl）。
- [ ] 配置入口统一：
  - [ ] CMake 选项：`TIMESHIFTX_NET_PLUGIN_QT`、`TIMESHIFTX_NET_PLUGIN_LIBCURL`。
  - [ ] 运行时配置：环境变量或 facade 参数（例如 `TIMESHIFTX_NET_BACKEND=qt`）。

### 1.3 核心层去 libcurl 依赖的完成标准
- [ ] `include/timeshiftx` 下核心头文件不再出现 libcurl 术语。
- [ ] `src/parser`、`src/catchup`、`src/facade` 不包含任何 `<curl/...>` 或 `HttpClient` 静态硬编码。
- [ ] 核心库 target 不直接 `PUBLIC` 链接 libcurl，改为插件 target 自己链接。

---

## 2. 分阶段改造清单（建议顺序）

## Phase 7.1 - 建立抽象层并完成兼容适配
- [ ] 新增 `include/timeshiftx/network_types.hpp`：定义 `NetworkRequest/NetworkResponse`。
- [ ] 新增 `include/timeshiftx/inetwork_plugin.hpp`：定义插件接口。
- [ ] 新增 `include/timeshiftx/network_service.hpp` + `src/net/network_service.cpp`：
  - [ ] 对业务暴露统一网络入口（替代 `HttpClient` 静态调用）。
  - [ ] 内部委托 `NetworkPluginManager`。
- [ ] 保留 `HttpClient` 作为临时 compatibility facade（deprecated）：
  - [ ] 内部改为调用 `NetworkService`。
  - [ ] 仅用于减小一次性改动范围，最终删除。

## Phase 7.2 - 业务调用点去耦（核心改造）
- [ ] `M3UParser::parseFromUrl` 改为依赖注入 `NetworkService/INetworkPlugin`。
- [ ] `XtreamCodesParser::parseFromApi` 改为依赖注入网络抽象。
- [ ] `CatchupEngine::probeAvailability` 改为依赖注入网络抽象。
- [ ] 消除静态硬依赖路径：避免业务代码直接 `#include "timeshiftx/http_client.hpp"`。
- [ ] 校正错误码映射归属：
  - [ ] `HTTP->ErrorCode` 映射应在统一策略层（或插件层可覆盖），不是散落在业务层。

## Phase 7.3 - Qt 异步网络插件（主推实现）
- [ ] 新增插件模块 `plugins/network_qt/`。
- [ ] 实现 `QtNetworkPlugin`（基于 `QNetworkAccessManager`）：
  - [ ] GET/HEAD 异步请求。
  - [ ] 超时控制（`QTimer` 或 reply timeout 机制）。
  - [ ] 自动重试策略（可配置次数 + 间隔）。
  - [ ] 状态码、错误码、响应体映射到统一 `NetworkResponse`。
  - [ ] SSL/证书错误策略（严格/宽松模式可配置）。
- [ ] 明确事件循环约束：
  - [ ] 若在非 Qt UI 线程使用，需提供专用 `QThread` + event loop 承载网络对象。
- [ ] 编写 Qt 插件单测（mock server / QSignalSpy）：
  - [ ] 成功、404、超时、重试成功、重试失败、取消请求。

## Phase 7.4 - libcurl 异步网络插件（兼容实现）
- [ ] 新增插件模块 `plugins/network_libcurl/`。
- [ ] 实现 `LibcurlNetworkPlugin`：
  - [ ] 推荐使用 `curl_multi` 实现异步，而非“线程池包同步 curl_easy”。
  - [ ] 统一请求队列、并发限制、取消机制。
  - [ ] 对齐 Qt 插件的错误码语义与状态码映射。
- [ ] 如果短期先落地 MVP：
  - [ ] 可保留当前 `RequestQueue` 线程池方案作为过渡，但要封装在插件内部，不得泄漏到核心层。
- [ ] 完成 libcurl 插件单测与压力测试。

## Phase 7.5 - 构建系统拆分与依赖收口
- [ ] CMake 将核心库与插件库拆 target：
  - [ ] `timeshiftx_core`：不依赖 Qt/libcurl。
  - [ ] `timeshiftx_net_qt_plugin`：依赖 Qt Network。
  - [ ] `timeshiftx_net_libcurl_plugin`：依赖 libcurl。
- [ ] 清理 `timeshiftx` 目标中的 `PUBLIC libcurl`。
- [ ] 增加插件开关与默认策略（例如桌面端默认 Qt，服务器端默认 libcurl）。

## Phase 7.6 - 删除过渡层与文档收尾
- [ ] 删除 `HttpClient` 旧接口与 `RequestQueue`（若已被插件内部替代）。
- [ ] 更新示例代码 `example/main.cpp`，演示插件选择与注入。
- [ ] 更新开发文档：插件接口规范、线程模型、错误码对照表、迁移指南。

---

## 3. 关键设计决策（需在编码前拍板）

- [ ] 插件边界：是“编译期静态注册”还是“运行时动态加载（.so/.dll）”？
- [ ] 回调模型：future / callback / Qt signal 三者如何统一？
- [ ] 取消语义：是否提供 request id + cancel API？
- [ ] 超时/重试策略归属：核心统一还是插件自定义并上报能力。
- [ ] 错误码映射是否保持现有 `ERR_CATCHUP_EXPIRED` 语义不变。
- [ ] 线程安全：插件实例是否多线程共享，是否要求可重入。

---

## 4. 测试与验收 TODO（必须和重构并行）

### 4.1 回归测试（业务不退化）
- [ ] M3U URL 拉取解析仍可用。
- [ ] Xtream API 拉取解析仍可用。
- [ ] Catchup HEAD 探测错误语义不变（404 -> `ERR_CATCHUP_EXPIRED`）。

### 4.2 插件一致性测试（同一输入同一语义）
- [ ] 对 Qt 与 libcurl 插件执行同一组 contract tests：
  - [ ] 2xx、3xx、4xx、5xx。
  - [ ] DNS 失败、连接超时、读超时。
  - [ ] 重定向链、空 body、大 body。
- [ ] 校验两插件输出 `ErrorCode` 与 message 模式一致（允许 message 文本差异，禁止语义差异）。

### 4.3 性能与资源
- [ ] 并发 100/500 请求下吞吐与延迟。
- [ ] 长时间运行（30min+）内存泄漏检查。
- [ ] 请求取消、插件卸载时无悬挂线程。

### 4.4 CI/CD
- [ ] 增加矩阵构建：`CORE_ONLY`、`CORE+QT_PLUGIN`、`CORE+LIBCURL_PLUGIN`、`ALL`。
- [ ] 将插件 contract tests 纳入必过门禁。

---

## 5. 建议任务拆分（可直接建 issue）

- [ ] Task A: 定义 `NetworkRequest/Response` 与 `INetworkPlugin`。
- [ ] Task B: 实现 `NetworkPluginManager + NetworkService`。
- [ ] Task C: 将 `M3UParser/XtreamCodesParser/CatchupEngine` 改为依赖网络抽象。
- [ ] Task D: 实现 Qt 异步插件 + 测试。
- [ ] Task E: 实现 libcurl 异步插件 + 测试。
- [ ] Task F: CMake 拆分与依赖收口。
- [ ] Task G: 删除过渡层 + 文档迁移说明。

---

## 6. 风险清单与缓解

- [ ] 风险：Qt 插件依赖 event loop，接入纯核心库场景可能困难。
  - [ ] 缓解：插件内自建线程与 event loop，并提供生命周期管理器。
- [ ] 风险：两套插件错误语义不一致导致上层逻辑分叉。
  - [ ] 缓解：先写 contract tests，再实现插件。
- [ ] 风险：一次性移除 `HttpClient` 影响面过大。
  - [ ] 缓解：保留一版 deprecated 适配层，分两次删除。
- [ ] 风险：CMake 依赖拆分导致示例/测试链接失败。
  - [ ] 缓解：先完成 target 拆分，再逐个迁移测试目标。

---

## 7. Definition of Done（完成判定）

- [ ] 核心层（parser/catchup/facade）不再直接包含或调用 libcurl。
- [ ] 所有网络调用均通过插件接口进入。
- [ ] Qt 异步插件可作为默认后端稳定运行。
- [ ] libcurl 异步插件可选启用且通过同一 contract tests。
- [ ] 现有业务回归测试与新增插件测试全部通过。
- [ ] 文档明确插件选择、线程模型、错误码语义与迁移方法。
