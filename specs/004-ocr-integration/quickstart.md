# OCR Integration Quickstart

## Native dependency prerequisites

The current adapter uses the downloaded Win32 CLIB package at `third_party/rapidocr/win-CLIB-CPU-Win32`; it links `lib/RapidOcrOnnx.lib` and deploys `bin/RapidOcrOnnx.dll`. It calls the package's `include/OcrLiteCApi.h` API directly, so a separate OpenCV or ONNX Runtime development package is not required. The model assets in `third_party/rapidocr/models` are:

- `ch_PP-OCRv3_det_infer.onnx`
- `ch_ppocr_mobile_v2.0_cls_train.onnx`
- `ch_PP-OCRv3_rec_infer.onnx` (Chinese recognizer)
- `en_PP-OCRv3_rec_infer.onnx` (English recognizer)
- `ppocr_keys_v1.txt`

Confirm the character dictionary is compatible with the selected recognition model. Configure native library and DLL deployment using the RapidOcrOnnx build guidance. OCR configuration must fail with a clear message if required native dependencies are missing; it must not affect core-only builds.

With Qt 5.15.19 for MSVC 2019 and Visual Studio 2019, configure the Win32 build with OCR enabled:

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32 ^
  -DCMAKE_PREFIX_PATH=D:/Qt/qt5.15.19/msvc2019 ^
  -DWINAUTO_ENABLE_OCR=ON -DWINAUTO_WERROR=ON
cmake --build build --config Debug
```

OCR is opt-in (`WINAUTO_ENABLE_OCR=OFF` by default) and this downloaded package supports Win32 only. CMake checks the DLL, import library, header and model files, then deploys the DLL, models, and a verification image beside the executable.

**CLIB limitation:** `OcrLiteCApi.h` returns concatenated recognized text only; it does not expose per-text boxes or confidence scores. Script OCR returns `{text, candidates: []}` and supports line-prefix keyword filtering. Workflow OCR locator/click and direction filtering are deliberately rejected as `Unsupported` with an explanation rather than inventing coordinates. The C API also conflates “no text recognized” and `OcrDetect` failure in its boolean return, so those two outcomes cannot be distinguished. A DLL exposing `TextBlock` geometry and typed inference errors is required to fully enable workflow OCR locators.

## Build and tests

Use the repository's Windows CMake/CTest configuration from `README.md` and the selected Visual Studio/Qt kit. Build the Windows platform and UI targets with warnings treated as errors, then run:

```bat
ctest --test-dir build -C Debug -R "test_ocr_locator|test_script_runtime|core_purity" --output-on-failure
```

OCR core tests use a fake provider and do not require ONNX files. Run the RapidOcrOnnx integration test and `tests/manual/ocr-windows-checklist.md` on a machine with the native runtime dependencies.

## Configure and validate

1. Run the UI/CLI from its build output directory so the deployed `models` directory and DLL are found.
2. Execute `automation.ocr("main")` in Python or `automation.ocr("main")` in Lua after defining the workflow window alias; read the returned `text` field.
3. Pass `keyword="Save"` (Python) or `"Save"` as the second Lua argument to retain recognized lines beginning with that prefix.
4. Do not use the current CLIB adapter for OCR workflow locators: this DLL does not provide region geometry, and the engine rejects locator use rather than clicking guessed positions.

## Optional keyword filtering

The portable OCR locator matcher supports confidence, keyword-prefix and eight-direction filters when a provider returns box geometry. The currently downloaded CLIB DLL exposes only flattened text, so those locator features remain unavailable with this adapter until a region-capable DLL is provided.
