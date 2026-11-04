# Data Model: 大模型辅助创建自动化流程

## Model Connection

表示用户选择的大模型服务配置。

| Field | Type | Required | Rules |
|---|---|---:|---|
| Provider | enum | Yes | `claude` 或 `openai` |
| Model name | string | Yes | 非空；必须属于用户配置或提供商当前支持范围 |
| API credential | secret | Yes, for generation | 单独安全存储；编辑界面掩码显示；不写入流程 JSON、提示词或日志 |

连接设置和密钥分开持久化。更换/清除密钥后，下一次新请求使用更新后的值；运行中的请求不在 UI 或日志回显密钥。

## Generation Request

表示一次用户发起的流程生成尝试。

| Field | Type | Required | Rules |
|---|---|---:|---|
| Task description | string | Yes | 非空；有明确的请求体大小上限 |
| Provider/model | reference | Yes | 指向当前选择且配置完整的 Model Connection |
| Capability context | bounded description | Yes | 只含首期已支持步骤及其约束 |
| Cancellation state | enum | Yes | `pending`、`completed`、`failed`、`cancelled`；用户取消后不能载入部分输出 |
| Diagnostic | string | No | 有界且不包含凭据、完整响应或其他非必要敏感内容 |

状态转换：

```text
pending -> completed
pending -> failed
pending -> cancelled
```

不得从失败或取消转换为可用草稿。重试会创建新的 generation attempt。

## Workflow Proposal

模型通过 `propose_workflow` 提交的结构化提案。提案仅是数据，不是可执行的本地工具请求。

| Field | Type | Required | Rules |
|---|---|---:|---|
| Status | enum | Yes | `ready`、`needs_clarification`、`unsupported` |
| Workflow JSON | object | When ready | 必须符合当前 schema，并通过 `readWorkflow` 和 `validateWorkflow` |
| Clarification questions | string array | When needs clarification | 至少一项、每项有界；不得创建可运行草稿 |
| Unsupported capabilities | string array | When unsupported | 说明无法由当前版本满足的任务部分 |

`ready` 时工作流至少包含有效的标识、名称、窗口定义和步骤；仅允许现有工作流步骤种类。任意可执行代码字段不作为替代能力。

## Draft Review

模型通过结构化解码和本地验证后形成的临时编辑草稿。

| Field | Type | Required | Rules |
|---|---|---:|---|
| Workflow | existing workflow model | Yes | 通过现有 validator 后才可视为有效提案 |
| Validation report | existing validation result | Yes | 错误必须可定位；错误不为零时禁止接受为有效草稿 |
| Review state | enum | Yes | `proposed`、`accepted`、`discarded` |

只有用户确认接受后，草稿才替换当前编辑器内容；在用户保存前，内容仅处于当前编辑会话。草稿载入绝不触发流程执行。

## Relationships

- 一个 Model Connection 可以被多个 Generation Request 选择。
- 一个 Generation Request 可产生一份 Workflow Proposal；澄清或不支持状态不会产生可运行 Draft Review。
- 有效 Workflow Proposal 经过本地解析和语义校验后成为 Draft Review。
- Draft Review 只有通过用户确认和现有编辑器保护流程后，才进入当前编辑工作流。
