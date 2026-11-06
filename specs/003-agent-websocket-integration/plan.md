# Implementation Plan: Agent WebSocket 对接

**Branch**: `003-agent-websocket-integration` | **Date**: 2026-11-05 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/003-agent-websocket-integration/spec.md`

## Summary

在现有 Qt 流程编辑器中加入与大模型流程生成相互独立的 Agent 会话入口，按 `docs/server/server.py` 的 JSON-over-WebSocket 协议实现连接、新建/恢复会话、流式文本和工具活动展示、中止及断开。使用 Qt WebSockets 在 UI 线程异步管理连接与消息；连接配置独立保存，Agent 操作不触碰工作流编辑器或大模型设置。

客户端将未知或格式错误的事件作为协议错误处理，只有收到 `turn_complete` 才显示本轮成功。参考桥接的 `COPILOT_AUTO_APPROVE` 默认关闭，只有显式设置为 `true` 才会选择 `PermissionHandler.approve_all`；自动化回归测试防止重新引入无条件自动批准。仍需在部署所用的 Copilot SDK 环境中手动确认默认权限处理行为确实保留服务端授权门禁。

## Technical Context

**Language/Version**: C++17；Qt 5.15 Widgets、Network 和 WebSockets

**Primary Dependencies**: 当前 UI 和 Qt Network；新增 Qt WebSockets 的 `QWebSocket` / `QWebSocketServer`；`QSettings` 用于独立的用户级 Agent 设置

**Storage**: Agent WebSocket URL、工作目录和用户选择持久化的 session ID 存于独立的 `QSettings` 键；运行中的会话与事件仅保存在内存中

**Testing**: 项目现有 CMake/CTest；为 Agent 协议层与会话状态增加 Qt 测试，使用本机 `QWebSocketServer` 测试端覆盖新建、恢复、流式事件、中止、错误、超时、断开和无效消息；不连接真实 Agent 或 SDK

**Target Platform**: Windows Qt Widgets 桌面应用；core、CLI 与既有跨平台测试不依赖 Qt WebSockets

**Project Type**: C++17 核心库 + CLI + 可选 Qt Widgets 桌面应用 + Windows 平台适配器

**Performance Goals**: WebSocket 连接和收发不得阻塞 UI 线程；对服务消息尺寸、事件展示历史和连接/会话等待设定上限，持续流式更新时 UI 保持响应

**Constraints**: `core/` 保持标准 C++17 且无 Qt/Win32 依赖；只接受 `ws` / `wss`；校验并规范化本地工作目录；不记录完整任务、完整事件或 URL 凭据；不把 `turn_end`、未知事件、断开或错误解释为成功；新旧会话切换不得自动批准权限或影响流程编辑状态

**Scale/Scope**: 一个桌面端 Agent WebSocket 会话、每次至多一个活动请求轮次；只支持参考桥接的 start/resume/chat/abort/disconnect/ping 协议，不包含 Agent 服务托管、替换现有大模型流程生成、自动流程导入或流程执行

## Constitution Check

`.specify/memory/constitution.md` 仍是未填写模板，没有已批准的项目原则。按仓库架构及开发约束检查：

- **Core purity**: PASS — 传输、会话状态和展示全部放在 `ui/`，不改动 core 模型或平台边界。
- **异步与失败语义**: PASS — 用 Qt WebSockets 异步接收事件；错误、超时、取消和断连使用明确状态，不以断开或 `turn_end` 伪造完成。
- **敏感信息**: PASS — 不记录任务正文、完整服务事件或含凭据的原始 URL；Agent 设置与模型凭据/设置分离。
- **授权边界**: PASS WITH MANUAL VERIFICATION — 桥接默认不选择 `PermissionHandler.approve_all`，显式自动批准为 opt-in；发布前仍须按部署 SDK 手动确认默认权限流程。
- **测试**: PASS — 通过本地 WebSocket 测试服务覆盖协议与状态转移；既有 core purity 与大模型生成功能保持不变。

**Gate result**: 源码默认值与回归检查已满足 opt-in 要求；真实 SDK 权限提示流程仍需手动确认，未验证前不宣称端到端授权行为已验收。

## Project Structure

### Documentation (this feature)

```text
specs/003-agent-websocket-integration/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── contracts/
    └── agent-websocket-protocol.md
```

### Source Code (repository root)

```text
ui/
├── MainWindow.h / MainWindow.cpp             # 独立 Agent 会话入口
├── AgentConnectionSettings.*                 # QSettings；与模型配置键分离
├── AgentSession.*                             # QWebSocket、协议解析、会话/轮次状态
├── AgentSessionDialog.*                       # 设置、连接状态、聊天和工具事件展示
└── CMakeLists.txt                             # Qt5::WebSockets 链接

docs/server/
└── server.py                                  # 参考桥接：自动批准仅在显式环境变量 opt-in 时启用

tests/
├── test_agent_session.cpp                     # Qt WebSocket 测试服务及协议/状态测试
└── CMakeLists.txt                             # 按 Qt WebSockets 可用性注册测试

CMakeLists.txt                                 # 探测 Qt5 WebSockets；不影响 core/CLI 构建
README.md                                      # Agent 配置、协议依赖和手动验证说明
```

**Structure Decision**: Agent 对接是桌面会话功能，沿用 `ui/` 的 Qt 网络架构，但将其设置、协议与界面独立于 `WorkflowGeneration` 和 `ModelConnectionSettings`。不将服务消息或 Agent 任务加入工作流，也不新增 core 或平台接口。桥接默认关闭自动批准；运行时 SDK 权限提示仍需手动验收。

## Phase 0: Research

见 [research.md](research.md)。实现基于 `docs/server/server.py` 的消息协议，选择 Qt WebSockets 原生客户端；另记录服务端强制自动批准与 FR-014 的冲突。

## Phase 1: Design

- [Data model](data-model.md) 定义独立连接配置、会话状态、请求轮次及事件。
- [WebSocket contract](contracts/agent-websocket-protocol.md) 规定客户端请求、服务端事件、终态和错误处理。
- [Quickstart](quickstart.md) 说明 Qt WebSockets 构建依赖、手动会话验证及隔离验证。

## Constitution Check (Post-Design)

- **Core purity**: PASS — 所有实现留在 UI 和参考桥接层。
- **Session correctness**: PASS — 仅单请求在途；只有明确完成事件成功，断连时清理有效会话。
- **Privacy and work preservation**: PASS — URL 凭据脱敏，任务/事件不进入日志；连接过程不改动流程模型、草稿或大模型配置。
- **Permission handling**: PASS WITH MANUAL VERIFICATION — `AUTO_APPROVE` 默认为关闭且受环境变量显式控制；部署 SDK 的实际默认权限提示仍需手动验证。
- **Build compatibility**: PASS WITH DEPENDENCY — Qt WebSockets 是单独 Qt 模块；缺失时 UI/Agent 目标不可构建，但 core、CLI 和不依赖 Qt 的测试仍可构建。

## Complexity Tracking

无额外架构复杂度豁免。WebSocket 客户端与事件状态机是本功能必需边界；不新增通用 Agent 抽象、后台服务或对 core 的网络依赖。
