# Research: 大模型辅助创建自动化流程

## 决策：工具调用只提交流程提案

**Decision**: 为模型提供一个受限的 `propose_workflow` 工具。它的输入是工作流元数据、步骤和“ready / needs_clarification / unsupported”状态。客户端仅解析提案，不把其他工具请求转发给 RPA 执行器，也不执行模型工具调用；不完整任务通过澄清状态和缺少的信息返回。

**Rationale**: 用户目标是生成 WinAuto 标准流程，而不是让模型在生成过程中直接操作桌面。工具调用在 Claude 和 OpenAI API 中都是模型要求客户端处理某个工具的结构化响应；只有应用主动调用工具处理程序时才会产生本地副作用。应用只需接收提案并走现有流程校验。

**Alternatives considered**:

- 每一步都由模型发出动作调用并立即操作桌面：违反用户审核流程要求，也会让模型生成和 RPA 执行授权混在一起。
- 仅要求普通文本 JSON：可实现，但不符合结构化 Function Calling 需求，且更依赖自由文本解码。
- 要求模型执行多轮桌面工具闭环：首期不需要，并扩大权限与失败面。

Sources: [Anthropic Tool use overview](https://docs.anthropic.com/en/docs/agents-and-tools/tool-use/overview), [Anthropic Messages API](https://docs.anthropic.com/en/api/messages), [OpenAI Function calling](https://platform.openai.com/docs/guides/function-calling), [OpenAI Responses API](https://platform.openai.com/docs/api-reference/responses/create).

## 决策：统一工作流草稿，不统一提供商传输协议

**Decision**: 分别实现 Claude Messages API 和 OpenAI Responses API 的请求/响应适配器；适配器输出同一个内部提案结构。HTTP headers、端点、消息格式、工具调用封装和错误响应按提供商处理。

**Rationale**: 两种 API 的请求封装和返回事件/内容结构不同。统一业务结果可复用 WinAuto 校验和草稿流程；试图统一底层 API 容易形成大量提供商分支，不会消除模型能力差异。

**Alternatives considered**:

- 单一通用协议客户端：不匹配两家各自的 API envelope，也难保证工具能力一致。
- 第一阶段仅支持 Claude：更小，但规格和需求方案明确提出 Claude 与 GPT 等多模型服务。
- 引入第三方聚合服务：增加额外服务商依赖、数据路径和凭据管理，不是首期必需。

Sources: [Anthropic Messages API](https://docs.anthropic.com/en/api/messages), [OpenAI Responses API](https://platform.openai.com/docs/api-reference/responses/create).

## 决策：工具/JSON Schema 是格式约束，core 校验才决定流程有效性

**Decision**: 在提供商支持时使用结构化工具参数或严格 schema 模式；无论响应是否符合模型端 schema，都必须在本地解码成现有 workflow JSON，并通过 `readWorkflow` 与 `validateWorkflow` 后才允许呈现为有效草稿。拒绝截断、不完整、未知字段和语义无效的结果。

**Rationale**: 提供商对 JSON Schema 子集与严格模式的支持不同，且模型生成仍可能遗漏上下文语义（例如窗口别名、定位器和步骤依赖）。WinAuto 的 C++ 校验器是当前唯一语义权威。

**Alternatives considered**:

- 仅依赖供应商的 schema 强制：无法验证项目语义，也存在不同模型兼容性差异。
- 在 UI 另建一套验证规则：会和 core validator 分叉。

Sources: [Anthropic Structured outputs](https://docs.anthropic.com/en/docs/build-with-claude/structured-outputs), [OpenAI Structured Outputs](https://platform.openai.com/docs/guides/structured-outputs).

## 决策：通过 Qt 异步网络请求管理请求生命周期

**Decision**: 使用 Qt 5.15 `QNetworkAccessManager` 执行非阻塞请求；单次生成对应可取消的 reply 和有界 timer。取消或超时即 abort 并丢弃任何部分响应。提供商重试仅限可安全重复的生成请求，次数有上限、采用退避并遵从可识别的限流提示。

**Rationale**: UI 不得因等待模型而冻结，用户取消后也必须保持当前工作流状态。HTTP 连接中断只能取消客户端等待，不能假设供应商已停止生成或退款。

**Alternatives considered**:

- 同步 HTTP 请求或 UI 线程睡眠：冻结编辑器，违背可取消性。
- 每次请求启动独立子进程：增加 IPC 和部署复杂度，生成请求本身不需要脚本执行隔离。
- 不设置时限/重试上限：无法可靠响应取消或服务端故障。

Sources: [Qt 5.15 QNetworkAccessManager](https://doc.qt.io/archives/qt-5.15/qnetworkaccessmanager.html), [Qt 5.15 QNetworkReply](https://doc.qt.io/archives/qt-5.15/qnetworkreply.html).

## 决策：使用用户自有密钥和 Windows 用户保护存储

**Decision**: 由用户提供各自的 API 凭据；不在桌面二进制中嵌入共享密钥。保存的凭据使用 Windows DPAPI 的当前用户范围加密；其他连接设置可持久化但不包含密钥。请求仅发往 HTTPS，保持 TLS 证书验证开启，禁止在 URL、提示词、日志和错误消息中记录密钥。

**Rationale**: 桌面应用无法安全保守地隐藏内置共享密钥。DPAPI 避免将用户密钥以明文放在普通配置文件中，同时符合目标平台；它不能抵御同一用户账户被攻陷，产品提示需说明这一限制。

**Alternatives considered**:

- 明文写入设置文件：密钥可由普通文件读取工具直接获取。
- 内置厂商密钥：可从分发的客户端中提取，不适合客户端授权。
- 自建服务端代理统一密钥：更适合产品托管账号方案，但需要服务端、用户认证和运营设施，不在首期范围。

Sources: [OpenAI API key safety](https://help.openai.com/en/articles/5112595-best-practices-for-api-key-safety), [Microsoft CryptProtectData](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata), [Qt 5.15 QNetworkAccessManager](https://doc.qt.io/archives/qt-5.15/qnetworkaccessmanager.html).

## 决策：限制响应、错误与日志暴露面

**Decision**: 为任务文本、模型输出 token 数和响应字节数设上限；识别鉴权失败、限流、网络错误、超时、取消、截断和无效结果。错误和日志只包含安全的状态与有限诊断，不记录完整请求/响应或密钥。

**Rationale**: 防止无界响应消耗内存或污染日志；区分可恢复错误与无效提案，方便用户处理。

**Alternatives considered**:

- 完整记录请求和响应便于排障：用户任务和 UI 内容可能包含敏感信息。
- 对任何错误自动无限重试：扩大费用、延迟并降低用户取消的确定性。

## 未决兼容性约束

- Claude 和 GPT 各自可用模型、工具调用及严格 schema 支持会随提供商和模型版本变化。实现时维护明确的支持型号/能力策略；模型不支持当前要求时给出错误，不静默降级成文本执行。
- 因提供商文档在本次研究环境无法直接访问（Anthropic 文档区域跳转，OpenAI 文档拒绝请求），来源选用其官方文档链接；实现阶段需按选择的具体型号复核参数、响应状态和速率限制字段。
