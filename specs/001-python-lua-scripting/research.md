# Research: Python and Lua Workflow Scripting

## Decision: Execute both languages in an external worker process

**Decision**: The host launches an independent worker for each script step. Python and Lua use the same versioned, bounded JSON-lines protocol. The worker communicates with the host for data access and automation operations; it does not receive native handles or platform objects.

**Rationale**:

- A separate process provides a practical way for the Windows host to enforce timeouts and terminate a stuck script and its child processes.
- One protocol gives Python and Lua consistent lifecycle, diagnostics, and capability behavior.
- The core remains standard C++17 and does not acquire interpreter or platform dependencies.
- Runtime discovery and packaging remain optional; the application can clearly report when the selected runtime is unavailable.
- All automation operations can be mediated by the existing workflow engine, preserving privilege checks, target-window checks, and risk confirmation.

**Alternatives considered**:

- **Embed both interpreters**: technically possible but requires separate interpreter lifecycle, conversion, exception, packaging, and cancellation work; safe forced termination of arbitrary in-process code is problematic.
- **Embed Lua and launch Python externally**: feasible, but creates divergent installation, timeout, and error behavior for two user-facing languages.
- **Launch arbitrary scripts without a host protocol**: rejected because it cannot provide workflow data access or enforce that automation operations pass through existing safety checks.

## Decision: Use a narrow host broker, not a sandbox claim

**Decision**: Expose a small JSON-compatible runtime data store and approved automation operations through host requests. Do not expose engine objects, HWND/native handles, arbitrary platform APIs, or unrestricted host callbacks.

**Rationale**: The product requirement permits scripts to interact with active workflow data and automation, while the repository's safety model centralizes effects behind core/platform interfaces. Brokered operations let the engine apply the existing checks. A worker process alone is not a security sandbox: it runs with the user's account privileges.

**Alternatives considered**:

- Expose the full workflow model or native objects: rejected because it expands privilege and couples scripts to internal representations.
- Allow direct OS automation from scripts: rejected because it bypasses confirmation, target checks, and run reporting.

## Decision: Treat scripts as executable content and report them before execution

**Decision**: The run trust summary identifies script steps and language; user confirmation covers script execution. Script-requested automation still passes the existing per-operation authorization and risk gates.

**Rationale**: Arbitrary source cannot be reliably summarized statically. Existing trust confirmation is the correct user-facing boundary; broker checks remain necessary because confirming a script is not equivalent to approving every possible action.

**Alternatives considered**:

- Attempt to infer all script effects by static analysis: rejected as incomplete for dynamic languages.
- Treat the general workflow confirmation as permission to bypass host checks: rejected as inconsistent with conservative automation policy.

## Decision: Reuse the workflow step timeout, bound protocol messages, and report errors as step failures

**Decision**: The engine's resolved per-step timeout applies to the whole script call, including host requests. User stop/cancellation and deadline expiry terminate the worker process tree. Worker startup, runtime, protocol, and script exceptions map to explicit step errors with bounded, scrubbed diagnostics.

**Rationale**: This aligns scripts with existing execution lifecycle and prevents non-terminating source from holding the synchronous engine indefinitely. Separate protocol framing from script output to avoid accidental message corruption.

**Alternatives considered**:

- Trust the script to cooperate with cancellation: rejected because user code can block or enter native code.
- Treat stdout/stderr as an unbounded log stream: rejected due to memory, UI, and sensitive-data risks.

## Runtime and platform notes

- Windows does not provide an application-managed Python runtime by default. Python's embeddable distribution is an option for later packaging, but intentionally omits pip and requires explicit dependency packaging.
- Lua is designed to be embeddable and also has standalone interpreter distributions; the external worker approach keeps its user-facing contract consistent with Python.
- Windows Job Objects are a suitable process-tree lifecycle primitive for terminating a worker and its descendants.
- Runtime install/bundling policy is left as a release/deployment decision; missing selected runtimes are an explicit execution failure.

## Sources

- Python documentation, Windows embeddable package: https://docs.python.org/3/using/windows.html#the-embeddable-package
- Python documentation, embedding the CPython runtime: https://docs.python.org/3/extending/embedding.html
- Python C API, exception handling: https://docs.python.org/3/c-api/exceptions.html
- Lua 5.4 Reference Manual, introduction and error handling: https://www.lua.org/manual/5.4/manual.html#1 and https://www.lua.org/manual/5.4/manual.html#2.3
- Microsoft Learn, Job Objects: https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects
- Microsoft Learn, creating processes: https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes
- Repository architecture and safety guidance: `README.md`, `core/Interfaces.h`, `core/Engine.cpp`, `core/Engine.h`, and `core/Validator.cpp`
