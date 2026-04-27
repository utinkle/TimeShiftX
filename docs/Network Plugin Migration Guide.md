# Network Plugin 开发与迁移指南

> 适用版本：Phase 7.6（已删除 `HttpClient` / `RequestQueue` 过渡层）

## 1. 插件接口规范（对齐当前代码）

### 1.1 核心类型
- `NetworkRequest`
  - `method`：`GET` / `HEAD`
  - `url`
  - `timeout_seconds`
  - `max_retries` / `retry_delay_ms`（由 `NetworkService` 统一执行重试）
  - `follow_redirect`
  - `strict_ssl`
  - `headers` / `user_agent`
- `NetworkResponse`
  - `error`
  - `http_status`
  - `body`
  - `headers`
  - `latency_ms`
- `NetworkCallback`
  - `std::function<void(NetworkResponse)>`

### 1.2 `INetworkPlugin` 约定
- 必须实现：
  - `name()`
  - `perform(request)`（同步单次请求）
- 可覆盖（推荐）：
  - `performAsync(request, callback)`
  - `performAsyncTracked(request, callback)`（返回 request_id）
  - `cancel(request_id)`

> 说明：插件推荐实现“单次请求语义”，重试逻辑由 `NetworkService` 统一执行。

---

## 2. 线程模型

### 2.1 Qt 插件
- 采用专用 `QThread` + 事件循环承载 `QNetworkAccessManager`。
- 插件可在非 UI 线程安全使用（请求通过队列投递到插件线程）。
- 回调在插件内部异步流程结束后触发。

### 2.2 libcurl 插件
- 采用 `curl_multi` + 独立 worker thread。
- 内部维护：
  - pending 请求队列
  - in-flight 映射
  - 最大并发限制
  - 可选取消集
- 回调在 worker 处理完成后触发。

---

## 3. 错误码映射对照（统一语义）

- `2xx` -> `ErrorCode::OK`
- `404` -> `ErrorCode::ERR_CATCHUP_EXPIRED`
- 其他 HTTP -> `ErrorCode::ERR_NETWORK_HTTP_STATUS`
- 传输层错误（连接失败、超时、被中断）-> `ErrorCode::ERR_NETWORK_TIMEOUT`
- 插件不可用/内部状态异常 -> `ErrorCode::ERR_INTERNAL`

> 业务层（parser/catchup/facade）应消费语义化 `ErrorCode`，不直接解析 HTTP 细节。

---

## 4. 构建与后端选择

### 4.1 目标拆分
- `timeshiftx_core`：核心库，不依赖 Qt/libcurl
- `timeshiftx_net_qt_plugin`：Qt Network 插件库
- `timeshiftx_net_libcurl_plugin`：libcurl 插件库
- `timeshiftx`：聚合目标，链接 `timeshiftx_core` + 当前选择的插件库

### 4.2 CMake 开关
- `TIMESHIFTX_NETWORK_BACKEND=qt|libcurl`
- `TIMESHIFTX_ENABLE_QT_PLUGIN`
- `TIMESHIFTX_ENABLE_LIBCURL_PLUGIN`

当前约束：单后端编译（Qt 与 libcurl 二选一）。

### 4.3 默认策略
- Windows/macOS（桌面）：默认 Qt
- Linux/服务器：默认 libcurl

---

## 5. 迁移指南（从旧调用迁移）

### 5.1 已移除内容
- `HttpClient`：已删除
- `RequestQueue`：已删除

### 5.2 推荐替换方式
- 同步：`NetworkService::request(...)` / `get(...)` / `head(...)`
- 回调异步：`NetworkService::requestAsync(...)` / `getAsync(...)` / `headAsync(...)`
- 需要取消：
  - 发送：`requestAsyncTracked(...)`（拿 request_id）
  - 取消：`NetworkService::cancel(request_id, ...)`

### 5.3 业务注入
- `M3UParser::parseFromUrl(..., INetworkPlugin* injected_plugin)`
- `XtreamCodesParser::parseFromApi(..., INetworkPlugin* injected_plugin)`
- `CatchupEngine::probeAvailability(..., INetworkPlugin* injected_plugin)`

测试时可注入 mock 插件，避免外网依赖。
