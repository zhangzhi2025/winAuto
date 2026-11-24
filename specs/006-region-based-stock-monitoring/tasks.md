# Tasks: 看盘区域配置与定时识别

**Input**: Design documents from `specs/006-region-based-stock-monitoring/`

**Prerequisites**: `spec.md`, `plan.md`, `research.md`, `data-model.md`, `quickstart.md`

**Organization**: Tasks are grouped by foundational dependency and user story. Portable domain/configuration and fake-based tests precede UI integration. Every implementation task is scoped to the listed files; tests must cover failures and must not infer unconfigured page structure.

**Feature invariants**:

- The saved client-area physical pixel width and height must exactly match the selected window before any configured region can be captured or recognized. Any mismatch invalidates the whole config; no automatic scaling.
- Window handles are runtime-only and never serialized.
- Target capture is read-only and uses visible desktop pixels.
- Config and results are not persisted without explicit user action; only config JSON is in scope for persistence.
- A region has at most one in-flight OCR task. Busy ticks are skipped, not accumulated.
- Daily run interval is local `[start,end)`, does not cross midnight, and never starts the next day without another explicit Start action.

**Format**: `[ID] [P?] [Story] Description`

- `[P]`: Can run in parallel with another task in the same phase without editing the same files or depending on unfinished work.
- `[US1]`–`[US5]`: Maps to the user stories in `spec.md`.
- Paths are repository-relative.

## Phase 1: Foundational Domain, Geometry, and Test Support

**Purpose**: Establish portable, validated contracts before UI or background scheduling.

- [X] T001 Add versioned region configuration types in `stock_monitor/core/RegionConfig.h`: `RegionCaptureConfig`, `WindowIdentityHint`, `Size`, `DailyWindow`, `CaptureRegion`, `TableTemplate`, and `FieldDefinition`; keep Win32/Qt/JSON types out of public headers.
- [X] T002 [P] Add runtime result/session types in `stock_monitor/core/RegionCaptureModel.h` for capture/OCR/field status, typed values, table rows, session state, and per-region skipped ticks.
- [X] T003 Add complete configuration validation in `stock_monitor/core/RegionConfig.cpp`: positive reference client dimensions and intervals, unique IDs/names, in-bounds pixel rectangles, valid local times with start before end, valid mode/template combinations, ordered non-overlapping field columns, and valid row geometry; return field-path diagnostics.
- [X] T004 [P] Add pure client-area size matching and checked rectangle/crop helpers in `stock_monitor/core/RegionGeometry.h/.cpp`; enforce exact width-and-height equality, reject overflow/empty/out-of-bounds rectangles, and do not implement coordinate scaling.
- [X] T005 Add portable test targets and CTest registration in `tests/CMakeLists.txt` for region config, geometry, JSON, table parsing, and scheduler tests; link only needed core libraries and keep domain tests usable without OCR or a desktop.
- [X] T006 Add `tests/test_region_config.cpp` covering valid/malformed region configuration, interval/time validation, unsupported versions, table-mode requirements, unique IDs, and field-path error reporting.
- [X] T007 [P] Add `tests/test_region_geometry.cpp` covering exact-size acceptance, either-dimension mismatch rejection, window movement independence, pixel-rectangle boundaries, overflow, and source-image crop preservation.

**Checkpoint**: Portable model validation and strict client-size/crop rules pass without Win32 or Qt UI dependencies.

## Phase 2: User Story 1 - 选择窗口并配置多个采集区域 (Priority: P1)

**Goal**: Select one live window, define multiple client-area pixel regions, and preview/edit them safely.

**Independent Test**: On a visible test window, define two regions, preview both, move the window without resizing and confirm coordinates remain valid; change either client-area dimension and confirm the config cannot capture until recalibrated.

### Tests for User Story 1

- [X] T008 [P] [US1] Add pure preview-to-source coordinate mapping tests for zoomed/letterboxed images, high-DPI display scaling, reversed drag direction, and selections clipped at preview edges in `tests/test_region_geometry.cpp`.
- [X] T009 [P] [US1] Extend `tests/manual/stock-monitor-windows-checklist.md` with multi-region selection, move-without-resize, client-width/height mismatch, minimized/closed/off-screen, and occlusion scenarios.

### Implementation for User Story 1

