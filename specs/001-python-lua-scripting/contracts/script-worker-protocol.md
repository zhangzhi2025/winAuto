# Script Worker Protocol Contract

## Scope

This contract defines communication between the workflow host and a Python or Lua worker for one script step. It is an application integration boundary, not a promise that worker processes sandbox arbitrary code.

## Launch and transport

- The host starts a worker for the selected language and passes the script source and a protocol version through a private, non-persistent channel.
- The worker receives only the data required for the active step; it does not receive native handles, platform objects, or unrestricted host callbacks.
- Standard output is reserved for protocol messages. Script `print`/ordinary output is discarded by a bounded sink; stderr is not forwarded to workflow logs.
- Messages are UTF-8 JSON Lines, one complete JSON object per line. A protocol version is negotiated at startup.
- The host enforces maximum message size, nesting depth, and outstanding request count. Exact numeric limits are implementation constants documented in the implementation.

## Message kinds

### Host to worker

- `start`: `version` (currently `1`), `language`, `step_id`, `timeout_ms`, `data` (the per-run mutable copy), and `code`.
- `response`: request ID and either a result value or a structured error for a host operation.
- `cancel`: requests cooperative shutdown before the host's deadline cleanup.

### Worker to host

- `request`: request ID, operation name, and JSON arguments.
- `complete`: successful script completion and optional result value.
- `error`: script/runtime failure with bounded diagnostic details.

Unknown versions, malformed JSON, invalid message sequences, oversized messages, duplicate in-flight request IDs, or unsupported operations fail the script step explicitly.

## Host capabilities

- Runtime-data operations read or update the current run's JSON-compatible context, initialized from the workflow's data object.
- Automation operations are limited to activating a named workflow window, locating/reading an element, clicking or moving to a locator, setting a value, inputting text, and sending a hotkey. Each operation is validated and dispatched through the host's existing workflow engine/platform interfaces and safety checks.
- Operation names are `data.get` (`{"key": ...}`; returns `{"found": bool, "value": ...}`), `data.set` (`{"key": ..., "value": ...}`), `window.activate` (`{"window": alias}`), `element.read`/`element.click`/`element.move` (`{"window": alias, "locator": workflow-locator}`), `element.set_value` (also requires `text`), `input.text` (`{"window": alias, "text": ...}`), and `input.hotkey` (`{"window": alias, "keys": [...]}`).
- Python exposes these through `data` (a mapping) and `automation`; Lua uses `data.get`/`data.set` and `automation`. `print` output is discarded; stdout remains protocol-only.
- The host rejects operations that are not available in the active execution context; workers cannot directly access HWNDs, COM objects, process memory, or raw input APIs through this protocol.
- Action requests are synchronous from the script's perspective: the worker waits for the matching response before continuing.
- Any action requiring risk confirmation must be authorized before it takes effect; script execution and its possible effects are disclosed in the pre-run trust summary.

## Completion, timeout, and cancellation

- `complete` means the script step succeeded; the engine records success and proceeds to the next step.
- `error`, worker crash/exit, runtime-not-found, or protocol violation means the step fails with a useful diagnostic and the configured step failure policy applies.
- The existing effective step timeout is the absolute deadline for worker execution and broker calls.
- On user cancellation or deadline expiry, the host sends `cancel`, allows only a bounded shutdown grace period, then terminates the worker and its descendants. Timeout and cancellation remain distinct results.
- Every terminal path reaps the worker and closes its IPC resources.

## Safety, privacy, and limits

- Scripts are executable content and must be identified in the pre-run trust summary; existing risk confirmation remains required.
- Script-triggered automation never bypasses existing privilege, target-window, foreground, occlusion, or high-risk confirmation checks.
- Worker output and errors are bounded and scrubbed before logs or reports. Prompted/sensitive values are never written to workflow files, persistent worker files, or unsanitized logs.
- The process boundary is for lifecycle and isolation of interpreter failures, not a security sandbox. The worker runs with the application's user privileges unless a future platform policy explicitly changes that.
