# Research: RPA OCR Integration

## Decision: Call RapidOcrOnnx through its in-memory C++ API

**Decision**: The Windows adapter uses `OcrLite::initModels(det, cls, rec, keys)` once per configuration and `OcrLite::detect(const cv::Mat&, ...)` for captured images. Keep the initialized model instance behind an OCR provider/runtime owned by the platform layer.

**Evidence**:

- `third_party/RapidOcrOnnx/include/OcrLite.h` exposes `initModels` and an overload accepting `const cv::Mat&`.
- `third_party/RapidOcrOnnx/include/OcrStruct.h` defines `OcrResult::textBlocks`; each `TextBlock` contains text, four corner points, box score, character scores and timing.
- `OcrLite::detect` returns text-block points relative to the original input image after removing its internal padding, so the adapter must add the captured image's screen origin when producing screen coordinates.
- `OcrLite::initModels` currently returns `true` after calling model initialization without propagating each model's success. Treat a boolean return alone as insufficient proof of readiness; prevalidate files, guard runtime failures, and use an actual test inference to verify readiness.

**Alternatives considered**:

- Use the vendored C API: `OcrLiteCApi.h` exports an opaque handle, but `OcrLiteCApi.cpp` has fragile result buffer length/termination handling. The C++ API has typed boxes and scores and avoids that conversion.
- Spawn the demo executable: adds process startup, temporary-image I/O and text parsing, and loses typed geometry.
- Link RapidOcrOnnx into core: violates the repository's standard C++ core purity and brings OpenCV/ONNX dependencies across the architectural boundary.

## Decision: Introduce one platform-neutral OCR provider boundary

**Decision**: Add standard-library-only OCR request and result structures plus an injectable provider interface to `core/Interfaces.h`. A Windows implementation captures the requested workflow window, runs RapidOcrOnnx, and returns structured text boxes in image-relative and screen physical coordinates. Locator matching stays in core; scripts reach the same provider through the existing `Engine.cpp` host broker.

**Rationale**: The existing `IElementLocator::locate` returns a single selected element, which is appropriate for workflow locator execution but cannot serve a script API that returns all recognized candidates. A shared multi-result provider avoids duplicating inference and preserves deterministic locator matching, while fakes make all behavior testable without OCR libraries.

## Decision: Use JSON-independent workflow locator semantics

**Decision**: Add OCR as a strategy in the existing locator `std::variant`, JSON reader/writer, validator, editor and schema. Locator requests carry the query, exact/contains matching behavior, recognition threshold, optional search crop and explicit ambiguity policy. OCR results that do not match the query yield `ElementNotFound`; model/configuration/capture/inference failures preserve their own error codes. The engine's existing fallback therefore advances only on a genuine no-match.

**Rationale**: All current target actions, waits, conditions and expectations already consume locators. Integrating once at the locator boundary keeps the behavior consistent rather than creating separate OCR execution code per action.

## Decision: Keep all recognition and script access behind the engine host

**Decision**: Expose one broker operation, provisionally `ocr.recognize`, to both Python and Lua workers. It accepts a workflow window alias and bounded optional recognition/match arguments and returns a JSON array of text candidates. It never accepts a raw native handle, executes no input, and is subject to the existing script deadline, cancellation, frame-size and workflow-window validation.

**Rationale**: `Engine.cpp::handleScriptRequest` already validates requests and resolves window aliases before dispatch; `platform_win/ScriptWorker.cpp` sends the same host request protocol for either language. Reusing the broker provides one permission and lifecycle boundary.

## Decision: Configure models outside workflow documents

**Decision**: Store model paths, recognizer/language selection and thread count in an OCR-specific user setting store, separate from model-generation and Agent settings. Workflow locators contain only behavior/query configuration. Provide a readiness probe that includes actual inference rather than relying solely on file existence or RapidOcrOnnx's current `initModels` return value.

**Available model files**:

- `ch_PP-OCRv3_det_infer.onnx`
- `ch_ppocr_mobile_v2.0_cls_train.onnx`
- `ch_PP-OCRv3_rec_infer.onnx`
- `en_PP-OCRv3_rec_infer.onnx`
- `ppocr_keys_v1.txt`

The repo has separate Chinese and English recognition models but only one visible character dictionary. The selected model/dictionary pairing must be verified with fixed test images; the specification should not promise arbitrary language support from filenames alone.

## Decision: Keep native build integration isolated

**Decision**: Integrate the required RapidOcrOnnx source files and imported OpenCV/ONNX Runtime dependencies in `platform_win/` rather than blindly adding `third_party/RapidOcrOnnx` as a subdirectory. Its current CMake file sets global C++ standard/definitions, forces `OCR_BENCHMARK` on, and defaults to building a standalone executable; those effects are inappropriate for the main application. Reuse its dependency wrapper configuration only after confirming the libraries exist for the selected architecture and CRT.

**Build risk**: `third_party/RapidOcrOnnx` contains the wrappers and model files, but the available Windows OpenCV/ONNX Runtime libraries and DLLs must be verified on the actual build machine. The OCR-dependent target should fail configuration clearly if its native dependencies are absent; core and non-OCR tests should remain buildable.

## Decision: Treat keyword as a filter over detected OCR text regions

**Decision**: RapidOcrOnnx detects text regions and recognizes each region's complete text. An optional keyword filters those results by recognized-text prefix: retain every region whose recognized text starts with the keyword and return its complete recognized text and existing geometry. Without a keyword, return all otherwise-qualified regions. Do not infer a new rectangle, crop from the keyword box, or run a second OCR pass.

**Rationale**: The user clarified that keyword-based region extraction is the same text-region extraction with one additional keyword parameter; when several regions are detected, the keyword filters the set and only matching regions are needed. This reuses RapidOcrOnnx's detected boxes directly and gives Python/Lua and workflow locator consumers identical, predictable behavior.

**Multiple matches**: The OCR provider and script host return all prefix-matching regions. A workflow locator that needs one target applies its existing ambiguity policy after filtering: fail by default or select deterministically only when explicitly configured. Zero keyword matches is an ordinary empty result / `ElementNotFound`, not an inference error.

## Decision: Apply an optional eight-direction filter in workflow locators

**Decision**: After keyword filtering, a workflow OCR locator may select a direction sector relative to the center of the target window's client area. Use eight equal 45-degree sectors: `left`, `left_front` (upper-left), `front` (up), `right_front` (upper-right), `right`, `right_back` (lower-right), `back` (down), and `left_back` (lower-left). Candidate position is its OCR box center. Direction filtering narrows candidates only; zero candidates is no match and multiple candidates still follow the existing ambiguity policy. Do not select the nearest candidate implicitly.

**Rationale**: This gives the requested left/left-front/... decision one stable reference frame and preserves conservative target selection. Script recognition continues returning candidate lists; this direction filter is specific to selecting a workflow locator target.

## Sources

- Local API: `third_party/RapidOcrOnnx/include/OcrLite.h`, `OcrStruct.h`
- Local implementation: `third_party/RapidOcrOnnx/src/OcrLite.cpp`, `OcrLiteCApi.cpp`
- Local build: `third_party/RapidOcrOnnx/CMakeLists.txt`, `onnxruntime-static/OnnxRuntimeWrapper.cmake`, `opencv-static/OpenCVWrapperConfig.cmake`
- Existing integration boundaries: `core/Interfaces.h`, `core/Engine.cpp`, `platform_win/ScriptWorker.cpp`
