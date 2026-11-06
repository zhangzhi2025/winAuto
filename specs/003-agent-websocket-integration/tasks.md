# Tasks: Agent WebSocket 对接

**Input**: Design documents from `specs/003-agent-websocket-integration/`

**Prerequisites**: `plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/agent-websocket-protocol.md`

**Organization**: Tasks are grouped by user story. Qt WebSockets and the service-side permission fix are shared prerequisites. The permission gate must pass before any end-to-end Agent session can be accepted as complete.

## Format: `[ID] [P?] [Story] Description`

- `[P]`: Can proceed in parallel with other tasks in the phase (distinct files / no unresolved dependency).
- `[US1]`, `[US2]`, `[US3]`: User story from `spec.md`.

## Phase 1: Setup

**Purpose**: Enable Qt WebSockets in the optional desktop target without affecting portable builds.

- [x] T001 Add optional Qt5 WebSockets component discovery in root `CMakeLists.txt`; preserve core/CLI/test configuration when the component is unavailable.
- [x] T002 Link `Qt5::WebSockets` to `winauto-ui` in `ui/CMakeLists.txt`, with an explicit configure-time explanation when the UI dependency is unavailable.

## Phase 2: Foundational

**Purpose**: Establish protocol, safe permissions, and test infrastructure required by all user stories.

- [x] T003 [P] Remove the unconditional `AUTO_APPROVE = True` override in `docs/server/server.py`; retain opt-in behavior only and confirm the default cannot invoke `PermissionHandler.approve_all`.
- [x] T004 [P] Add a local Qt WebSocket test target using `QWebSocketServer` in `tests/CMakeLists.txt` and `tests/test_agent_session.cpp`; register it only when Qt WebSockets is available.
- [x] T005 Implement independent Agent settings value type and QSettings read/write/clear operations in `ui/AgentConnectionSettings.h` and `ui/AgentConnectionSettings.cpp`, using a dedicated key prefix and no model credential APIs.
- [x] T006 Implement URL and working-directory validation plus safe URL display/redaction helpers in `ui/AgentConnectionSettings.cpp`; accept only `ws` / `wss`, require a host and an existing accessible directory, and return explicit validation errors.
- [x] T007 [P] Add settings isolation tests to `tests/test_agent_session.cpp`; verify Agent save/update/clear does not alter model setting keys or credentials.
- [x] T008 Implement the asynchronous protocol/session controller in `ui/AgentSession.h` and `ui/AgentSession.cpp`; support connect/start/resume, typed inbound event decoding, one active turn, explicit timeouts, bounded messages, and cleanup on close/error.
- [x] T009 Add protocol-controller tests in `tests/test_agent_session.cpp` for start/resume frames, malformed/unknown messages, timeout, and stale-session cleanup.

**Checkpoint**: Qt WebSockets builds independently of core; Agent settings and the protocol controller have focused tests; the bridge's default permission behavior no longer auto-approves.

## Phase 3: User Story 1 - 配置并连接 Agent 服务 (Priority: P1)

**Goal**: Configure the Agent endpoint, project directory and optional session ID; create or restore a session and show confirmed session details without affecting existing model settings.

**Independent Test**: Against the local WebSocket test server, create a session with an empty session ID and restore one with an existing ID. Verify the confirmed ID and effective working directory; invalid URL, directory, or restore ID produces an error and preserves configuration.

### Tests for User Story 1

- [x] T010 [P] [US1] Add new-session and restore-session acceptance tests in `tests/test_agent_session.cpp`, including server rejection and invalid local settings.

### Implementation for User Story 1

- [x] T011 [US1] Build the connection/settings dialog in `ui/AgentSessionDialog.h` and `ui/AgentSessionDialog.cpp`; provide editable WebSocket URL, directory picker, optional session ID, save/clear controls, and a pre-connect directory review.
- [x] T012 [US1] Wire connect to `start` when session ID is empty and `resume` when non-empty in `ui/AgentSessionDialog.cpp`; show only server-confirmed active session ID and actual directory, and retain editable settings after failures.
- [x] T013 [US1] Add a separate Agent session action to the existing Flow menu in `ui/MainWindow.h` and `ui/MainWindow.cpp`; do not route through or modify `WorkflowGeneration` or `ModelConnectionSettings`.
- [x] T014 [US1] Verify invalid URL, inaccessible/non-directory paths, failed restore, and service disconnect leave the editor workflow and its unsaved edits unchanged; cover behavior in `tests/test_agent_session.cpp`.

**Checkpoint**: Users can independently configure the Agent service, create a new session or restore one, and identify the active session and directory.

## Phase 4: User Story 2 - 与 Agent 进行会话交互 (Priority: P1)

**Goal**: Send one task at a time and display streamed assistant output, tool activity, terminal state, cancellation, and disconnection accurately.

**Independent Test**: Use the local server to stream deltas and tool events, then complete, fail, abort, and disconnect turns. Verify duplicate sends are rejected and no error/unknown/`turn_end` path reports success.

### Tests for User Story 2

