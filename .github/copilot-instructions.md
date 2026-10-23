# Copilot instructions

## Build and test

The project uses CMake (3.16+) and CTest. On Windows, the documented Visual Studio and Qt configuration is:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64 -DWINAUTO_WERROR=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Use `"Visual Studio 16 2019"` for VS 2019. Qt 5.15 Widgets is optional; if CMake cannot find it, the UI target is skipped. On non-Windows platforms, core, CLI, and tests can be built without the Windows adapters or Qt:

```sh
cmake -S . -B build -DWINAUTO_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Run one test by its CTest name, for example:

```sh
ctest --test-dir build -R "^test_engine$" --output-on-failure
```

Other test names include `test_json`, `test_validator`, `test_editor`, `test_flow_control`, `core_purity`, and `cli_validate_examples`. The unit tests are standalone executables using the small in-repository test framework (`tests/testing.h`), not QtTest. There is no separate lint target; `WINAUTO_WERROR=ON` makes compiler warnings fail the build.

## Architecture

- `core/` is standard C++17 and is the source of truth for workflow models, JSON parsing, semantic validation, execution, matching, editor operations, and other automation logic. It must remain independent of Win32, COM, and Qt; the `core_purity` CTest enforces this.
- `core/Interfaces.h` defines the boundary to external effects (window discovery, element lookup, input, clock, and optional screenshot/clipboard services). `platform_win/` implements these interfaces with Windows APIs. Keep decision-making and reusable rules in `core/`; keep Windows-specific calls in the adapter layer. Tests inject fakes at this boundary.
- `cli/` and `ui/` are clients of the core. The UI is Qt Widgets and is optional at configure time; on Windows it links the platform adapters. Workflow editing rules belong in `core/WorkflowEditor`, while UI code forwards edits and presents state. Workflow execution is synchronous in the engine and is run on a worker thread by the UI.
- Workflow JSON is parsed into the core model and semantically checked by `core/Validator`. `schemas/workflow.schema.json` is for documentation/editor assistance, not the semantic authority. Keep examples and schema aligned when changing the workflow format.
- `tests/` contains focused core tests, CLI/schema checks, and Windows manual validation checklists. Platform-dependent runtime behavior should be checked against the relevant checklist.

## Repository-specific conventions

- Represent failures with the existing `Result<T>` / `Status` and `ErrCode` types; retain useful error details and workflow paths. Keep workflow semantic rules centralized in the validator rather than duplicating them in the CLI or UI.
- Locator strategies are represented by `std::variant` in the model, so strategy-specific fields stay associated with their strategy. When changing workflow data or execution behavior, trace the change through model, JSON read/write, validation, engine, and the relevant platform adapter; update schema, examples, and tests where applicable.
- Preserve conservative automation behavior: ambiguous targets fail unless the workflow explicitly opts into first-match behavior, and fallback advances only for `ElementNotFound`. Do not silently choose a target or convert other failures into absence.
- Editing, nested-step traversal, and debugging share core-owned rules and ordering (`WorkflowEditor` / `StepTree`). Keep UI row/index behavior consistent with those core rules.
- Sensitive or prompt-supplied text must remain excluded from persisted workflows and redacted from logs/reports. Preserve the existing trust-confirmation and platform input safety checks when changing execution paths.
- Match the surrounding C++17 style and keep platform-specific dependencies out of public core headers. The vendored `nlohmann/json` dependency is private to core implementation files.
