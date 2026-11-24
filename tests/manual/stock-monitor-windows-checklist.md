# Stock monitor Windows checklist

The current checkout can build the standalone image-input shell and portable algorithms. Native
RapidOcrOnnx text-box inference is blocked because the matching Win32 OpenCV and ONNX Runtime C++
packages are absent. Do not mark OCR or accuracy scenarios passed until that dependency gate and
the approved labeled-image gate are resolved.

## Standalone process and image input

- [ ] Launch `stock-monitor.exe` without starting `winauto-ui` or `winauto-cli`; verify it is a separate process.
- [ ] Import a user-approved PNG/JPEG/BMP screenshot and verify preview dimensions and no image/result file is created automatically.
- [ ] Reject unsupported/corrupt image files with an explicit error and keep the previous image.
- [ ] Run beside an open/unsaved workflow and while RPA is idle/running; verify workflow state is unchanged.

## Read-only window capture

- [ ] Refresh and select a visible target window; capture its visible client area without activating it or sending input.
- [ ] Verify minimized, destroyed, empty, partially off-screen, and protected windows produce explicit errors.
- [ ] Repeat at 100%, 150%, and mixed-monitor DPI; verify the captured dimensions and image coordinates.
- [ ] Cover the target window with another window and verify the operator understands that desktop BitBlt captures visible pixels and may include the occluding window.
- [ ] Verify screenshot/capture code has no mouse, keyboard, workflow, IPC, or trade operation.

## Analysis and persistence (blocked until OCR adapter and downstream UI exist)

- [ ] Verify OCR block bounds/confidences and model/inference errors using the real local C++ runtime.
- [ ] Check all five colors, gray/antialias/background, low confidence, row ambiguity, code/name conflicts, and price/change-column exclusion on approved labeled samples.
- [ ] Verify threshold/layout changes are validated, persisted separately from RPA settings, and re-applied without restart.
- [ ] Verify JSON export and SQLite snapshot save happen only after explicit user action; repeated snapshots append and survive restart.
- [ ] Verify cancel/close clears worker state and never reports a failed operation as successful.
- [ ] Measure color analysis latency and calculate SC-002/SC-003 only from the approved labeled dataset.

## Multiple client-relative regions

Use one visible target window whose client area the operator may move and resize.

- [ ] Select the target window and capture one client-area snapshot; verify the recorded reference size equals the live physical-pixel client width and height.
- [ ] Add two or more regions with different intervals, crop each on the snapshot, and verify overlapping and adjacent regions keep their own rectangles.
- [ ] Configure a table region with several fields (text, integer, decimal, percent) plus a row pitch; verify the overlay marks each row band and the field columns inside it.
- [ ] Verify capture, OCR, and scheduling never scale or clip a region: a configuration only applies while the client size matches exactly.
- [ ] Save the configuration as JSON, close the application, relaunch, open the file, and verify regions, intervals, daily start/end, table fields, and reference size are restored.
- [ ] Verify an invalid configuration or a failed write reports an explicit error and leaves the previous file content intact.
- [ ] Resize the target window by one pixel and verify the mismatch banner appears and capture, single-region test, and Start are all blocked while the configuration content is preserved.
- [ ] Re-calibrate to the current size, recheck each rectangle, save, and verify capture, test, and Start become available again.
- [ ] Start inside the daily window and verify each region fires on its own interval while the runtime table shows last run, next due, completed, failed, and skipped ticks.
- [ ] Start outside the daily window and verify the session waits for the daily start instead of capturing.
- [ ] Verify the target window is never activated: no click, key, clipboard, workflow, or trade action reaches it during capture, OCR, or session runs.
- [ ] Verify editing regions, opening another configuration, or switching the target window while a session runs requires Stop first.
- [ ] Verify closing the application with unsaved edits prompts to save and that cancelling the prompt keeps the application open.
- [ ] Drag the target window and move the mouse during a session; verify the UI stays responsive and only complete client snapshots are used.
- [ ] Minimize or close the target window during a running session; verify the session stops itself with an explicit message instead of continuing to fire futile ticks, and that a client-size change during the session stops it the same way.

## Region result presentation

- [ ] Select a region and verify the result view shows the cropped image, capture timestamp, capture status, OCR status, and the client size used.
- [ ] Verify each table row lists field name, raw OCR text, typed value, OCR confidence, and per-field status, covering parsed, missing, invalid-format, ambiguous, and low-confidence values.
- [ ] Verify the raw OCR text pane shows the ordered recognised blocks and that no image or recognised text is written to disk.
- [ ] Force a failure in the current cycle after a success: verify the status reports the current failure instead of an empty success, and that any retained historical success is labelled with its own timestamp and a stale marker.
- [ ] Run a single-region test while no session runs: verify it reports its own result or error, does not start a session, and persists no image or recognition data.
- [ ] Verify a single-region test is refused while a session is running so scheduling and manual tests cannot interleave.
- [ ] Verify OCR confidence values and recognised block text match the displayed region image and that the image is never stretched or scaled.
