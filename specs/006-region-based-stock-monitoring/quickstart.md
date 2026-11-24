# Quickstart: 看盘区域配置与定时识别

## Requirements

- Windows 10+
- CMake 3.16+
- Visual Studio 2019 or 2022, Win32 toolchain
- Qt 5.15 MSVC Widgets and Concurrent modules
- Existing OpenCV/ONNX Runtime packages and RapidOcrOnnx models under `third_party/RapidOcrOnnx`

## Configure and build

Use the Qt 5.15 MSVC kit already installed on the machine. Create a separate build directory when changing Visual Studio generator/toolset:

```bat
cmake -S . -B build-region -G "Visual Studio 16 2019" -A Win32 ^
  -DCMAKE_PREFIX_PATH=D:/Qt/qt5.15/msvc2019 ^
  -DSTOCK_MONITOR_ENABLE_RAPIDOCR=ON ^
  -DWINAUTO_WERROR=ON

cmake --build build-region --config Release --target stock-monitor
ctest --test-dir build-region -C Release -R "region_|stock_monitor|test_stock_ocr_runtime" --output-on-failure
```

For Visual Studio 2022, use `-G "Visual Studio 17 2022"` and a distinct build directory. OCR remains local; the stock-monitor target uses the shared Win32 ONNX Runtime package when present.

## Deploy to another computer

Copy the executable together with its sibling `models` folder and runtime DLLs. The OCR adapter locates models relative to the running executable, not the build machine or current working directory:

```text
deployment-folder/
├── stock-monitor.exe
├── onnxruntime.dll
├── onnxruntime_providers_shared.dll
└── models/
    ├── ch_PP-OCRv3_det_infer.onnx
    ├── ch_ppocr_mobile_v2.0_cls_train.onnx
    ├── ch_PP-OCRv3_rec_infer.onnx
    └── ppocr_keys_v1.txt
```

The English recognition model and sample image are not required by the current Chinese OCR adapter. If a required model is missing, the error reports the expected executable-relative directory and missing paths.

## Configure and run

1. Launch `stock-monitor` as an independent process.
2. Refresh the visible-window list and choose the target application.
3. Capture its visible client area. Drag selection rectangles on the captured preview; create one or more regions and set each interval.
4. For a table region, define the first data-row position, row height/pitch and field names, column pixel ranges and types (`text`, `integer`, `decimal`, `percent`).
5. Set the local daily start/end time. Validate and save the config JSON explicitly.
6. To reuse it, open the JSON, reselect the live target window, and verify client width/height exactly match the saved dimensions. A mismatch blocks screenshot, test and scheduled capture until the regions are recalibrated and the config resaved.
7. Use a single-region test before starting. Start/End controls manage the scheduled session. Each enabled region runs at its own interval; a busy region skips ticks instead of creating a backlog.

## Runtime semantics

- The window may move; region coordinates are relative to its client-area top-left.
- Any change to client physical-pixel width or height invalidates the whole loaded config. No automatic scaling is performed.
- Capture reads visible desktop pixels. Minimized, off-screen or invalid windows fail; occluded content may reflect the covering window.
- Daily interval is local `[start,end)`, with start before end. No midnight-crossing range. The session stops at the daily end and never starts the next day without another explicit Start action.
- OCR and capture run outside the UI thread. One OCR task per region may be active; due ticks while it is busy are skipped and counted.
- Config files are saved only on explicit user action. Per-cycle screenshots and recognition results are not automatically written to disk or sent over the network.
- If the target window becomes minimized, destroyed, off-screen, or its client size changes while a session runs, the whole session stops with an explicit log message instead of continuing to fire futile ticks.

## Limitations and verification status (T048)

Implementation review against `spec.md` (FR-001..FR-023, SC-001..SC-008):

- Client-size authority: the config records the reference client size in physical pixels, and every capture is re-checked against it (`RegionSessionController::makeRequest` sends the reference size, `StockWindowCapture::captureClientRegion` refuses on mismatch). No code path scales or clips a region.
- Daily window: `RegionScheduler` enforces local `[start,end)`, refuses start >= end and midnight crossing, ends the session at the daily end, and never auto-restarts the next day.
- Per-region intervals: one in-flight OCR per region; due ticks while busy are skipped and counted (`RegionScheduler::poll` + `advanceDue(countSkipped=true)`), so no backlog builds up.
- Result honesty: failures are never reported as successes; a failure after a success shows the retained historical result with its own timestamp and a stale marker (`RegionResultTracker` + `RegionWorkspace::showResult`).
- Concurrency: capture/OCR run on a `QThreadPool`; the UI thread only polls a timer, and results from a stopped/replaced session are dropped instead of overwriting the new session.
- Side effects: this feature performs read-only capture and OCR; it sends no input, runs no workflow step, and writes no per-cycle image or text to disk.

Known limitations (do not claim more than this):

- No automated page understanding: region text is extracted block-by-block and, for table regions, split by configured row/column geometry. There is no layout inference beyond the configured template.
- No real-market color accuracy claim: five-color analysis is validated only against the labelled fixtures used by `test_font_color_analyzer`; real trading applications, themes, and anti-aliasing may differ.
- Interpretation stays limited to the configured field types (`text`, `integer`, `decimal`, `percent`); unrecognised formats are reported as invalid-format values instead of being guessed.
- Capture uses desktop BitBlt, so occluded pixels may show the covering window and protected windows cannot be captured.
- Scheduling uses the local wall clock, so a system clock jump can shift the daily window or the per-region due times within one session.
- Automated coverage stops at the core/unit level (region config, geometry, table parser, config JSON, scheduler, capture result tracking). Real-window acceptance items in `tests/manual/stock-monitor-windows-checklist.md` (including T047) still require a human operator with a live market window.
