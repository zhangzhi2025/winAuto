# Tasks: 独立看盘应用与股票字体颜色识别

**Input**: Design documents from `specs/005-stock-monitor/`

**Prerequisites**: `spec.md`, `plan.md`, `research.md`, `data-model.md`, `quickstart.md`

**Organization**: Tasks are grouped by user story. OCR native feasibility and the portable stock-monitor foundation are blocking prerequisites. Real-image accuracy acceptance remains blocked until a representative, user-approved, manually labeled screenshot set is available.

**Implementation status (2026-11-23)**: The standalone Qt image-input shell, read-only capture adapter, portable analysis algorithms, asynchronous OCR UI path, and focused unit tests are implemented. CMake prefers the installed shared ONNX Runtime package and deploys its DLLs beside the app and OCR test. OCR-enabled `stock-monitor` links under both VS2019/v142 and VS2022/v143; `test_stock_ocr_runtime` passes with the packaged sample image under VS2019. RapidOcrOnnx points outside the unpadded source at padded image edges, so the adapter clips quadrilateral points to the source bounds. Export, settings persistence, and SQLite history remain incomplete. The real labeled screenshot set is also unavailable.

**Format**: `[ID] [P?] [Story] Description`

- `[P]`: Can run in parallel with other tasks in the phase without editing the same files or depending on unfinished work.
- `[US1]`–`[US4]`: Maps to the user stories in `spec.md`.
- Paths are repository-relative.

## Phase 1: Setup and Feasibility Gates

**Purpose**: Prove the native OCR route and establish an isolated application target before implementing dependent functionality.

- [ ] T001 Build a small VS2019 Win32 smoke target using `third_party/RapidOcrOnnx/include/OcrLite.h`, `third_party/RapidOcrOnnx/include/OcrStruct.h`, and `OcrLite::detect`; document whether OpenCV/ONNX Runtime headers, libraries, CRT, and runtime DLLs are available. Do not use the flattened CLIB result as a geometry substitute.
- [ ] T002 [P] Identify or request a representative, user-approved Tonghuashun 9.x screenshot set containing the five font colors; define annotation format for stock code/name boxes and color labels under `tests/fixtures/stock-monitor/`. Mark SC-002/SC-003 blocked until such data is supplied and labeled.
- [x] T003 Add the `stock_monitor/` CMake subdirectory and a separate `stock-monitor` executable target in `CMakeLists.txt` and `stock_monitor/CMakeLists.txt`; keep it independently buildable and avoid linking `winauto-ui`, workflow execution, script workers, or RPA input controllers.
- [x] T004 [P] Add a stock-monitor-specific Windows capture target/source in `stock_monitor/platform_win/` or a narrowly scoped shared capture library; preserve DPI-awareness, minimized-window, off-screen, and physical-coordinate checks without depending on the complete `winauto_platform` input/runtime graph.
- [x] T005 Add explicit CMake checks and deployment rules for RapidOcrOnnx C++ sources, OpenCV/ONNX Runtime libraries, models, and required runtime DLLs in `stock_monitor/CMakeLists.txt`; fail with actionable diagnostics when requested dependencies or architecture are unavailable, without changing portable core/CLI configurations.

**Gate**: Continue OCR-dependent integration only after T001 demonstrates a real text-block result with coordinates and scores on the selected build architecture. If unavailable, record the exact blocker and do not fabricate OCR boxes.

## Phase 2: Foundational Domain and Analysis Interfaces

**Purpose**: Define portable data contracts and test infrastructure shared by all user stories.

