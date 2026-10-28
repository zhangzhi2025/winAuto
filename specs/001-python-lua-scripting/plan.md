# Implementation Plan: Python and Lua Scripting

**Branch**: `001-python-lua-scripting` | **Date**: 2026-10-27 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/001-python-lua-scripting/spec.md`

## Summary

Add a first-class Python/Lua workflow step that can access runtime workflow data and request approved automation actions. Keep the core portable by defining a script-runner boundary; execute each script in a separate worker process and relay narrowly scoped host calls through the workflow engine. This provides a common timeout/cancellation/error contract without embedding either runtime in core or granting scripts direct access to platform handles.

## Technical Context

**Language/Version**: C++17 core; Python and Lua are optional worker runtimes selected per script step.

**Primary Dependencies**: Existing CMake/CTest and Qt 5.15 UI; external Python/Lua runtime packages are optional and are not linked into core.

**Storage**: Workflow JSON stores script language/source and an initial JSON-compatible data object. Each run gets a mutable copy shared across script steps; runtime mutations are discarded when the run ends.

**Testing**: Existing standalone C++ tests and CTest; add fake script-runner/host-call tests in core and Windows worker integration checks.

**Target Platform**: Windows desktop first; portable core validation/build remains supported on non-Windows platforms.

**Project Type**: C++17 core library with Windows platform adapters, CLI, and optional Qt Widgets desktop client.

**Performance Goals**: Script startup and host-call latency should not make ordinary workflows noticeably slower; timeout enforcement must stop a worker at the configured step deadline.

**Constraints**: `core/` remains independent of Win32, COM, and Qt. Script actions must route through existing engine/platform interfaces and safety confirmation; scripts are executable content, not a sandbox. Runtime absence, startup errors, malformed protocol messages, timeouts, and cancellation must be explicit failures. Bound protocol/output sizes and do not expose native handles.

**Scale/Scope**: One script worker per executing script step; one Python or Lua language per step; runtime data is scoped to one workflow run. No arbitrary plugin installation, script package manager, or cross-run persistence is included.

## Constitution Check

The checked-in constitution contains only unfilled template placeholders, so it establishes no actionable project-specific gates.

Applicable repository governance and architecture:

- **Core purity**: PASS — script models and runner contracts stay platform-neutral; process creation and OS termination stay outside `core/`.
- **Safety**: PASS WITH DESIGN GATE — workers receive no native handles or direct platform objects. All automation actions are brokered through the engine and continue to honor trust confirmation, privilege checks, foreground/occlusion rules, and high-risk confirmation.
- **Failure handling**: PASS — script/runtime/protocol errors use existing `Status`/`Result` and `ErrCode`; no silent skip or success-shaped fallback.
- **Sensitive data**: PASS WITH DESIGN GATE — script source/output is treated as potentially sensitive; bounded diagnostics are scrubbed before logging/reporting and must not persist prompted or sensitive input.
- **Testing**: PASS — fake-based core tests plus focused worker integration tests; preserve `core_purity`.
- **Simplicity**: PASS — use one process protocol for both languages instead of two independent embedded runtimes.

No unjustified constitution gate violations.

## Project Structure

### Documentation (this feature)

```text
specs/001-python-lua-scripting/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
│   └── script-worker-protocol.md
└── tasks.md
```

### Source Code (repository root)

```text
core/
├── Model.h / Model.cpp             # Script language, step, and initial workflow-data model
├── Json.cpp                        # Workflow JSON round-trip
├── Validator.cpp                   # Script step validation and risk reporting
├── Interfaces.h                    # Platform-neutral script runner/host boundary
├── Engine.h / Engine.cpp           # Step lifecycle, timeout, cancellation, brokered calls
└── Summary.cpp                     # Trust summary for executable script content

platform_win/
├── ScriptWorker.*                  # Windows worker lifecycle, IPC, process-tree termination
└── ScriptHost.*                    # Broker operations routed to approved engine capabilities

cli/
└── main.cpp                        # Runtime discovery/configuration and run integration

ui/
├── StepPanel.*                     # Script language/source editing
└── RunController.*                 # Worker dependencies in the run thread

schemas/
└── workflow.schema.json            # Script step JSON documentation

tests/
├── test_json.cpp
├── test_validator.cpp
├── test_engine.cpp
├── test_script_runtime.cpp         # Fake runner, broker, failure and timeout behavior
└── manual/script-runtime-checklist.md
```

**Structure Decision**: Extend existing workflow model, engine, UI, platform adapter, CLI, schema, and test locations. Keep script parsing and execution out of `core/`; the core only defines a platform-neutral runner contract and step semantics.

## Phase 0: Research

See [research.md](research.md). The design choice is a common external worker-process protocol for Python and Lua, with host-brokered operations.

## Phase 1: Design

- [Data model](data-model.md) defines the script step, runtime context, host request, and result.
- [Worker protocol contract](contracts/script-worker-protocol.md) specifies JSON-lines messages, allowed host capabilities, limits, and error behavior.
- [Quickstart](quickstart.md) describes end-to-end validation on Windows and portable core tests.

## Constitution Check (Post-Design)

- **Core purity**: PASS — process and IPC implementation reside in platform adapters; the runner interface and request/result data are standard C++ data.
- **Safety**: PASS WITH REQUIRED IMPLEMENTATION CHECKS — every requested action is dispatched through existing host capabilities, never directly from the worker; arbitrary script execution is called out in the trust summary.
- **Timeout/cancellation**: PASS — engine deadline is authoritative; platform worker termination covers the process tree; cooperative cancellation is not trusted as the only limit.
- **Privacy/logging**: PASS WITH REQUIRED IMPLEMENTATION CHECKS — protocol output is size-limited, sensitive values are scrubbed, and source/runtime output are not silently persisted.
- **Testing**: PASS — fake broker tests cover policy, state updates, malformed messages, failure, cancellation, and timeout; Windows integration covers both runtimes when installed.

No unresolved design clarifications or unjustified gate violations remain.

## Complexity Tracking

No constitution violations requiring an exception.
