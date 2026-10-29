# Quickstart: Validate Python and Lua Workflow Scripts

## Prerequisites

- Windows with the project-supported Visual Studio/MSVC toolchain and CMake.
- Qt 5.15 Widgets for building the desktop UI; the core and CLI can be built without Qt.
- Python (`python.exe`) and Lua (`lua.exe`) available on `PATH`, or explicit executable paths in `WINAUTO_PYTHON` and `WINAUTO_LUA`. Python is launched with `-u`; Lua is launched with the script worker file.
- Worker files are copied to the build's `script_worker` directory. Set `WINAUTO_SCRIPT_WORKER_DIR` if they are moved after building.
- Workers communicate over private stdin/stdout pipes using version 1 JSON Lines. Stdout is protocol-only; script `print` output is discarded and is not included in logs/reports.

## Build and core tests

From the repository root:

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32 -DCMAKE_PREFIX_PATH=D:/Qt/qt5.15/msvc2019 -DWINAUTO_WERROR=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The configured `D:/Qt/qt5.15/msvc2019` kit contains 32-bit libraries, so use `-A Win32` with it. Use `-A x64` only with an x64 Qt kit. Set `CMAKE_PREFIX_PATH` to the installed Qt 5.15 kit as appropriate.

## Script APIs

Each script step receives a new worker process. Python scripts use the `data` mapping and `automation` object; Lua scripts use `data.get`/`data.set` and the `automation` table; use `json.null`, `json.array(...)`, and `json.object(...)` for explicit JSON null/empty containers. Both expose `activate_window`, `read`, `click`, `move`, `set_value`, `input_text`, and `hotkey` operations through the host broker. Window arguments are workflow aliases, never native handles. Locator arguments use the workflow locator JSON shape. Keyboard input requires the selected window to be foreground; click and value actions retain the engine's target and enabled-control checks.

Example Python:

```python
data["count"] = data.get("count", 0) + 1
automation.activate_window("main")
```

Example Lua:

```lua
data.set("count", (data.get("count") or 0) + 1)
automation.activate_window("main")
```

Scripts execute arbitrary code as the current user. The process boundary manages interpreter lifetime and failure; it is not a sandbox. Review the script and confirm the explicit trust warning before running. Only brokered automation operations are provided by the protocol.

Workflows can also use a `launch_program` step to start an executable and bind its unique visible window to a blank alias, selected by the new process ID. The step must be top-level and precede any steps that use that alias; it requires explicit trust confirmation. See `examples/launch-program-demo.json`.

## Validation scenarios

1. **Persistence and validation**: create a workflow with a Python step and a Lua step; validate it, save/reopen it, and confirm each language and source are preserved. Empty or whitespace-only code and unsupported language values must fail validation with a path to the offending field.
2. **Runtime data**: put an initial value in the workflow data object, have a script read it and update a key, then have a later script read the updated value. Confirm the saved workflow file is unchanged and the runtime copy is discarded after the run.
3. **Brokered automation**: invoke an approved automation action from each language. Confirm normal target and trust checks apply, including high-risk confirmation.
4. **Failure reporting**: test syntax/runtime error, missing runtime, worker crash, malformed/oversized protocol input, and unsupported host operation. The run report must name the script step and include bounded diagnostics.
5. **Timeout and stop**: run a non-terminating script and verify the configured step timeout stops the worker and descendants. Separately cancel a running script and verify it is reported as cancelled rather than successful.
6. **Privacy**: place a disposable test value in runtime data, deliberately print it and trigger an exception; confirm it is absent from logs/reports and the saved workflow remains unchanged.
7. **Platform boundary**: run `core_purity` and compile core/CLI without the Windows UI adapter to verify core remains independent of Windows, COM, Qt, Python, and Lua headers.

See [data-model.md](data-model.md) for entities, [contracts/script-worker-protocol.md](contracts/script-worker-protocol.md) for the worker contract, and [../../tests/manual/script-runtime-checklist.md](../../tests/manual/script-runtime-checklist.md) for Windows runtime and failure checks.