- [x] T015 [P] [US2] Add turn lifecycle tests in `tests/test_agent_session.cpp` for streaming, duplicate submission, error followed by completion, `turn_end` without completion, abort confirmation, and remote close.

### Implementation for User Story 2

- [x] T016 [US2] Add chat input, send/abort/disconnect controls, and typed event history to `ui/AgentSessionDialog.cpp`; disable send while a turn is active and reject empty prompts.
- [x] T017 [US2] Render assistant deltas/messages, tool activity, turn status, and safe error messages distinctly in `ui/AgentSessionDialog.cpp`; bound displayed history and do not log full task text or tool payloads.
- [x] T018 [US2] Implement abort confirmation and terminal-state handling in `ui/AgentSession.cpp`; remain `Aborting` until server confirmation or disconnect, treat `turn_complete` as success only absent an earlier error, and never infer success from `turn_end`.
- [x] T019 [US2] Ensure closing the Agent dialog aborts/disconnects active work and clears stale session state in `ui/AgentSessionDialog.cpp` and `ui/AgentSession.cpp`.

**Checkpoint**: The session remains responsive during streaming; users can distinguish output, tool events, completion, failure, abort, and disconnect.

## Phase 5: User Story 3 - 管理连接设置并保护现有工作 (Priority: P2)

**Goal**: Persist and manage independent Agent settings while preserving the editor, generated drafts, model configuration, and permission boundaries.

**Independent Test**: Save, reopen, update, and clear Agent settings; exercise connection/request failures with unsaved editor changes and configured model generation. Verify state isolation and no automatic permission approval.

### Tests for User Story 3

- [x] T020 [P] [US3] Add settings persistence and isolation tests in `tests/test_agent_session.cpp` for URL/path/session ID update and clear, and verify model settings remain unchanged.
- [x] T021 [P] [US3] Add a bridge permission regression check in `tests/test_agent_session.cpp` or a focused Python test; verify the default configuration does not select `PermissionHandler.approve_all`.

### Implementation for User Story 3

- [x] T022 [US3] Preserve retryable form values and session ID after connection, restore, request, and transport failures in `ui/AgentSessionDialog.cpp`.
- [x] T023 [US3] Redact URL user-info and sensitive query parameters in dialog status and diagnostics; ensure task text, raw frames, and complete tool events are never written to logs in `ui/AgentSession.cpp` and `ui/AgentSessionDialog.cpp`.
- [x] T024 [US3] Document Qt WebSockets setup, reference bridge permission requirements, and manual create/resume/chat/abort verification in `README.md`.

**Checkpoint**: Agent lifecycle and persistence remain isolated from existing workflow/model state, and the service permission boundary is preserved.

## Phase 6: Polish & Cross-Cutting Validation

**Purpose**: Validate the complete feature and preserve existing build/test behavior.

- [x] T025 [P] Run focused Agent tests and the existing model-generation test using CTest; fix failures without weakening the protocol contract.
- [x] T026 [P] Run the existing `core_purity`, schema, and CLI validation tests to confirm no core or workflow format changes.
- [x] T027 Validate `specs/003-agent-websocket-integration/quickstart.md` against a local test server and update it if implementation details differ.

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No prerequisite; must complete before Qt WebSockets targets can be built.
- **Foundational (Phase 2)**: Depends on Setup. All stories depend on settings and session-controller contracts being available.
- **User Story 1 (Phase 3)**: Depends on T005-T009; uses settings and session controller for connection configuration and create/restore.
- **User Story 2 (Phase 4)**: Depends on T008-T009 and the session entry point from US1.
- **User Story 3 (Phase 5)**: Depends on US1/US2 integration and T003 permission correction.
- **Polish (Phase 6)**: Depends on all intended stories being implemented.

### User Story Dependencies

- **US1 (P1)**: First independently usable increment after foundational tasks.
- **US2 (P1)**: Builds on the connected session established by US1; it cannot be meaningfully integrated without the US1 entry point.
- **US3 (P2)**: Cross-cutting persistence/privacy validation; depends on the Agent dialog and session lifecycle from US1/US2.

### Critical Permission Gate

T003 is required before accepting any end-to-end Agent feature as satisfying FR-014. If inspection or manual validation shows the Copilot SDK auto-approves by default even without `approve_all`, stop claiming the requirement is met and add an explicit service-side permission confirmation/rejection mechanism before release.

### Parallel Opportunities

- T001 and T003 can proceed independently.
- T004 can proceed independently of the settings implementation.
- T005/T006 and T008 can proceed in parallel after the Qt WebSockets setup, provided the controller consumes the settings contract.
- Story acceptance tests can be prepared in parallel with UI work once the test server exists.
- README work can proceed independently from final test execution.

## Implementation Strategy

1. Complete setup and foundational tasks, resolving the service permission gate before end-to-end acceptance.
2. Deliver US1 so the user can create/restore sessions and inspect the active directory.
3. Add US2 interaction and lifecycle controls without permitting concurrent turns or false success.
4. Complete US3 isolation, retryability, privacy, and documentation checks.
5. Run focused Qt Agent/model tests and existing core purity/CLI/schema checks.