- [x] T006 Define standard C++17 image, rectangle, OCR text-block, analysis status, and provider result types in `stock_monitor/core/StockModel.h`; keep Qt, Win32, OpenCV, and RapidOcrOnnx types outside this public model.
- [x] T007 [P] Add versioned `FontColorConfig` and `TableLayoutConfig` data types and validation interfaces in `stock_monitor/core/StockConfig.h` and `stock_monitor/core/StockConfig.cpp`; reject invalid ranges, overlapping/invalid hue intervals, unsupported versions, and impossible column layouts.
- [x] T008 Add CTest targets for the stock-monitor portable core and register them in `stock_monitor/CMakeLists.txt` and `tests/CMakeLists.txt`; ensure algorithm/config tests do not require Qt SQL or native OCR runtime.
- [x] T009 Define an injectable local OCR adapter contract in `stock_monitor/core/StockMonitorService.h`; distinguish successful empty recognition from capture, model, inference, cancellation, and timeout failures.
- [ ] T010 Implement the stock-monitor RapidOcrOnnx adapter in `stock_monitor/platform_win/RapidOcrAdapter.h/.cpp`; initialize models once, convert input pixels safely, map each `TextBlock` quad to source-image bounds and detection/recognition scores, and return explicit failures without logging recognized content.
- [ ] T011 Add fixed-image OCR integration coverage in `tests/test_stock_ocr_runtime.cpp` and register it in CMake; verify model readiness with inference, valid geometry/score bounds, zero text, and runtime failure distinctions when target native dependencies are present.
- [x] T012 Add a portable fake OCR provider and foundation tests in `tests/test_stock_monitor_service.cpp`; cover image-size/box validation, empty successful results, provider failures, and cancellation behavior.

**Checkpoint**: Portable core tests build without OCR/Qt; on supported Windows configuration the adapter returns real per-region text boxes, or CMake/runtime produces an actionable dependency error.

## Phase 3: User Story 1 - 独立启动并获取截图 (Priority: P1)

**Goal**: Start a standalone watch-app process and obtain image input by file or explicit read-only capture.

**Independent Test**: Launch `stock-monitor` without RPA running; import an image and capture a selected window. Run it beside WinAuto UI and confirm no workflow state or target-window input is changed.

### Tests for User Story 1

- [ ] T013 [P] [US1] Add file-image loading and invalid/unsupported image tests for the image input adapter in `tests/test_stock_image_input.cpp`.
- [x] T014 [P] [US1] Add manual Windows capture checklist cases for missing, minimized, partially off-screen, DPI-scaled, and successfully captured windows in `tests/manual/stock-monitor-windows-checklist.md`.
- [x] T015 [US1] Add a process-isolation smoke test or manual checklist verifying `stock-monitor` starts independently and has no RPA input/workflow dependency.

### Implementation for User Story 1

- [x] T016 [US1] Implement local image loading and source metadata in `stock_monitor/core/StockMonitorService.*` or a dedicated image-input adapter; validate dimensions/byte lengths and preserve original pixels for color analysis.
- [x] T017 [US1] Implement read-only window enumeration and capture in `stock_monitor/platform_win/StockWindowCapture.h/.cpp`; ensure the capture path exposes no mouse/keyboard operations and maps client-area image origin/physical coordinates correctly.
- [ ] T018 [US1] Complete the standalone Qt entry point and `StockMonitorWindow` in `stock_monitor/ui/main.cpp` and `stock_monitor/ui/StockMonitorWindow.*`; add explicit image-import and target-window-capture actions, source preview, and idle/working/success/failure states.
- [ ] T019 [US1] Add asynchronous capture/OCR task ownership and cancel/close cleanup in `stock_monitor/ui/StockMonitorWindow.*`; prevent stale completion callbacks from updating a closed/replaced analysis.
- [x] T020 [US1] Add process-boundary and simultaneous-run validation to `tests/manual/stock-monitor-windows-checklist.md`; verify RPA document, unsaved edits, and execution state remain unchanged and no target input is sent.

**Checkpoint**: The app starts as a separate process, loads/captures images on explicit user action, and does not interfere with RPA or the target window.

## Phase 4: User Story 2 - 股票行识别与字体颜色分类 (Priority: P1)

**Goal**: Convert real OCR blocks and original screenshot pixels into conservative stock-row and color classifications.

**Independent Test**: Use synthetic raster tests for deterministic color boundaries and annotated real screenshots for row association and accuracy. Verify five mappings, independent board/follow dimensions, unknown/low-confidence behavior, conflicts, and exclusion of price/change columns.

### Tests for User Story 2