- [X] T010 [US1] Extend `stock_monitor/platform_win/StockWindowCapture.h/.cpp` with a read-only client-area snapshot result containing client physical pixel dimensions, screen origin, and image; revalidate dimensions before and after the screen copy and reject changes during capture.
- [X] T011 [US1] Add a checked client-rectangle crop operation in `stock_monitor/platform_win/StockWindowCapture.h/.cpp` or a dedicated adapter source; crop from one captured immutable client image and return explicit invalid-window, size-mismatch, and capture errors.
- [X] T012 [US1] Add a preview selector canvas in `stock_monitor/ui/RegionSelector.h/.cpp` that maps mouse drags on the stock-monitor preview to exact source-image client pixels; keep all mouse events within stock-monitor and never forward input to the target.
- [X] T013 [US1] Add multi-region list and region editor UI in `stock_monitor/ui/RegionEditor.h/.cpp` for name, enabled state, interval, mode, selection preview, edit, and delete; reject invalid or out-of-client-area rectangles before accepting changes.
- [X] T014 [US1] Add target-window/config binding state in `stock_monitor/ui/StockMonitorWindow.h/.cpp`; record client width/height when regions are first configured, allow window movement, and mark the entire configuration unusable if either current dimension differs.
- [X] T015 [US1] Add a one-region still capture/OCR test action in `stock_monitor/ui/StockMonitorWindow.*`; require exact client-size match, display capture/OCR failures explicitly, and do not affect scheduled-session state.

**Checkpoint**: Users can choose one live target window, configure/preview multiple client-relative pixel regions, and cannot use stale-size coordinates.

## Phase 3: User Story 2 - 定义表格行和字段类型 (Priority: P1)

**Goal**: Configure fixed table rows/columns and conservatively convert OCR blocks to typed fields.

**Independent Test**: Use synthetic OCR blocks and row templates containing text, integer, decimal, and percent fields; verify raw OCR text and confidence are retained and bad/missing values have explicit per-field errors.

### Tests for User Story 2

- [X] T016 [P] [US2] Add `tests/test_region_table_parser.cpp` fixtures for single/multiple rows, missing fields, multiple blocks in a field, blocks crossing column/row boundaries, low confidence, and unconfigured plain-OCR mode.
- [X] T017 [P] [US2] Add strict type-conversion cases in `tests/test_region_table_parser.cpp`: signed 64-bit bounds, finite decimal values, optional sign/decimal point, required percent suffix and percentage-point semantics; reject locale guessing, exponent notation, overflow, and invalid formats.

### Implementation for User Story 2

- [X] T018 [US2] Implement table-template validation and deterministic row/column assignment in `stock_monitor/core/RegionTableParser.h/.cpp`; only assign blocks satisfying configured row bands and field x ranges, and mark ambiguous matches rather than selecting nearest or merging.
- [X] T019 [US2] Implement raw-text-preserving parsers for `text`, `integer`, `decimal`, and `percent` in `stock_monitor/core/RegionTableParser.cpp`; provide a typed value only on strict successful parse and retain field-local missing/invalid/ambiguous/low-confidence states.
- [X] T020 [US2] Implement plain OCR result handling and optional per-field color analysis integration in `stock_monitor/core/RegionTableParser.cpp`; pass original region pixels and source block bounds to existing color analysis, preserving its unknown/conflict result.
- [X] T021 [US2] Add table template editor UI in `stock_monitor/ui/RegionEditor.*` for first-row offset, row height/pitch, field names, column pixel ranges, value type, and optional color analysis; preview fields over the captured region.

**Checkpoint**: Fixed templates yield deterministic field values; unconfigured layouts are not inferred, and every failed field preserves its raw evidence/status.

## Phase 4: User Story 3 - 保存、重新打开和校验 JSON 配置 (Priority: P1)

**Goal**: Save and load portable, versioned region configurations without persisting HWND or partially applying invalid files.

**Independent Test**: Round-trip a multi-region table config through JSON, reopen it with the app stopped, reselect the target, and test malformed JSON, unsupported versions, invalid bounds, and failed writes while verifying active config/file remain intact.

### Tests for User Story 3

- [X] T022 [P] [US3] Add `tests/test_region_config_json.cpp` round-trip tests for all window hints, reference client dimensions, regions, intervals, table fields, data types, and daily times; assert serialized JSON contains no HWND, runtime state, screenshot, or recognition result.
- [X] T023 [P] [US3] Add `tests/test_region_config_json.cpp` invalid-input tests for parse errors, missing fields, unknown schema versions/types, invalid dimensions, out-of-bounds/overlapping ranges, invalid time windows, and field-path diagnostics; confirm failed parse leaves an existing config object unchanged.

### Implementation for User Story 3

