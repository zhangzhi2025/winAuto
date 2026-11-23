# Copilot instructions

## Build and test

The project uses CMake (3.16+) and CTest. On Windows, use a Visual Studio generator and a Qt 5.15 MSVC kit whose architecture matches `-A` (the UI target is omitted when Qt Widgets, Network, or WebSockets is unavailable). Example for VS 2019 Win32:

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32 -DCMAKE_PREFIX_PATH=D:/Qt/qt5.15/msvc2019 -DWINAUTO_WERROR=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Replace the Qt path with the local kit path; use `-A x64` and a matching x64 Qt kit when appropriate. For RapidOcrOnnx on Windows, `WINAUTO_ENABLE_OCR` is on by default and the Win32 DLL/model setup must match the selected architecture.

On non-Windows platforms, core, CLI, and tests can be built without Windows adapters or Qt:

```sh
cmake -S . -B build -DWINAUTO_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run one test by its CTest name (build it first if needed):

```sh
cmake --build build --target test_engine
ctest --test-dir build -R "^test_engine$" --output-on-failure
```

Other focused test names include `test_json`, `test_validator`, `test_script_runtime`, `test_ocr_locator`, `test_flow_control`, and `test_editor`. Integration checks include `core_purity`, `cli_validate_examples`, and `schema_examples`. Unit test executables use the in-repository test framework (`tests/testing.h`), not QtTest. There is no separate lint target; `WINAUTO_WERROR=ON` makes compiler warnings fail the build.

## Architecture

- `core/` is standard C++17 and is the source of truth for workflow models, JSON parsing, semantic validation, execution, matching, editor operations, and other automation logic. It must remain independent of Win32, COM, and Qt; the `core_purity` CTest enforces this.
- `core/Interfaces.h` defines the boundary to external effects (window discovery, element lookup, input, clock, and optional screenshot/clipboard services). `platform_win/` implements these interfaces with Windows APIs. Keep decision-making and reusable rules in `core/`; keep Windows-specific calls in the adapter layer. Tests inject fakes at this boundary.
- `cli/` and `ui/` are clients of the core. The UI is Qt Widgets and is optional at configure time; on Windows it links the platform adapters. Workflow editing rules belong in `core/WorkflowEditor`, while UI code forwards edits and presents state. Workflow execution is synchronous in the engine and is run on a worker thread by the UI.
- The main workflow path is `core/Model` -> `core/Json` -> `core/Validator` -> `core/Engine`; platform services are injected through `EngineDeps`. Locator matching and input planning stay in core, while `platform_win/` performs UIA, Win32 input, capture, clipboard, and script-worker operations. The Python/Lua workers request privileged operations through the engine's broker; do not bypass its validation, cancellation, deadline, or trust-confirmation paths.
- Workflow JSON is parsed into the core model and semantically checked by `core/Validator`. `schemas/workflow.schema.json` is for documentation/editor assistance, not the semantic authority. Keep examples and schema aligned when changing the workflow format.
- `tests/` contains focused core tests, CLI/schema checks, and Windows manual validation checklists. Platform-dependent runtime behavior should be checked against the relevant checklist. `tests/CMakeLists.txt` is authoritative for which test sources are built and which CTest names exist.

## Repository-specific conventions

- Represent failures with the existing `Result<T>` / `Status` and `ErrCode` types; retain useful error details and workflow paths. Keep workflow semantic rules centralized in the validator rather than duplicating them in the CLI or UI.
- Locator strategies are represented by `std::variant` in the model, so strategy-specific fields stay associated with their strategy. When changing workflow data or execution behavior, trace the change through model, JSON read/write, validation, engine, editor, and the relevant platform adapter; update schema, examples, and tests where applicable.
- Preserve conservative automation behavior: ambiguous targets fail unless the workflow explicitly opts into first-match behavior, and fallback advances only for `ElementNotFound`. Do not silently choose a target or convert other failures into absence.
- Editing, nested-step traversal, and debugging share core-owned rules and ordering (`WorkflowEditor` / `StepTree`). Keep UI row/index behavior consistent with those core rules.
- Sensitive or prompt-supplied text must remain excluded from persisted workflows and redacted from logs/reports. Preserve the existing trust-confirmation, screenshot suppression, and platform input safety checks when changing execution paths. Sensitive input must not use clipboard transport.
- Scripts share run-local workflow data, but all automation effects must go through the checked host broker. Keep `automation.read`'s metadata contract distinct from `automation.get_value`'s ValuePattern string result.
- Match the surrounding C++17 style and keep platform-specific dependencies out of public core headers. The vendored `nlohmann/json` dependency is private to core implementation files.
