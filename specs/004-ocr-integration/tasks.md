# Tasks: RPA OCR Integration

**Input**: Design documents from `specs/004-ocr-integration/`

**Prerequisites**: `plan.md`, `spec.md`, `research.md`, `data-model.md`, `contracts/ocr-locator-contract.md`, `contracts/script-ocr-host-contract.md`

**Organization**: Tasks are grouped by user story. Shared build, provider, and OCR runtime prerequisites are completed first. The OCR provider uses injectable C++ interfaces so core tests do not require native OCR libraries or model files.

## Format: `[ID] [P?] [Story] Description`

- `[P]`: Can proceed in parallel with other tasks in the phase without editing the same files or depending on unfinished work.
- `[US1]`–`[US4]`: User story mapping from `spec.md`.
- All paths are repository-relative.

## Phase 1: Setup

**Purpose**: Integrate RapidOcrOnnx native dependencies without changing portable core builds or building its standalone demo/benchmark.

- [ ] T001 Add an OCR build option and Windows-only OpenCV/ONNX Runtime discovery in `CMakeLists.txt` and `platform_win/CMakeLists.txt`; fail with an actionable diagnostic when OCR is requested but matching architecture/CRT libraries are unavailable, while keeping non-Windows core/CLI builds independent.
- [ ] T002 Add only the required RapidOcrOnnx C++ sources to the OCR platform target in `platform_win/CMakeLists.txt`; do not `add_subdirectory(third_party/RapidOcrOnnx)` or inherit its global flags, forced benchmark, JNI, or standalone executable targets.
- [ ] T003 [P] Add model/runtime deployment rules for the shipped models and required native runtime DLLs in `platform_win/CMakeLists.txt`; preserve user-configured external model paths and do not silently substitute missing files.

## Phase 2: Foundational

**Purpose**: Define portable OCR request/result/provider types and the platform OCR runtime required by locator, scripts, settings, and preview.

**⚠️ CRITICAL**: User-story implementations depend on the provider contract and native adapter being available.

- [ ] T004 [P] Add core OCR request, text-candidate, rectangle, and provider error data types plus injectable `IOcrProvider` in `core/Interfaces.h`; keep all types standard C++17 and free of OpenCV, Qt, and Win32.
- [ ] T005 Add an OCR provider fake and foundational request/result/error tests in `tests/test_ocr_locator.cpp`; cover empty result versus provider failure, cancellation, timeout, and candidate geometry.
- [ ] T006 Implement `OcrRuntime` in `platform_win/OcrRuntime.h` and `platform_win/OcrRuntime.cpp`; load configured detector/classifier/recognizer/dictionary once, serialize access to the non-reentrant model instance, report initialization/inference failures explicitly, and verify readiness by actual inference rather than `initModels`' return value alone.
- [ ] T007 Implement `Win32OcrProvider` in `platform_win/Win32OcrProvider.h` and `platform_win/Win32OcrProvider.cpp`; capture the target client image, invoke `OcrLite::detect(const cv::Mat&, ...)`, translate detected quadrilateral bounds to image-relative and screen physical pixels, and check cancellation/deadline before returning results.
- [ ] T008 Add focused Windows OCR runtime/provider tests in `tests/test_ocr_runtime.cpp` and register them in `tests/CMakeLists.txt`; use a fixed image and configured models when native dependencies are available, and keep portable fake-based tests runnable without them.
- [ ] T009 Connect the OCR provider to Windows execution dependencies in `cli/main.cpp` and `ui/RunController.cpp`; when OCR is unavailable, preserve non-OCR workflows and return a clear error only when OCR is requested.

**Checkpoint**: Portable builds can test the provider contract without native libraries; Windows builds either produce a usable provider with required runtime assets or clearly report missing OCR dependencies.

## Phase 3: User Story 1 - Use recognized text as a workflow target (Priority: P1)

**Goal**: Locate workflow targets by recognized text, with confidence, keyword-prefix and eight-direction filtering, while preserving ambiguity, fallback, coordinates, and input safety.