- [X] T024 [US3] Implement private JSON serialization/parsing in `stock_monitor/core/RegionConfigJson.h/.cpp` using `nlohmann/json` only in `.cpp`; encode schema version and every persistent config field, parse into a temporary object, run complete validation, and return typed path diagnostics.
- [X] T025 [US3] Add explicit Open/Save/Save As actions in `stock_monitor/ui/StockMonitorWindow.*`; use `QFileDialog` and `QSaveFile` for atomic writes, surface read/write errors, and only replace the active config after successful parse and validation.
- [X] T026 [US3] Add dirty-state and unsaved-change prompts for opening another config, replacing current config, and closing the application in `stock_monitor/ui/StockMonitorWindow.*`; opening a valid config must leave capture stopped and require live-window reselection.
- [X] T027 [US3] Add client-size compatibility status after config load and window selection; preserve config contents on mismatch, block all capture/OCR actions, and require explicit recalibration plus save to establish new reference dimensions.

**Checkpoint**: Config files round-trip; failed load/save is visible and cannot partially replace the active config or start capture.

## Phase 5: User Story 4 - 在每日运行时段定时采集并停止 (Priority: P1)

**Goal**: Schedule multiple regions independently in a bounded session with explicit start/end and daily-window behavior.

**Independent Test**: Drive a fake clock and fake capture/OCR provider through before-start, active interval, busy-region skip, manual stop, daily end, next-day no-auto-start, target-window invalidation, and per-region failure.

### Tests for User Story 4

- [X] T028 [P] [US4] Add `tests/test_region_scheduler.cpp` tests using an injected clock for `[start,end)` boundaries, waiting before daily start, exact daily end, invalid/cross-midnight times, wall-clock jump/resume, and requiring an explicit next-day Start.
- [X] T029 [P] [US4] Add fake-worker scheduler tests in `tests/test_region_scheduler.cpp` for independent region intervals, no same-region reentry, skipped tick counts, bounded pending work, cancellation/stop semantics, and stale-session generation rejection.
- [X] T030 [P] [US4] Add capture/OCR failure and target-window invalidation cases in `tests/test_region_scheduler.cpp`; a region failure must not stop other regions, while invalid target window terminates the whole session.

### Implementation for User Story 4

- [X] T031 [US4] Implement portable clock-driven session transitions and per-region due-time calculation in `stock_monitor/core/RegionScheduler.h/.cpp`; enforce local non-cross-midnight window and do not arm next day after end.
- [X] T032 [US4] Implement per-region in-flight/skipped-tick accounting and stop behavior in `stock_monitor/core/RegionScheduler.*`; each region may have only one task, busy ticks are skipped, and stop rejects new work and cancels work not yet started.
- [X] T033 [US4] Add `stock_monitor/ui/RegionSessionController.h/.cpp` to drive the portable scheduler from one `QTimer`, dispatch due region work through a bounded worker pool, and keep capture/OCR off the GUI thread.
- [X] T034 [US4] Add session generation, cancellation ownership, and watcher cleanup in `stock_monitor/ui/RegionSessionController.*`; ignore results from stopped/replaced sessions and report in-flight task completion/cancellation explicitly.
- [X] T035 [US4] Add Start/End controls and waiting/running/ended/failed UI states in `stock_monitor/ui/StockMonitorWindow.*`; validate config, selected live window and exact client-size match before Start, require Stop before editing/loading/switching, and stop dispatch at daily end.
- [X] T036 [US4] Add visible per-region last-run time, next due time, skipped ticks, and error state to the workbench; keep a failed current-cycle result distinct from any stale prior success.

**Checkpoint**: Fake-clock and worker tests pass; scheduled tasks remain bounded, stop cleanly, and never run outside an explicitly armed daily window.

## Phase 6: User Story 5 - 检查区域截图和结构化识别结果 (Priority: P1)

**Goal**: Present per-region screenshots and OCR/table results with current timestamps and explicit failure/field states.

**Independent Test**: Run one region with normal, missing, low-confidence, invalid-format, and capture/OCR failure fixtures; verify screenshot, raw text, typed values and status remain independently visible.

### Tests for User Story 5

- [X] T037 [P] [US5] Add result-model tests for successful empty OCR versus capture/OCR failure, missing/invalid/ambiguous/low-confidence fields, stale result timestamps, and independent row-field statuses in `tests/test_region_table_parser.cpp` or a dedicated `tests/test_region_capture_result.cpp`.
- [X] T038 [P] [US5] Extend `tests/manual/stock-monitor-windows-checklist.md` with result preview, current-cycle failure versus stale prior result, OCR confidence/box display, and single-test non-interference with scheduled session.

### Implementation for User Story 5

- [X] T039 [US5] Add a region result view in `stock_monitor/ui/StockMonitorWindow.*` showing selected region image, capture timestamp, capture/OCR status, OCR text boxes, raw text, and confidence.
- [X] T040 [US5] Add table row/field presentation in `stock_monitor/ui/StockMonitorWindow.*` showing field name, raw text, typed value, OCR confidence, optional color result, and missing/invalid/ambiguous/low-confidence status.
- [X] T041 [US5] Ensure latest-cycle failure replaces the current status without appearing as an empty successful result; if retaining previous success, display its timestamp and stale indicator.
- [X] T042 [US5] Ensure the single-region test action remains isolated from scheduler start/end and presents its own result/error without persisting images or recognition data.

