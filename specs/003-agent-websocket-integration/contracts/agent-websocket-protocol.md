# Agent WebSocket Protocol Contract

This contract describes the JSON WebSocket protocol implemented by `docs/server/server.py`. Each frame is one UTF-8 JSON object. The client never invokes local tools based on received messages; `tool_event` is display-only.

## Client to server

| `type` | Required fields | Meaning |
|---|---|---|
| `start` | `working_directory`: canonical absolute path; optional `model` | Create a new session. The reference bridge enables streaming and defaults model to `auto`. |
| `resume` | `session_id`: non-empty string | Resume an existing session. |
| `chat` | `prompt`: non-empty string | Submit one request to the connected session. A second request while a turn is active is rejected. |
| `abort` | none | Ask the current session to stop its active turn. |
| `disconnect` | none | Disconnect the session and clear the server-side session state. |
| `ping` | none | Liveness probe; server replies with `pong`. |

The client sends `start` only when the configured session ID is empty; otherwise it sends `resume`. Invalid local URL, directory, or empty resume ID is rejected before transmission.

## Server to client

| `type` | Required fields | Semantics |
|---|---|---|
| `session` | `action`: `started` or `resumed`; `session_id`; `working_directory` for `started` | Confirms session creation or restore. `started` path is the server-resolved effective directory. |
| `turn_start` | `session_id` | Starts a request turn. |
| `delta` | `text` | Incremental assistant text. |
| `message` | `text` | Complete assistant message, if emitted by the SDK. It is not by itself a turn-success signal. |
| `tool_event` | `event`: JSON value | Tool activity for display only. |
| `error` | `error`: string | Server/request error. The current turn fails; text must be rendered safely and never logged verbatim. |
| `turn_complete` | none | Successful turn completion unless an error was already observed for that turn. |
| `turn_end` | none | Turn stream cleanup marker only. It is sent from the server's `finally` path and is not proof of success. |
| `status` | `status`: `aborted` or `disconnected` | Lifecycle update. `aborted` ends the current turn without disconnecting the session. |
| `pong` | none | Response to `ping`. |

Unknown `type`, invalid JSON, wrong field types, oversized frames, or out-of-state messages are protocol errors. They must not be interpreted as assistant text or success. Preserve request input and saved configuration so the user can correct/retry.

## Lifecycle and failure rules

1. A WebSocket handshake transitions to `Starting` or `Resuming`, not `Connected`. Only a valid `session` confirmation establishes the session.
2. One request turn may be active at a time. The UI disables submission while running and rejects duplicate submissions at the session controller boundary.
3. `turn_complete` is the only success terminal event. If `error` arrives first, a later `turn_complete` cannot erase the failure. `turn_end` only releases turn-local resources.
4. Abort requests are asynchronous. Show `Aborting` until the server confirms `status: aborted` or the connection closes; do not claim completion merely because the abort frame was sent.
5. Local disconnect, remote close, timeout, malformed messages, and transport errors clear active session availability and cannot report request success.
6. Apply finite connection/response timeouts and a maximum inbound frame size. Do not retry a `chat` automatically because delivery may have occurred even when its response was lost.
7. Do not log prompts, raw frames, full tool payloads, or unredacted URLs. A server-provided error may contain sensitive content and must be treated as untrusted display text.

## Permission boundary

Permission approval is not a client message in this protocol. The client must not imply that opening a session authorizes all tool operations. The reference bridge defaults `COPILOT_AUTO_APPROVE` to false and only selects automatic approval when explicitly opted in. Verify the deployed SDK's default permission behavior manually; if it auto-approves without this callback, the bridge must add explicit confirmation or rejection before FR-014 can be accepted end to end.
