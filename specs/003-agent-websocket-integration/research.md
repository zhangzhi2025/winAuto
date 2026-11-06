# Research: Agent WebSocket 对接

## 决策：直接兼容仓库参考桥接的 JSON WebSocket 协议

**Decision**: 客户端连接用户配置的 WebSocket 地址，发送 JSON 对象，按消息 `type` 分派。新建会话发送 `start`，已有 session ID 发送 `resume`；请求轮次使用 `chat`，控制消息使用 `abort`、`disconnect` 和 `ping`。按 [协议契约](contracts/agent-websocket-protocol.md) 严格校验入站消息。

**Rationale**: `docs/server/server.py` 已有明确的消息格式和 SDK 事件映射。复用该协议避免在 UI 侧引入第二个 Agent 服务协议，也不用将 Copilot SDK 引入桌面程序。

**Alternatives considered**:

- 在 WinAuto 内直接集成 Copilot SDK：服务端 SDK 生命周期、认证和运行时依赖会进入桌面客户端，且不符合用户要求的服务地址配置。
- 另建 REST 或通用 Agent 抽象：与现有服务不兼容，也会扩大首期范围。

## 决策：使用 Qt WebSockets 异步实现传输

**Decision**: 新增 Qt 5.15 WebSockets 模块依赖，使用 `QWebSocket` 的信号槽进行异步连接、发送和接收；为连接/握手和服务端确认操作设置定时器。UI 不进行阻塞等待。

**Rationale**: UI 已基于 Qt 5.15，并使用 Qt Network 管理异步网络请求。Qt WebSockets 提供同一事件循环中的原生客户端和本地测试服务器。它比手写 WebSocket 握手/帧解析更可靠，也避免平台专属 WinHTTP 代码。

**Alternatives considered**:

- 通过 WinHTTP 实现：将传输限制为 Windows 平台，并引入与现有 Qt UI 不同的异步模型。
- 自行实现 WebSocket 帧协议：重复实现协议细节，增加握手、分片、Ping/Pong 和 UTF-8 处理错误面。

**Build impact**: Qt WebSockets 是与 Widgets、Network 分开的组件。CMake 应显式探测并链接 `Qt5::WebSockets`，并保证找不到该组件不会影响 core、CLI 和非 Qt 测试；UI 目标在组件缺失时应明确跳过或给出配置提示。

## 决策：一个活动请求，以显式完成事件判定结果

**Decision**: 客户端状态机限制每个会话同时只有一个请求轮次。`turn_start` 开始轮次，`delta` / `message` / `tool_event` 是可展示事件，`turn_complete` 才是成功终态。`error`、连接断开、超时或协议错误均不能转换为成功；`turn_end` 只用于结束服务端事件流清理，不代表成功。

**Rationale**: 参考桥接拒绝并发 `chat`，且 SDK 回调中 `session.error` 后服务端仍可能发 `turn_complete`。客户端需保留错误优先级，不能因后续完成事件覆盖已观察到的轮次失败。中止后忽略旧轮次迟到事件，直到轮次终止或会话断开。

**Alternatives considered**:

- 把 WebSocket 保持连接等同于 Agent 会话就绪：握手成功并不保证 start/resume 成功。
- 把 `turn_end` 当成功：桥接无论异常都在 `finally` 发送它，会误报失败轮次成功。
- 并发发起多轮：服务明确禁止，且事件无法可靠归属到请求。

## 决策：连接配置独立保存，运行状态只保存在内存

**Decision**: 用新建的 Agent 设置组件读写独立的 QSettings 键，持久化 URL、工作目录和用户选择保存的 session ID。连接对象、当前请求和事件仅保存在会话窗口及其会话控制器中。保存和清除 Agent 设置不得访问模型设置键或 DPAPI 模型凭据。

**Rationale**: 用户需要跨次启动复用地址和路径，但 Agent 会话连接本身是短暂资源。明确分离存储可避免覆盖模型服务，也使关闭窗口时能显式断开会话。

## 决策：校验工作目录和 WebSocket URL，并限制敏感诊断

**Decision**: 在连接前解析 URL，仅接受 `ws` / `wss`，要求主机有效；对工作目录执行存在、目录类型和可访问性校验，发送规范化的绝对路径。界面展示与日志中的 URL 要隐藏 user-info、敏感查询参数和片段；日志不记录任务正文、完整服务事件或工具参数。

**Rationale**: URL 和事件数据可能包含访问令牌、工作区信息及用户任务。客户端必须在网络操作前拒绝无效设置，并保留足够诊断而不泄漏配置秘密。

**Alternatives considered**:

- 将原始 URL 或服务错误写入日志：服务地址可能含令牌，错误可回显请求 URL 或用户数据。
- 只用字符串前缀判断 URL：无法可靠识别 scheme、主机和编码后的 user-info。

## 权限兼容与验证状态

`docs/server/server.py` 将 `COPILOT_AUTO_APPROVE` 默认设为 `false`，仅显式设置为 `true` 时才使用 `PermissionHandler.approve_all`；`tests/test_agent_permissions.py` 通过 AST 回归检查默认关闭及调用受条件保护。桌面端不会发送或暗示权限批准。

**Release check**: 在实际部署的 Copilot SDK 环境中手动确认未设置 `COPILOT_AUTO_APPROVE` 时，服务仍按预期执行权限确认；若 SDK 默认行为会自动放行，必须在桥接中增加显式拒绝/确认回调。源码默认值和静态测试不足以证明运行时授权提示。

## 来源

- 仓库内参考实现：[docs/server/server.py](../../../docs/server/server.py)
- Qt 5.15 WebSockets 文档：<https://doc.qt.io/archives/qt-5.15/qwebsocket.html>
- Qt 5.15 WebSocket Server 文档：<https://doc.qt.io/archives/qt-5.15/qwebsocketserver.html>
