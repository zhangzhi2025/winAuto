# Data Model: Python and Lua Workflow Scripting

## Script Step

A workflow leaf step with a language and source code.

| Field | Type | Required | Rules |
|---|---|---:|---|
| `id` | string | Yes | Uses existing globally unique step ID rules. |
| `type` | string | Yes | `script`. |
| `language` | enum | Yes | `python` or `lua`. |
| `code` | string | Yes | Non-empty source; whitespace-only source is invalid. |
| `timeoutMs` | integer | No | Uses the existing step timeout resolution and range; applies to worker execution and broker calls. |
| `onFailure` | enum | No | Uses existing per-step `stop`/`continue` policy. |

Scripts are leaf steps: they do not contain nested workflow steps, locators, retries, or a target window unless a later requirement defines those semantics.

## Workflow Data and Runtime Context

Workflow data is a JSON-compatible key/value object stored with the workflow and provided to scripts. At run start, the host creates a mutable runtime copy; changes are visible to later script steps in that run and are discarded when the run ends.

| Attribute | Type | Rules |
|---|---|---|
| Key | string | Non-empty and at most 256 UTF-8 bytes; comparison is exact. |
| Value | JSON-compatible value | Null, boolean, finite number, string, array, or object; the complete serialized object is at most 1 MiB and values are nested at most 32 levels. |
| Initial value | workflow definition | User-authored JSON-compatible values. |
| Runtime value | workflow run | Mutable copy shared by script steps; discarded at run end and never silently written back to the workflow file. |

The context can be read and updated only through the host broker. Sensitive and prompted values must not be copied into logs, run reports, or persistent files.

## Host Operation

A request from a worker for one supported host capability.

| Attribute | Description |
|---|---|
| Request ID | Correlates the response with the request; unique within the worker session. |
| Operation | A named operation from the supported host API, such as runtime-data get/set or an approved automation action. |
| Arguments | Bounded JSON-compatible values; validated by the host before use. |
| Result | A JSON-compatible response or a structured error; host operation errors are visible to the script and remain attributable to its parent step. |

Automation operation names are a constrained set corresponding to existing supported actions: activate a named workflow window, locate/read an element, click or move to a locator, set a value, input text, and send a hotkey. All operations are mediated by existing host/engine rules. No process handle, HWND, COM object, or raw input API is part of the data model.

## Script Result

The terminal result of a script step.

| Field | Description |
|---|---|
| Status | Success, script/runtime failure, protocol failure, timeout, or cancellation. |
| Diagnostic | Bounded and scrubbed detail suitable for the existing step record and report. |
| Output | Optional bounded non-protocol output; sensitive content is not persisted and is subject to existing redaction. |
| Duration | Elapsed time recorded through the existing step-record mechanism. |

## Relationships and lifecycle

- A workflow contains zero or more script steps among its ordered steps.
- A script step selects exactly one language runtime.
- A script step runs in one worker process; the worker is associated with that step and workflow run.
- Every host operation belongs to one active script step and is authorized before it takes effect.
- Worker timeout, user cancellation, protocol failure, runtime failure, or script exception terminates that step through the existing failure-policy path.
