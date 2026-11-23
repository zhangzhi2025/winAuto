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
