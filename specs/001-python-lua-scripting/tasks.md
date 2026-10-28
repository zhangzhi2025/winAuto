---

description: "Implementation tasks for Python and Lua workflow scripting"
---

# Tasks: Python and Lua Workflow Scripting

**Input**: Design documents in `specs/001-python-lua-scripting/`

**Prerequisites**: `plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/script-worker-protocol.md`

**Tests**: Focused tests are included because the feature specification defines independent and failure-path verification scenarios.

**Organization**: Tasks are grouped by user story so each increment can be implemented and verified.

## Path Conventions

Paths are repository-relative. Core behavior stays in `core/`; Windows process and IPC behavior stays in `platform_win/`; UI and CLI remain clients of the core.

## Phase 1: Setup

**Purpose**: No separate project initialization is needed; the repository already has CMake/CTest targets and the feature-specific targets are added with their implementation tasks.

## Phase 2: Foundational

**Purpose**: Define the platform-neutral contract shared by script execution and failure handling.

- [X] T001 Define platform-neutral script runner, host request/response, and result types in `core/Interfaces.h`, using standard C++ data only and keeping process/IPC implementation out of `core/`

**Checkpoint**: Core script contracts are available without adding Win32, COM, Qt, Python, or Lua dependencies to `core/`.

## Phase 3: User Story 1 - Extend a Workflow with a Script (Priority: P1) - MVP

**Goal**: Add Python and Lua steps that persist in workflows, access per-run data, and invoke approved automation actions.

**Independent Test**: Save and reopen a workflow containing one Python and one Lua step; run both against an available runtime, verify each reads and updates workflow data and requests an approved action, then verify subsequent steps see the updates and the saved workflow remains unchanged.

### Tests for User Story 1

- [X] T002 [P] [US1] Add round-trip tests for Python/Lua script steps and initial workflow data in `tests/test_json.cpp`
- [X] T003 [P] [US1] Add validation tests for supported script languages, empty/whitespace-only source, and initial workflow data constraints in `tests/test_validator.cpp`

### Implementation for User Story 1

- [X] T004 [US1] Add the script-step language/source and initial JSON-compatible workflow-data model, persistence, validation, and schema representation in `core/Model.h`, `core/Model.cpp`, `core/Json.cpp`, `core/Validator.cpp`, and `schemas/workflow.schema.json`; require language `python` or `lua`, reject empty or whitespace-only code, require data keys to be non-empty and bounded with exact comparison, and allow only null, boolean, finite number, string, array, or object values with bounded serialized size and nesting
- [X] T005 [P] [US1] Add script step type/language/source editing and display in `ui/StepPanel.h` and `ui/StepPanel.cpp`, preserving the selected language and code when editing an existing workflow
- [X] T006 [P] [US1] Include executable script steps and their language in the pre-run trust summary in `core/Summary.cpp`
- [X] T007 [P] [US1] Create a mutable workflow-data copy at run start, share it among script steps for that run, discard it at run end, and dispatch script steps through the runner contract in `core/Engine.h` and `core/Engine.cpp`
- [X] T008 [US1] Implement the versioned bounded JSON-lines worker process launcher and protocol handling in `platform_win/ScriptWorker.h`, `platform_win/ScriptWorker.cpp`, and `platform_win/CMakeLists.txt`; keep protocol stdout separate from bounded script output
- [X] T009 [P] [US1] Implement Python and Lua worker-side protocol adapters and script-facing data/automation APIs in `platform_win/script_worker/python_worker.py` and `platform_win/script_worker/lua_worker.lua`, following `specs/001-python-lua-scripting/contracts/script-worker-protocol.md`
- [X] T010 [P] [US1] Implement the host broker for workflow-data access and the contract's approved automation operations in `core/Engine.cpp` alongside existing engine/platform safety checks
- [X] T011 [US1] Wire worker and host dependencies into CLI and desktop workflow execution in `cli/main.cpp` and `ui/RunController.cpp`; report a clear error if the selected runtime is unavailable
- [X] T012 [US1] Add fake-runner/host engine tests for script success, per-run data sharing and discard, and brokered action dispatch in `tests/test_script_runtime.cpp`, register the test in `tests/CMakeLists.txt`, and cover step continuation on success

**Checkpoint**: Python and Lua script steps can be authored, persisted, run with shared temporary workflow data, and invoke only brokered automation operations.

## Phase 4: User Story 2 - Diagnose Script Failures (Priority: P2)

**Goal**: Make script startup, execution, protocol, timeout, cancellation, and runtime failures visible at the failing workflow step and preserve its configured failure behavior.

**Independent Test**: Exercise syntax/runtime errors, missing runtime, worker crash, malformed/oversized protocol input, timeout, and user cancellation; verify each result names the script step, includes a bounded useful diagnostic, applies the configured stop/continue policy, and never reports timeout as cancellation or success.

### Tests for User Story 2

- [X] T013 [US2] Add tests for runtime-not-found, worker/protocol failures, timeout, cancellation, diagnostic attribution, and stop/continue behavior in `tests/test_script_runtime.cpp`