**Checkpoint**: Every region’s current result is reviewable and failure states cannot be confused with empty successful recognition.

## Phase 7: Integration, Documentation, and Acceptance

**Purpose**: Wire all targets, validate end-to-end behavior, and document operation/limitations.

- [X] T043 Register all new core, platform, UI, and test sources in `stock_monitor/CMakeLists.txt` and `tests/CMakeLists.txt`; keep OCR runtime DLL/model deployment unchanged and do not add dependencies on RPA input/execution targets.
- [X] T044 [P] Update `specs/006-region-based-stock-monitoring/quickstart.md` with the final UI steps, JSON config workflow, exact client-size invalidation behavior, field conversion semantics, and daily Start/End behavior.
- [X] T045 [P] Update `tests/manual/stock-monitor-windows-checklist.md` with full acceptance coverage for multiple regions, JSON persistence, client-size mismatch/recalibration, schedule boundaries, UI responsiveness, and no target input.
- [X] T046 Build `stock-monitor` and all region tests with supported VS2019 Win32 and VS2022 Win32 configurations; run focused region tests, existing stock-monitor tests, `test_stock_ocr_runtime`, and `stock_monitor_core_purity`.
- [ ] T047 Perform manual Windows acceptance with a real visible target window: save/reopen config, move window without resizing, resize client area and verify all capture is blocked, recalibrate, test two independent intervals and daily boundaries, then verify stop/close leaves no running task.
- [X] T048 Review final implementation against `specs/006-region-based-stock-monitoring/spec.md`; record any accuracy/dataset limitations and do not claim automated page understanding or real-market color accuracy beyond available fixtures.

## Dependencies and Execution Order

### Phase dependencies

- **Phase 1** has no prerequisites and blocks all feature implementation.
- **Phase 2 (US1)** depends on Phase 1 domain/geometry types and establishes client capture and region editing.
- **Phase 3 (US2)** depends on Phase 1 OCR/result types and the US1 region coordinate semantics; table editor can proceed after region editing contracts exist.
- **Phase 4 (US3)** depends on Phase 1 config types/validation; UI integration also depends on the US1 editor model so the complete config can be saved.
- **Phase 5 (US4)** depends on validated config, US1 live-window capture and US2 region processing; fake scheduler tests can be developed as soon as Phase 1 model contracts are stable.
- **Phase 6 (US5)** depends on US1 preview, US2 result contracts and US4 result delivery for periodic views; single-test result view can be implemented after US1/US2.
- **Phase 7** depends on all implemented story checkpoints.

### User-story dependency graph

```text
Phase 1 domain/config/geometry
   ├── US1 region/window configuration ───┐
   ├── US2 table parser/template ─────────┼── US4 scheduler/session
   └── US3 JSON codec ── US1 config UI ───┘
                      US1 + US2 + US4 ─────── US5 periodic result review
All stories ──────────────────────────────── Phase 7 acceptance
```

### Parallel opportunities

- T002 and T004 may proceed independently after shared names/interfaces are agreed.
- T006 and T007 can proceed in parallel after T003’s validation contract is agreed.
- T008 and T009 are independent manual/geometry test work.
- T016/T017 are independent parser test groups once `FieldDefinition` is stable.
- T022/T023 are independent JSON test groups once the schema shape is stable.
- T028/T029/T030 can proceed in parallel against the scheduler interface.
- T037/T038 are independent result/manual test work.
- T044/T045 can proceed independently from implementation once behavior is stable.

## Implementation Strategy

1. Establish strict geometry/config invariants and focused portable tests before UI work.
2. Deliver a usable vertical slice: choose window, capture client preview, define/crop a region, run one OCR test, and show region-relative boxes.
3. Add deterministic table fields and typed parsing before adding periodic execution.
4. Add versioned JSON save/load so the configuration is reproducible before scheduling.
5. Add fake-clock scheduler and bounded session ownership, then wire Start/End and region results to Qt.
6. Finish field/result review, run dual-toolchain builds and focused/manual acceptance.

## Notes

- T001–T047 are implementation tasks; T048 is the final acceptance review.
- Config round-trip tests must include the clarified exact-size rule. A mismatch must block all regions, not just portions that still happen to fit.
- A target window moving changes screen origin only; client-relative pixel regions remain unchanged as long as both client dimensions are exactly equal.
- Do not save image buffers, OCR text or periodic results as a side effect of JSON config save.