- [x] T021 [P] [US2] Add `FontColorAnalyzer` tests in `tests/test_font_color_analyzer.cpp` for each five-color class, background/gray pixels, antialias mixes, tiny/out-of-bounds regions, foreground-pixel minimum, dominant-ratio threshold, low confidence, and batch analysis.
- [x] T022 [P] [US2] Add `StockRowAssembler` tests in `tests/test_stock_row_assembler.cpp` for row grouping, configured code/name columns, row-height variation, overlapping boxes, missing/duplicate fields, ambiguous assignment, unassigned blocks, and deterministic ordering.
- [x] T023 [P] [US2] Add `StockClassifier` tests in `tests/test_stock_classifier.cpp` for the five confirmed color mappings, independent `BoardStage`/`FollowStatus`, unknown values, and reliable code/name color conflicts.
- [ ] T024 [US2] Add annotated-image regression tests and a metric runner for five-color accuracy, stock-name color accuracy, code/name row association, and low-confidence false classifications; report SC-002/SC-003 as blocked rather than passed when the real labeled set is absent.

### Implementation for User Story 2

- [x] T025 [US2] Implement HSV/RGB foreground-pixel extraction, per-color counts, dominant color, ratio, confidence, clipping and `Unknown` rules in `stock_monitor/core/FontColorAnalyzer.h/.cpp`; avoid Qt/OpenCV dependencies and keep batch analysis allocation-bounded.
- [x] T026 [US2] Implement deterministic geometry-based row grouping and code/name assignment using validated `TableLayoutConfig` in `stock_monitor/core/StockRowAssembler.h/.cpp`; preserve ambiguous/unassigned blocks instead of guessing.
- [x] T027 [US2] Implement board-stage/follow-status/tag mapping and `COLOR_CONFLICT` handling in `stock_monitor/core/StockClassifier.h/.cpp`; use stock-name color as primary and code color only as secondary cross-check.
- [x] T028 [US2] Implement the end-to-end OCR → row assembly → original-pixel color analysis → classification pipeline in `stock_monitor/core/StockMonitorService.cpp`; never sample price/change/amount fields and propagate stage-specific failures.
- [x] T029 [US2] Add a portable `core_purity` check for `stock_monitor/core/` and run the existing repository purity checks; ensure domain/algorithm code does not include Qt, Win32, OpenCV, or RapidOcrOnnx headers.

**Checkpoint**: Fake and synthetic tests prove algorithm semantics; actual accuracy claims require annotated Tonghuashun images and must meet the spec's thresholds.

## Phase 5: User Story 3 - 调试、校准、复核和导出 (Priority: P1)

**Goal**: Inspect classification evidence, safely adjust settings, review uncertain rows, and explicitly export analysis JSON.

**Independent Test**: Analyze an image, inspect an OCR/color region and its evidence, change thresholds and re-run without restart, review a conflict without changing model confidence, export JSON, and verify invalid settings retain the previous valid configuration.

### Tests for User Story 3

- [ ] T030 [P] [US3] Add configuration JSON parse/round-trip/validation tests in `tests/test_stock_config.cpp`; cover version mismatch, bad hue ranges, invalid thresholds/layout, atomic retention of the previous valid configuration, and isolation from RPA settings.
- [ ] T031 [P] [US3] Add analysis JSON serialization and export tests in `tests/test_stock_analysis_json.cpp`; verify schema version, source-image pixel coordinates, OCR/color evidence, row status, tags, timestamp, and write failures.
- [ ] T032 [P] [US3] Add UI/controller tests or manual cases for async re-analysis, cancellation, error state, selection overlay, and retained analysis when configuration validation fails in `tests/manual/stock-monitor-windows-checklist.md`.

### Implementation for User Story 3