### Implementation for User Story 2

- [X] T014 [US2] Map runtime startup, script exception, worker exit, and protocol violations to explicit step errors with bounded actionable diagnostics in `core/Engine.cpp` and `platform_win/ScriptWorker.cpp`
- [X] T015 [US2] Enforce the resolved step deadline across worker execution and broker calls; send cooperative cancellation, then terminate and reap the worker process tree after a bounded grace period in `core/Engine.cpp` and `platform_win/ScriptWorker.cpp`
- [X] T016 [US2] Bound and scrub worker output and diagnostics before they enter logs or run reports, ensuring prompted/sensitive values are not persisted in `core/Engine.cpp` and `platform_win/ScriptWorker.cpp`
- [X] T017 [US2] Document and add Windows manual verification cases for runtime absence, errors, timeout, cancellation, protocol limits, privacy, and both languages in `tests/manual/script-runtime-checklist.md` and `specs/001-python-lua-scripting/quickstart.md`

**Checkpoint**: Script failures are explicit, attributable, bounded, privacy-safe, and handled by existing workflow failure policy.

## Phase 5: Polish & Cross-Cutting Concerns

**Purpose**: Verify the full feature against the design gates and ensure project architecture remains intact.

- [X] T018 Run the build and CTest scenarios documented in `specs/001-python-lua-scripting/quickstart.md`, including `core_purity`, and resolve feature-caused failures in the affected source, schema, examples, or tests

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No work; existing project initialization is sufficient.
- **Foundational (Phase 2)**: T001 must complete before either user story.
- **User Stories (Phase 3+)**: US1 depends on T001. US2 depends on the worker/engine path established by US1; it can then be completed as a separate increment.
- **Polish (Phase 5)**: Depends on the desired user stories being complete.

### User Story Dependencies

- **US1 (P1)**: Starts after T001; no dependency on another feature story.
- **US2 (P2)**: Starts after US1 establishes script execution and worker lifecycle, because it adds failure semantics to those paths.

### Within Each User Story

- US1 tests T002-T003 can be written in parallel and before their corresponding model/validation implementation T004. After T004, T005-T007 can proceed in parallel. T008 establishes the worker process before T009 and T010, which can proceed in parallel against the protocol contract; T011 wires the completed runtime into clients; T012 verifies the integrated engine path.
- US2 begins with T013 failure-path tests. T014-T016 change overlapping execution/worker lifecycle behavior and should be integrated carefully; T017 documents the final behavior.

### Dependency Graph

```text
T001
 └─ US1: (T002 || T003) -> T004 -> (T005 || T006 || T007)
                                      T007 -> T008 -> (T009 || T010) -> T011 -> T012
 US2 (after US1): T013 -> T014 -> T015 -> T016 -> T017
 Polish: T018 (after desired stories)
```

### Parallel Opportunities

- T002 and T003 edit different test files and can proceed in parallel.
- T005 (UI editing) and T006 (trust summary) can proceed independently from one another after the foundational contract.
- After T008 defines the worker process/protocol boundary, T009 and T010 can be developed in parallel if their protocol types and host-operation names are agreed first; T011 and T012 follow their integration.
- US2 failure tests can be prepared independently of the documentation task, but the worker lifecycle implementation tasks must be integrated against the same deadline and cancellation semantics.

## Parallel Example: User Story 1

```text
Task: "Add script-step JSON round-trip tests in tests/test_json.cpp"
Task: "Add script validation tests in tests/test_validator.cpp"
Task: "Add script editing controls in ui/StepPanel.h and ui/StepPanel.cpp"
Task: "Include script language in the trust summary in core/Summary.cpp"
```

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete T001, then T002-T012.
2. Verify workflow persistence, script execution for both languages, runtime-data sharing/discard, brokered automation, and successful continuation independently.
3. This delivers the core MVP. Do not claim process-tree timeout/cancellation or full failure diagnostics until US2 is complete.

### Incremental Delivery

1. Complete the foundational runner contract.
2. Implement and independently verify US1 as the first deliverable.
3. Add US2 diagnostics, timeout/cancellation enforcement, and privacy handling.
4. Run the quickstart and core-purity checks in T018.

## Notes

- Every task uses the required checkbox, sequential ID, optional parallel marker, user-story label where applicable, and explicit repository file path(s).
- `[P]` marks only tasks that can work on different files without unfinished-task dependencies.
- Runtime processes are lifecycle isolation, not a sandbox; host operations must continue to pass existing authorization and safety checks.
- Validation completed with the installed 32-bit Qt kit: the Win32 build succeeded, including UI; 19/19 CTest cases passed when excluding the existing failing `test_engine` mouse-click assertion (expected window handle 1, observed 0). The script-runtime, JSON, validator, schema, CLI, and `core_purity` tests passed. Python worker success/data IPC and timeout were exercised end to end; Lua runtime absence reports explicitly. The Lua interpreter was unavailable for a live execution check.