**Independent Test**: With a fake provider, cover no/one/multiple matches, keyword prefix, each of eight direction sectors relative to the target client-area center, center/boundary candidates, and fallback. Verify native integration with a fixed-image test and that existing input guards remain authoritative.

### Tests for User Story 1

- [ ] T010 [P] [US1] Add OCR locator serialization/deserialization tests in `tests/test_json.cpp` for required text, match mode, confidence, search region, optional keyword, direction, and ambiguity.
- [ ] T011 [P] [US1] Add validation tests in `tests/test_validator.cpp` for empty/invalid UTF-8 query, unsupported direction, invalid confidence, and invalid search-region bounds.
- [ ] T012 [P] [US1] Add locator matching tests in `tests/test_ocr_locator.cpp` for exact/contains matching, keyword prefix, confidence filtering, zero/one/multiple candidate outcomes, and explicit `ambiguity: first`.
- [ ] T013 [P] [US1] Add eight-direction geometry tests in `tests/test_ocr_locator.cpp` for all sectors, clockwise boundary ownership, candidate at window center, off-center window origin, and multiple candidates remaining in one sector.
- [ ] T014 [US1] Add engine/fallback tests in `tests/test_engine.cpp` for OCR locator use by locator-consuming steps, `ElementNotFound` fallback only, and preservation of ambiguity/configuration/capture/inference errors.

### Implementation for User Story 1

- [ ] T015 [US1] Add `OcrLocator` and optional direction enum to `core/Model.h` and `core/Model.cpp`; include query text, match mode, confidence, search region, optional keyword, and ambiguity policy in the existing locator variant.
- [ ] T016 [US1] Implement OCR locator JSON parsing and writing in `core/Json.cpp`; reject unknown or malformed OCR-specific fields with field-path diagnostics.
- [ ] T017 [US1] Add OCR locator semantic validation in `core/Validator.cpp`; validate query, confidence, optional keyword, crop bounds and the eight supported direction values.
- [ ] T018 [US1] Implement provider-backed OCR matching and eight-way direction filtering in the existing locator dispatch in `core/Engine.cpp`; apply filters in confidence → keyword prefix → direction → ambiguity order, use OCR box centers relative to the client-area center, and return a normal `ElementRef` with physical screen bounds.
- [ ] T019 [US1] Ensure OCR results continue through existing input safety checks in `core/Engine.cpp` and `platform_win/`; do not bypass occlusion, foreground, window ownership, risk confirmation, or click-point validation.
- [ ] T020 [P] [US1] Add the OCR locator definition to `schemas/workflow.schema.json` and update schema examples/checks without changing existing locator behavior.
- [ ] T021 [US1] Add OCR locator editing controls to `ui/StepPanel.*` for query, matching, confidence, keyword, optional direction and ambiguity; preserve invalid-edit rollback behavior and all other locator modes.

**Checkpoint**: OCR locators round-trip through workflow JSON, validate centrally, work in existing locator-consuming steps, and never silently choose a target after the filters.

## Phase 4: User Story 2 - Configure and verify the OCR engine (Priority: P1)

**Goal**: Configure the shipped or user-selected RapidOcrOnnx models, verify the runtime, and report actionable setup failures independently of model/Agent settings.

**Independent Test**: Load default shipped model assets and run a fixed-image inference; then test missing/unreadable/incompatible model and native-library cases. Confirm settings persist independently and failure is never reported as an empty OCR result.

### Tests for User Story 2

- [ ] T022 [P] [US2] Add OCR settings persistence/validation tests in `tests/test_ocr_settings.cpp` for model paths, recognizer/dictionary pairing, thread count, clear/update, and isolation from model and Agent settings.
- [ ] T023 [P] [US2] Add runtime readiness tests in `tests/test_ocr_runtime.cpp` proving missing files, model initialization errors, and failed verification inference are explicit failures.

### Implementation for User Story 2

