# Agent WebSocket 对接数据模型

本功能的数据类型属于 `ui/`，不进入标准 C++ core，也不序列化进流程 JSON。

## AgentConnectionSettings

用户级连接配置，由独立的 `AgentConnectionSettings` 读写：

| 字段 | 类型 | 约束 / 说明 |
|---|---|---|
| `webSocketUrl` | URL 字符串 | 必填；有效主机；scheme 仅 `ws` 或 `wss`。展示、错误及日志中需隐藏 user-info 和敏感查询值。 |
| `workingDirectory` | 本地路径 | 必填；连接前必须存在、可访问且为目录；发送给服务的是规范化绝对路径。 |
| `sessionId` | 字符串 | 可空。为空时新建会话；非空时恢复。由服务分配，用户可查看、复制、编辑或清除，并可选择持久化。 |

无 Agent 凭据字段。Agent 设置使用与 `ModelConnectionSettings` 不同的设置键空间，不调用模型凭据存储。

## AgentSession

一次 WebSocket 会话的内存状态：

| 字段 | 类型 | 说明 |
|---|---|---|
| `connectionState` | 枚举 | `Disconnected`、`Connecting`、`Starting`、`Resuming`、`Connected`、`Disconnecting`、`Failed`。 |
| `sessionId` | 字符串 | 服务确认后的活动 session ID；连接失败或断开后不得继续显示为活动会话 ID。 |
| `workingDirectory` | 路径 | 服务确认的新建会话实际使用的目录；恢复会话未由服务确认目录时显示“服务未返回”，不能冒称为当前实际目录。 |
| `turnState` | 枚举 | `Idle`、`Running`、`Aborting`、`Succeeded`、`Aborted`、`Failed`。新请求开始时从非活动状态转为 `Running`。 |
| `lastError` | 安全诊断 | 可呈现的错误分类和已脱敏文本；不得存储/记录完整请求内容或含凭据 URL。 |
| `events` | 有界事件序列 | 当前对话显示的回复片段、工具活动与状态；超出 UI 保留上限时丢弃最旧展示项，不影响协议状态。 |

WebSocket 建立不等于 Agent 会话建立：只有有效 `session` started/resumed 响应后才进入 `Connected`。网络错误、远端关闭或本地断开清理活动 session 和轮次状态。

## AgentTurn

一轮用户任务及其事件归属：

| 字段 | 类型 | 说明 |
|---|---|---|
| `prompt` | 短期内存文本 | trim 后不得为空；仅发送到当前 Agent，会话结束后不持久化或写日志。 |
| `state` | 枚举 | `Running`、`Aborting`、`Succeeded`、`Aborted`、`Failed`。 |
| `sawError` | 布尔值 | 当前轮次已收到 `error` 时设为真；不能被后续 `turn_complete` 清除。 |
| `events` | 有界序列 | `delta`、`message`、`tool_event`、状态、错误；带类型以便 UI 区分。 |

每会话最多一轮 `Running` / `Aborting`。只有未观察到错误且收到 `turn_complete` 才能到 `Succeeded`；收到错误、断连或超时到 `Failed`；收到 `status: aborted` 后为 `Aborted`。`turn_end` 仅收尾，不自行决定成功。

## AgentServerEvent

按协议解码的事件种类：

- `SessionStarted(sessionId, workingDirectory)` / `SessionResumed(sessionId)`
- `TurnStarted(sessionId)` / `TextDelta(text)` / `AssistantMessage(text)`
- `ToolActivity(event)`，以可读但有界方式展示，不用于本地执行
- `TurnCompleted` / `TurnEnded`
- `Aborted` / `Disconnected` / `Pong`
- `ServerError(message)`
- `ProtocolError(reason)`，用于 JSON 非对象、缺失必需字段、未知类型或字段类型错误

原始 WebSocket 帧和完整 `tool_event` 不持久化。无法识别的消息作为协议错误显式显示，不被转换成普通 Agent 回复。