- [ ] T033 [US3] Implement versioned JSON load/save and validation for color rules and table layout in `stock_monitor/core/StockConfig.cpp` and `stock_monitor/config/`; only commit a fully validated configuration and preserve the last valid settings on failure.
- [ ] T034 [US3] Implement versioned analysis-result JSON serialization and explicit file export in `stock_monitor/repository/AnalysisJson.h/.cpp`; include row/code/name boxes, color diagnostics, independent classification fields, confidence, source and capture time without modifying workflow files.
- [ ] T035 [US3] Implement `FontColorDebugWidget` in `stock_monitor/ui/FontColorDebugWidget.*`; show the original image with selectable OCR boxes and display RGB/HSV, foreground sample count, dominant ratio, OCR/color confidence, labels and unknown/conflict status.
- [ ] T036 [US3] Implement `StockSettingsDialog` in `stock_monitor/ui/StockSettingsDialog.*`; expose validated HSV/foreground/confidence and layout settings, persist them in the watch-app namespace, and recompute the current image immediately after successful apply.
- [ ] T037 [US3] Add user review/annotation controls for ambiguous/low-confidence/conflict rows in `stock_monitor/ui/StockMonitorWindow.*`; retain original algorithm result and confidence separately from any user annotation.
- [ ] T038 [US3] Add explicit JSON export and source-page metadata controls in `stock_monitor/ui/StockMonitorWindow.*`; do not auto-export screenshots or OCR results and report path/permission errors without discarding in-memory analysis.
- [ ] T039 [US3] Add image/source and analysis-state privacy checks to `tests/manual/stock-monitor-windows-checklist.md`; verify no network upload or implicit persistence occurs during normal analysis.

**Checkpoint**: Users can inspect the basis of every row classification, adjust persisted settings without restart, preserve uncertain cases, and export only on explicit request.

## Phase 6: User Story 4 - 保存与查看分类历史快照 (Priority: P2)

**Goal**: Persist user-confirmed classification records locally without replacing prior captures.

**Independent Test**: Save the same stock from two analyses with different classifications, restart the application, query both records, and simulate database initialization/write failures.

### Tests for User Story 4

- [ ] T040 [P] [US4] Add SQLite repository tests in `tests/test_stock_snapshot_repository.cpp` for schema creation/versioning, transaction rollback, append-only duplicate captures, nullable/missing codes, time/source fields, query order and database failures.
- [ ] T041 [US4] Add persistence UI tests/manual cases verifying the unsaved current analysis survives save errors and no snapshot is written without explicit user action.

### Implementation for User Story 4

- [ ] T042 [US4] Implement the versioned SQLite schema and append-only transactional repository in `stock_monitor/repository/StockSnapshotRepository.h/.cpp`; do not upsert by stock code or persist full screenshots implicitly.
- [ ] T043 [US4] Configure the application-specific user-data path and verify/deploy the Qt QSQLITE plugin in `stock_monitor/CMakeLists.txt` and `stock_monitor/ui/main.cpp`; show clear diagnostics if the driver/database is unavailable.
- [ ] T044 [US4] Add explicit “save snapshot” action and historical snapshot query UI in `stock_monitor/ui/SnapshotHistoryWidget.*` and `stock_monitor/ui/StockMonitorWindow.*`; distinguish saved/unsaved/error states.

**Checkpoint**: Repeated captures remain independently queryable after restart; failures are explicit and the current analysis remains available.

## Phase 7: Polish and End-to-End Validation

**Purpose**: Verify the independent executable, native deployment, measured outcomes, and documentation without regressing RPA.

- [ ] T045 [P] Add default versioned color-rule and table-layout configuration files in `stock_monitor/config/` and document which threshold values are calibration starting points versus measured settings.
- [x] T046 [P] Complete `tests/manual/stock-monitor-windows-checklist.md` with target-window read-only capture, DPI/off-screen/minimized cases, five colors, row association, low confidence, conflict review, SQLite append behavior, JSON export, and simultaneous RPA operation.
- [ ] T047 Update root `CMakeLists.txt`, `README.md`, and `specs/005-stock-monitor/quickstart.md` with standalone build/run commands, native OCR prerequisites, separate settings/database locations, and out-of-scope capabilities.
- [ ] T048 Run focused stock-monitor unit/integration tests, existing CTest suite, schema/config checks, and core-purity checks; fix regressions without changing existing RPA workflow behavior.
- [ ] T049 Build `stock-monitor`, `winauto-ui`, and `winauto-cli` with VS2019/Qt and `WINAUTO_WERROR=ON`; verify native OCR and QSQLITE runtime deployment independently for the stock-monitor executable.
- [ ] T050 Run the quickstart and simultaneous-process manual checklist; measure actual color-analysis latency and, when labeled screenshots are available, calculate SC-002/SC-003/SC-004/SC-005 metrics and record any unmet criterion.