- [ ] T024 [US2] Implement `OcrSettings` in `ui/OcrSettings.h` and `ui/OcrSettings.cpp` with dedicated QSettings keys and defaults pointing to the shipped Chinese detector/classifier/recognizer/dictionary; allow selecting the English recognizer and custom model paths without modifying workflow files.
- [ ] T025 [US2] Implement `OcrSettingsDialog` in `ui/OcrSettingsDialog.h` and `ui/OcrSettingsDialog.cpp` with model path pickers, recognizer/language selection, thread count, readiness state, and actionable native dependency/configuration errors.
- [ ] T026 [US2] Add a fixed-image OCR configuration test action in `ui/OcrSettingsDialog.cpp`; perform initialization/inference off the UI thread, show recognized text boxes on completion, and allow retry after errors.
- [ ] T027 [US2] Wire settings into `OcrRuntime` creation in `ui/RunController.cpp` and `cli/main.cpp`; reload/reinitialize safely after configuration changes and do not retain a stale model instance.
- [ ] T028 [P] [US2] Add OCR settings and readiness entry points to the existing UI menus in `ui/MainWindow.cpp`; keep OCR configuration separate from model-generation and Agent settings.

**Checkpoint**: Users can test model setup and determine readiness; invalid native dependencies/models yield explicit diagnostics, not false no-match results.

## Phase 5: User Story 3 - Call OCR from Python and Lua scripts (Priority: P1)

**Goal**: Let both script runtimes use the same read-only OCR provider through the existing checked host broker.

**Independent Test**: Run equivalent Python and Lua scripts against a fake OCR provider and compare returned candidate JSON. Verify invalid alias/arguments, timeout, cancellation, no candidates, and provider errors remain distinct and no native handle is exposed.

### Tests for User Story 3

- [ ] T029 [P] [US3] Add `ocr.recognize` host-broker tests in `tests/test_script_runtime.cpp` for alias resolution, valid options, keyword-prefix filtering, structured candidates, empty results, invalid input, and provider failure.
- [ ] T030 [P] [US3] Add Python/Lua parity integration samples/tests for the same OCR request and result shape in `tests/test_script_runtime.cpp` or the existing script-runtime test fixtures.
- [ ] T031 [US3] Add cancellation/deadline tests in `tests/test_script_runtime.cpp` for OCR host requests; verify the script worker receives explicit timeout/cancel errors and cannot continue with stale results.

### Implementation for User Story 3

- [ ] T032 [US3] Inject the OCR provider into the script host handler in `core/Engine.h` and `core/Engine.cpp`; add a read-only `ocr.recognize` operation bound to a workflow window alias and the current script cancellation/deadline.
- [ ] T033 [US3] Validate and decode OCR host arguments and serialize ordered candidate arrays in `core/Engine.cpp`; accept bounded keyword and recognition options only, reject arbitrary model paths/native handles, and preserve typed provider errors.
- [ ] T034 [US3] Add equivalent `ocr.recognize` helpers to `platform_win/script_worker/python_worker.py` and `platform_win/script_worker/lua_worker.lua`; both must call the same host operation and expose identical candidate fields.
- [ ] T035 [US3] Enforce OCR host request/result size limits and ensure OCR calls do not issue input or write recognized text to logs in `core/Engine.cpp` and `platform_win/ScriptWorker.cpp`.

**Checkpoint**: Python and Lua use the same checked OCR API, inherit script limits, and receive the same structured result/error semantics.

## Phase 6: User Story 4 - Inspect OCR results and preserve workflow safety (Priority: P2)

**Goal**: Inspect recognized boxes read-only and keep OCR activity isolated from workflow edits and logs.

**Independent Test**: Preview OCR for a test window and verify text, geometry and confidence render without input or workflow mutation. Cancel/close during inference and confirm UI responsiveness and clean state.

### Tests for User Story 4

- [ ] T036 [P] [US4] Add preview/controller tests in `tests/test_ocr_settings.cpp` or a Qt UI test target for read-only behavior, empty/error states, cancellation and repeated requests.
- [ ] T037 [P] [US4] Add diagnostics/privacy checks for OCR execution and preview; ensure task/query text, recognized text and image contents are absent from ordinary logs and persistent settings.

### Implementation for User Story 4

