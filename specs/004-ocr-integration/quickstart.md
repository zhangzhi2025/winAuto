# OCR Integration Quickstart

## Native dependency prerequisites

The OCR adapter requires the RapidOcrOnnx source plus matching architecture/CRT builds of OpenCV and ONNX Runtime. The model assets in `third_party/RapidOcrOnnx/models` are:

- `ch_PP-OCRv3_det_infer.onnx`
- `ch_ppocr_mobile_v2.0_cls_train.onnx`
- `ch_PP-OCRv3_rec_infer.onnx` (Chinese recognizer)
- `en_PP-OCRv3_rec_infer.onnx` (English recognizer)
- `ppocr_keys_v1.txt`

Confirm the character dictionary is compatible with the selected recognition model. Configure native library and DLL deployment using the RapidOcrOnnx build guidance. OCR configuration must fail with a clear message if required native dependencies are missing; it must not affect core-only builds.

## Build and tests

Use the repository's Windows CMake/CTest configuration from `README.md` and the selected Visual Studio/Qt kit. Build the Windows platform and UI targets with warnings treated as errors, then run:

```bat
ctest --test-dir build -C Debug -R "test_ocr_locator|test_script_runtime|core_purity" --output-on-failure
```

OCR core tests use a fake provider and do not require ONNX files. Run the RapidOcrOnnx integration test and `tests/manual/ocr-windows-checklist.md` on a machine with the native runtime dependencies.

## Configure and validate

1. Open OCR settings and confirm the detector, classifier, recognizer and key dictionary point to readable files. Select the intended language recognizer.
2. Run the OCR test on a known image. Readiness requires successful inference and text-box output, not merely existing files.
3. Use OCR preview on a chosen target window. Confirm the preview is read-only and displays recognized text, boxes and confidence without issuing input.
4. Add an OCR locator to an existing supported workflow step. Validate zero, one and duplicate matches; use the optional eight-way direction filter relative to the target client-area center, and confirm duplicates remaining in a sector fail unless `ambiguity: first` is explicitly selected.
5. Add OCR to a fallback chain and verify only genuine no-match advances to the next locator; invalid configuration or inference failure stops with an explicit error.
6. Exercise `ocr.recognize` from both Python and Lua script samples. Confirm both receive the same candidate JSON and that timeout/cancellation/error are explicit.
7. Check that OCR coordinates are physical screen coordinates and that click operations still reject occluded or out-of-window targets.

## Optional keyword filtering

OCR detects and recognizes text regions as usual. An optional keyword filters the results to regions whose recognized text starts with that keyword. Workflow locators may then filter candidates by one of eight directions relative to the target window client-area center (`left`, `left_front`, `front`, `right_front`, `right`, `right_back`, `back`, `left_back`; `front` means up and `back` means down). The filters narrow the candidate set; if multiple targets remain in the selected sector, apply the existing ambiguity policy rather than silently selecting the nearest target. Direction boundaries are deterministic: angles are measured clockwise from screen-right, and a boundary belongs to the clockwise sector. No extra crop size, offset, or second recognition pass is involved.