## Dependencies & Execution Order

### Phase Dependencies

- **Phase 1 (Setup/feasibility)**: No source implementation prerequisite. T001 is a hard gate for real OCR geometry; T002 is a data gate for accuracy acceptance.
- **Phase 2 (Foundation)**: Depends on the isolated target and OCR feasibility result. T006-T009 can establish portable types/tests; T010-T011 require T001 to pass.
- **US1 (Phase 3)**: Depends on image types, OCR/capture interfaces and application target from Phases 1-2.
- **US2 (Phase 4)**: Depends on OCR blocks and image model from Phase 2; its algorithms can proceed with synthetic tests even while real data acceptance is blocked.
- **US3 (Phase 5)**: Depends on analysis/config/result models and US1/US2 pipeline; debug UI requires classifiable row outputs.
- **US4 (Phase 6)**: Depends on finalized `StockClassificationSnapshot` from the data model and analysis output; independent of manual-review implementation details.
- **Polish (Phase 7)**: Depends on all accepted stories and actual native deployment checks.

**Current blockers/evidence**:

- T001/T005/T010: `third_party/rapidocr/win-CLIB-CPU-Win32/bin/RapidOcrOnnx.dll` and `third_party/rapidocr/models` are present. That DLL's `OcrLiteCApi.h` exports `OcrInit`, `OcrDetect`, `OcrGetLen`, `OcrGetResult`, and `OcrDestroy`; `OcrGetResult` returns only `strRes`. OpenCV x86/vc16 4.11 and the ONNX Runtime static package are now present. RapidOcrOnnx and the C++ adapter compile, but final VS2019 linking fails because ONNX `link.log` identifies a v143 build and unresolved v143 STL/runtime symbols. CMake now stops with an actionable toolset mismatch error. Need a v142-compatible ONNX package/build or a full compatible VS2022 application/Qt toolchain before smoke inference.
- T002/T024: no approved labeled stock-list screenshot fixture is available; SC-002/SC-003 remain blocked.
- T048: full CTest currently has two unrelated failures in existing `test_json` (round-trip/value-source round-trip) and `test_validator` (`input_text_text_rules`); all stock-monitor-focused tests and purity check pass.

### User Story Dependencies

- **US1 (P1)**: Main-window scaffold and explicit image/capture sources; independent of SQLite history.
- **US2 (P1)**: Requires real OCR text blocks from the foundation and image input from US1.
- **US3 (P1)**: Requires US2 analysis output; configuration and export are separate from SQLite history.
- **US4 (P2)**: Requires classified results from US2; can proceed independently from the threshold-debug UI once data contracts stabilize.

### Parallel Opportunities

- T001 native feasibility and T002 dataset acquisition can proceed in parallel.
- T006 portable model types and T007 config model can proceed in parallel after the target skeleton is established.
- T021, T022, and T023 cover distinct test files and can proceed in parallel after shared types are stable.
- Font color analyzer, row assembler, and classifier implementations (T025-T027) use separate files and can proceed in parallel once model/config contracts are fixed.
- T030/T031 tests and later SQLite tests use separate files; avoid parallel edits to `StockMonitorWindow.*` across US1/US3/US4 integration tasks.
- Native OCR, OpenCV, ONNX Runtime, and QSQLITE target-link changes share CMake surfaces and should be integrated serially.

## Implementation Strategy

1. Prove native C++ OCR text-block availability and collect a real labeled sample set; keep explicit gates.
2. Establish the standalone CMake executable and portable data/provider contracts.
3. Deliver explicit image import/window capture and prove process/read-only isolation.
4. Implement and test OCR row association, color analysis and classification independently before tuning on real screenshots.
5. Add calibration/review/export UI, then append-only local snapshot persistence.
6. Validate actual build/deployment, latency, accuracy metrics and RPA coexistence. Do not claim metrics passed without corresponding test evidence.