- [ ] T038 [US4] Implement `OcrPreviewDialog` in `ui/OcrPreviewDialog.h` and `ui/OcrPreviewDialog.cpp` to choose a validated workflow window, show the working capture region, recognized text, candidate boxes and confidence, and perform no desktop input.
- [ ] T039 [US4] Run preview inference asynchronously and support cancellation/close cleanup in `ui/OcrPreviewDialog.cpp`; prevent stale completion callbacks from updating a closed dialog.
- [ ] T040 [US4] Add an OCR preview action in `ui/MainWindow.cpp` that does not alter the current editor document, undo stack, unsaved edits, generated drafts, or model/Agent configuration.
- [ ] T041 [US4] Review OCR diagnostics and logging in `core/Engine.cpp`, `platform_win/OcrRuntime.cpp`, `platform_win/Win32OcrProvider.cpp`, and `ui/OcrPreviewDialog.cpp`; report safe error categories without persisting recognized text or screenshots.

**Checkpoint**: OCR preview is read-only, cancellable, and does not mutate or leak the user's workflow content.

## Phase 7: Polish & Cross-Cutting Validation

**Purpose**: Validate the complete OCR integration and document native setup and manual checks.

- [ ] T042 [P] Update `README.md` with OCR model defaults, native OpenCV/ONNX Runtime prerequisites, configuration, workflow locator, direction values, and Python/Lua host API usage.
- [ ] T043 [P] Add `tests/manual/ocr-windows-checklist.md` covering model readiness, Chinese/English samples, DPI/multi-monitor coordinate mapping, directional boundaries, occlusion safety, cancellation, and native DLL deployment.
- [ ] T044 Run focused OCR, script-runtime, JSON, validator, engine, schema and core-purity tests through CTest; resolve regressions without weakening existing behavior.
- [ ] T045 Build the Windows UI/CLI targets with `WINAUTO_WERROR=ON` and validate native dependency discovery and model deployment using the documented quickstart.
- [ ] T046 Verify `specs/004-ocr-integration/quickstart.md` against the finished implementation and update any command/API details that differ.

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No prerequisite; establishes native dependency discovery and deployment.
- **Foundational (Phase 2)**: Depends on Setup. Provider types, fake tests and platform runtime block user-story integration.
- **US1 (Phase 3)**: Depends on T004-T009 for provider contract and runtime wiring.
- **US2 (Phase 4)**: Depends on T006-T009 for runtime/configuration integration; settings tests can be prepared after Setup.
- **US3 (Phase 5)**: Depends on T004-T009 and the provider injection; independent of UI preview.
- **US4 (Phase 6)**: Depends on provider/runtime setup and OCR configuration from US2.
- **Polish (Phase 7)**: Depends on intended user stories being complete.

### User Story Dependencies

- **US1 (P1)**: Primary locator increment after foundation.
- **US2 (P1)**: Can be implemented after foundation and enables configurable native OCR runtime; should be available before native end-to-end acceptance.
- **US3 (P1)**: Can proceed after the provider contract; uses the same provider but does not depend on OCR locator UI.
- **US4 (P2)**: Depends on runtime configuration from US2; otherwise independent of script integration.

### Parallel Opportunities

- T001/T003 and T004/T005 can proceed in parallel when they touch separate CMake/test/core files.
- After provider types are agreed, US1 core tests and US2 settings tests can be prepared independently.
- US1 locator/model/JSON work and US3 script broker work share `core/Engine.cpp` and must be integrated sequentially there; their test files may be developed independently.
- US2 dialog and US4 preview use separate UI files but should share the finalized runtime/settings factory rather than duplicate initialization.
- Documentation and manual checklist can proceed independently from implementation after contracts stabilize.

## Implementation Strategy

1. Establish isolated native build integration and the portable provider contract.
2. Implement/configure the shared RapidOcrOnnx runtime and validate it with a fixed image.
3. Deliver US1 locator integration, JSON/schema/editor support, and direction/ambiguity behavior.
4. Deliver US2 settings and configuration verification; then add US3 Python/Lua access through the shared broker.
5. Deliver US4 read-only preview, privacy and cancellation handling.
6. Run focused portable tests and Windows native build/manual checks.
