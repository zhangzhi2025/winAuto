# Research: 独立看盘应用与股票字体颜色识别

## Decision: Deliver a separate Qt executable

**Decision**: Add a dedicated CMake target under `stock_monitor/` that starts its own Qt application process. It owns its configuration, window, result state and local database; it must not be a dialog or tab hosted by `winauto-ui`.

**Evidence**:

- The user explicitly requires a separate process.
- The root CMake project already builds multiple executables/targets (`winauto-cli`, `winauto-ui`) and locates Qt 5.15 conditionally.
- Current `winauto_platform` combines read-only capture with UIA, input, script, process launch, and the OCR CLIB adapter. Linking the complete target solely for capture would unnecessarily put RPA capabilities in the look-at-market dependency graph.

**Alternatives considered**:

- Add a watchlist page to `MainWindow`: rejected because it shares RPA process lifetime/state and violates the independent-process requirement.
- Spawn a second mode of `winauto-ui`: rejected because it remains coupled to UI startup, RPA dependencies and future UI changes.
- Add IPC between RPA and watch app: rejected as out of scope; no workflow handoff or command bridge is required.

## Decision: Use RapidOcrOnnx C++ text blocks, not the current CLIB result

**Decision**: The OCR adapter must return per-region text, geometry and scores. The current `third_party/rapidocr/win-CLIB-CPU-Win32` C API cannot satisfy this because it returns flattened text. The source tree `third_party/RapidOcrOnnx/include/OcrLite.h` and `OcrStruct.h` exposes `OcrLite::detect` and `OcrResult::textBlocks`; `TextBlock` contains quadrilateral points, box score, recognized text and character scores. The watch app should use this typed interface if its OpenCV/ONNX Runtime dependencies can be built/deployed for the selected target.

**Rationale**: Stock row assembly and sampling original font color require OCR geometry mapped to the source image. Guessing positions from line breaks is not acceptable.

**Alternatives considered**:

- Use the downloaded CLIB DLL and fabricate equal-width boxes from text order: rejected; those boxes do not represent OCR geometry and would corrupt color sampling.
- Link the flattened CLIB adapter: rejected; it also couples watch OCR behavior to the RPA platform target and cannot provide per-region scores.
- Use a remote OCR service: rejected; the feature is local/offline and screenshots may contain user/account data.

**Build gate**: `third_party/RapidOcrOnnx/CMakeLists.txt` forces benchmark ON, sets global C++ standard/definitions and chooses a standalone executable by default. Do not blindly `add_subdirectory` it. Configure a dedicated OCR target/source integration and validate that the repository's OpenCV and ONNX Runtime wrapper artifacts support the user's VS2019 Win32 configuration. If they do not, document the missing runtime/library and stop before claiming OCR feature complete.

## Decision: Separate capture and analysis from RPA input

**Decision**: Provide a read-only watch-app capture adapter and only expose image acquisition operations (file load and user-selected window capture). It returns an immutable image buffer and source metadata to the analysis service. No input controller, workflow engine, automation broker or trade operation is available to the watch UI.

**Evidence**:

- `platform_win/WindowGrab.cpp` captures client-area pixels via `captureScreen`, performs DPI-awareness validation, checks minimized/off-screen windows, and returns a BGRA bitmap.
- `platform_win/CMakeLists.txt` currently puts `WindowGrab.cpp` in `winauto_platform`, which also contains input, script, UIA and process-control code.

**Implementation consequence**: Reuse the read-only capture implementation through a narrowly scoped capture library/source target, or implement an equivalent thin watch-app adapter that retains the DPI/client-coordinate checks. Do not make the watch app depend on the complete RPA engine or input APIs merely to avoid a small adapter.

## Decision: Keep classification decisions in a pure C++17 module

**Decision**: Implement foreground pixel filtering, HSV/RGB classification, confidence calculation, row grouping and stock-classification mapping in `stock_monitor/core` using standard C++ types and image spans/buffers. Qt can display and convert images in UI/platform layers but must not own business mappings.

**Rationale**: These rules need deterministic unit tests, batch use and future reuse by different page collectors. The repository's core purity constraint requires keeping Qt/Win32 dependencies outside core.

**Color policy**:

- Treat the stock-name box as the primary color sample and the stock-code box as secondary evidence.
- Map red/orange/magenta to board stage 3+/1/2 respectively; map white/yellow to watchlist/not-followed respectively.
- Keep board stage and follow status separate.
- If usable pixel count, dominant ratio or confidence misses configured thresholds, output Unknown.
- If two reliable code/name colors conflict, retain a conflict state; do not silently override one value.
- Do not sample price/change/amount fields for classification.

The initial HSV ranges in `docs/rpa看盘设计.md` are calibration starting points, not proven production thresholds. Defaults remain configurable and must be tuned on annotated real screenshots.

## Decision: Assemble rows conservatively using OCR geometry

**Decision**: Convert OCR quadrilaterals to clipped axis-aligned bounds in source-image coordinates, sort by vertical center/reading order, and group blocks into rows using a configurable vertical-overlap/center-distance rule. Identify code/name columns using configured table layout (column ranges or anchor regions) plus conservative text validation. If a row has multiple plausible code/name blocks or unresolved assignment, expose it as ambiguous/unknown for review rather than guessing.

**Rationale**: The design specifies coordinate-based row composition but does not define a universal layout for all resolutions or user-customized columns. An editable, versioned table layout is safer than assuming fixed absolute x coordinates.

**Calibration requirement**: A small real screenshot set covering screen scaling, row heights and the five font colors is required before accepting the row accuracy target. No such labeled set is presently identified in the repository.

## Decision: Run OCR and bulk analysis off the GUI thread

**Decision**: Execute capture/OCR/analysis through a worker task owned by the UI controller, pass immutable input/config values, and deliver typed completion/error/cancel results to the Qt main thread. Maintain one in-flight analysis per window/image session initially; cancel and close transitions must clear busy state.

**Rationale**: ONNX inference can take long enough to make editing thresholds or closing the app unresponsive. A single operation avoids concurrent model mutation and unnecessary memory pressure while keeping UI logic simple.

## Decision: Use local versioned settings and append-only SQLite snapshots

**Decision**: Keep font-color and table-layout JSON settings under a watch-app-specific config namespace, separate from workflow/OCR/Agent/model-generation settings. Use a dedicated SQLite repository for explicitly saved classification snapshots, preserving every capture as an independent record. Use Qt SQL/QSQLITE if available in the target Qt kit; validate and deploy its driver with the application.

**Alternatives considered**:

- Store snapshots in workflow `data`: rejected; it couples unrelated applications and could persist screenshots/results in workflow files.
- Store only the latest classification per symbol: rejected; the feature calls for historical state and must not overwrite previous capture evidence.
- Automatically save every screenshot: rejected; screenshots may contain private content and the spec requires explicit user persistence.

## Decision: Treat evidence readiness as an acceptance gate

**Decision**: Separate synthetic unit tests from real-image accuracy claims. Unit tests may use generated raster patches to test thresholds and edge cases, but SC-002/SC-003 can only be evaluated on user-approved, manually labeled screenshots from the actual target application.

**Current gap**: `docs/rpa看盘设计.md` refers to a first screenshot sample but no matching screenshot is currently established as a tracked fixture; the available architecture PNG is not a stock-list screenshot. Before reporting the 98%/99% criteria as passed, acquire a representative labeled dataset covering five colors, row layouts, DPI and conflict/unknown cases.

## Sources

- Feature requirements: `specs/005-stock-monitor/spec.md`
- Product/algorithm design: `docs/rpa看盘设计.md`
- Architecture reference: `docs/看盘架构.png`
- Existing CMake targets: `CMakeLists.txt`, `ui/CMakeLists.txt`, `platform_win/CMakeLists.txt`
- Existing capture implementation: `platform_win/WindowGrab.h/.cpp`, `ScreenCapture.h/.cpp`
- OCR source API: `third_party/RapidOcrOnnx/include/OcrLite.h`, `OcrStruct.h`, `src/OcrLite.cpp`
- OCR build configuration: `third_party/RapidOcrOnnx/CMakeLists.txt`, `opencv-static/OpenCVWrapperConfig.cmake`, `onnxruntime-static/OnnxRuntimeWrapper.cmake`
- Existing OCR limitation: `specs/004-ocr-integration/quickstart.md`
