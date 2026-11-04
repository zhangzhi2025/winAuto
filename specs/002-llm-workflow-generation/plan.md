# Implementation Plan: 大模型辅助创建自动化流程

**Branch**: `002-llm-workflow-generation` | **Date**: 2026-10-31 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/002-llm-workflow-generation/spec.md`

## Summary

在 WinAuto 桌面编辑器中加入自然语言生成流程草稿能力。用户配置 Claude 或 GPT 服务并提交任务后，服务端模型通过单一受限的 `propose_workflow` 工具返回符合现有工作流模型的提案；客户端解析并调用现有 `readWorkflow` / `validateWorkflow` 校验，随后由用户确认是否载入编辑器。模型工具调用只用于产生草稿，不会执行工具请求、桌面动作或脚本。第一阶段不包含 Excel 专用操作、定时触发和工作流自动运行。

按提供商分别适配 Claude Messages API 和 OpenAI Responses API，统一的是 WinAuto 流程提案及错误模型，而非强行统一各家的传输和消息协议。Qt 网络请求异步执行；用户凭据使用 Windows 保护存储，不进入提示词、日志或错误文本。

## Technical Context

**Language/Version**: C++17；Qt 5.15 Widgets/Network（现有 UI 使用 Qt 5.15）

**Primary Dependencies**: 当前 core 工作流模型、JSON 读写和 `validateWorkflow`；Qt Network / Qt JSON 用于异步 API 接入；Win32 DPAPI 用于保护用户 API 凭据

**Storage**: 用户级非敏感连接设置持久化；API 凭据单独加密存储；生成草稿仅保存在当前编辑会话，用户主动保存后才写入工作流 JSON

**Testing**: 项目现有 CMake/CTest 独立 C++ 测试；新增提供商响应解析、工具调用解码、错误映射、草稿校验和取消场景测试；使用 mock 网络响应，不依赖真实 API 凭据或网络

**Target Platform**: Windows 桌面应用；保留 core 的标准 C++17 和跨平台纯净性

**Project Type**: C++17 核心库 + CLI + Qt Widgets 桌面应用 + Win32 平台适配器

**Performance Goals**: 网络请求异步执行，不阻塞编辑器线程；首个结果在服务返回后立即呈现；对网络响应、生成时长及重试次数施加明确上限

**Constraints**: 只允许 HTTPS；默认验证 TLS 证书；提供商密钥不得打包进程序；不得在日志中记录完整密钥或完整提示/模型响应；模型工具仅能提出标准流程草稿，客户端本地校验仍为权威；限流重试必须有界且可取消

**Scale/Scope**: 首期支持 Claude 与 GPT 两类服务、一个用户发起的生成请求及一个工作流草稿；不提供多代理规划、后台调度、Excel 文件能力或模型直接执行桌面操作

## Constitution Check

仓库中的 `.specify/memory/constitution.md` 仍为未填写的模板，不含已批准的项目原则。按仓库现行架构约束检查：

- **Core purity**: 通过。HTTP、提供商协议、Qt、凭据存储不进入 `core/` 公共接口；现有工作流读写和校验是跨边界的唯一事实源。
- **安全边界**: 通过。模型输出只成为草稿；只有用户审阅并启动后才沿用现有信任摘要、权限检查与风险确认。
- **可测试性**: 通过。提供商传输和响应解析可注入 mock；校验逻辑沿用 core 测试。
- **敏感信息**: 通过。凭据使用 OS 保护存储，不写入流程文件或日志；提示词和响应只在生成期间处理。
- **范围控制**: 通过。首期仅覆盖可由现有步骤表示的桌面流程；Excel、定时计划、任意代码执行明确排除。

**Gate result**: PASS。没有需要额外架构复杂度豁免的已知事项。

## Project Structure

### Documentation (this feature)

```text
specs/002-llm-workflow-generation/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── contracts/
    └── workflow-generation-contract.md
```

### Source Code (repository root)

```text
core/
├── Json.h / Json.cpp                # 现有 Workflow JSON 读写
├── Model.h / Model.cpp              # 现有可生成的步骤模型
└── Validator.h / Validator.cpp      # 生成结果的语义校验权威

ui/
├── MainWindow.h / MainWindow.cpp    # 生成入口、草稿确认与载入编辑器
├── WorkflowGenerationDialog.*       # 用户任务、连接选择、状态和预览
├── WorkflowGeneration.*             # 提供商请求、响应解码和草稿转换
└── CMakeLists.txt                   # Qt Network 链接

platform_win/
└── ModelCredentialStore.*           # Windows 用户凭据保护存取（DPAPI）

tests/
├── test_workflow_generation.*       # 提案、响应解析、边界/失败场景
└── CMakeLists.txt
```

**Structure Decision**: 保持 `core/` 无网络和 Qt 依赖。首期的网络交互和提供商适配属于桌面 UI 功能，置于 `ui/WorkflowGeneration.*`；密钥保护是 Windows 专属职责，置于 `platform_win/`。通过构造注入隔离网络客户端、凭据存储和编辑器载入操作，便于测试失败场景而不访问真实服务。

## Complexity Tracking

无 Constitution Check 豁免项。
