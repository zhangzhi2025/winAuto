# Research: 看盘区域配置与定时识别

## Decision: Reuse the standalone stock-monitor application and OCR provider

**Decision**: Extend `stock_monitor/` and reuse `StockWindowCapture`, `RapidOcrAdapter`, the portable `Image`/`OcrTextBlock` types, and the existing font-color/stock-row algorithms. Do not add the feature to `winauto-ui` and do not link the RPA input/execution targets.

**Evidence**:

- The feature spec explicitly extends the independent `stock-monitor` process.
- The application already enumerates windows, checks DPI awareness/minimized/off-screen states, captures visible client-area pixels and performs asynchronous OCR.
- The C++ RapidOcrOnnx adapter and sample inference have been verified; the CLIB concatenated-text API is insufficient.

**Alternatives considered**:

- Rebuild a second OCR runtime for region capture: rejected; model initialization is expensive and existing UI shares one provider.
- Reuse workflow OCR/input services: rejected; adds unrelated RPA permissions and execution behavior.

## Decision: Persist client-area pixel dimensions and use strict equality

**Decision**: Store the reference client-area physical pixel width and height in the config, and store each region as a pixel rectangle relative to the client-area top-left. Before any preview/capture/test/scheduled task, re-read current client dimensions. Both dimensions must exactly match the saved values. A mismatch invalidates the entire config until the user reselects/recalibrates regions and explicitly saves.

**Rationale**:

- The user clarified that a size change invalidates the configuration.
- Relative pixel coordinates remain stable when the window moves; screen origin is only used to crop the visible desktop image.
- Ratio scaling could select the wrong table columns/rows after layout, DPI or internal scaling changes and would contradict the clarified requirement.

**Alternatives considered**:

- Normalize coordinates to `[0,1]` and scale on resize: rejected because any window-size change must invalidate the config.
- Save screen absolute coordinates: rejected because moving the window would invalidate otherwise stable client-relative regions.
- Persist HWND: rejected because native handles are process/session-local and become stale after restart.

## Decision: Capture once, then crop regions in memory

**Decision**: Capture the visible full client area once for a tick and crop each due region from its immutable RGBA buffer. Check the client bounds before and after the screen capture; if they changed during capture, discard the image as a geometry mismatch. Derive a region image using checked bounds and preserve original pixels for color analysis.

**Rationale**:

- Current Win32 capture already maps client origin to the desktop and validates virtual-screen bounds.
- One capture for regions due at the same time reduces GDI overhead and avoids slight timing differences between adjoining regions.
- Crop coordinate conversion has one explicit origin and can be unit tested without a desktop.

**Alternatives considered**:

- GDI capture separately for each region: rejected for repeated resource setup and inconsistent frame timing.
- Capture arbitrary desktop rects in UI code: rejected; target client origin and dimensions belong to the platform adapter.
- Capture occluded window content using undocumented/off-screen techniques: rejected; current contract is visible desktop pixels only.

## Decision: Keep typed fields and table interpretation deterministic

**Decision**: Each table region defines data-row y origin, row height/pitch and ordered non-overlapping x ranges. OCR blocks are assigned only when their centers/overlap satisfy an explicit field and row band rule. Require at most one block per field unless a future explicit aggregation policy is added. Preserve raw text and confidence; typed parsing failure, missing data, or ambiguous block assignment is represented independently.

Supported initial types:

| Type | Value | Parsing policy |
|---|---|---|
| `text` | UTF-8 string | Preserve OCR text; trim only configured outer whitespace policy (default trim) |
| `integer` | signed 64-bit integer | Strict base-10 parse; no implicit rounding |
| `decimal` | finite decimal value | Strict sign/digits/one decimal point; no implicit locale guessing |
| `percent` | finite numeric percentage | Accept explicit trailing `%`; value represents displayed percentage points (e.g. `12.5%` → `12.5`), never silently divide by 100 |

**Alternatives considered**:

- Infer rows/columns from text placement: rejected; users explicitly configure a template.
- Silently choose the nearest OCR block or concatenate multiple blocks: rejected; can corrupt financial fields.
- Convert invalid/missing fields to `0`, empty string, or `Unknown` success: rejected; masks OCR and template errors.
- Locale-sensitive number parsing without a config contract: rejected; ambiguous separators can change financial meaning.

## Decision: Versioned JSON codec in portable core, atomic file handling at the UI boundary

**Decision**: Keep region configuration types in C++17 core. Implement JSON conversion with the repository's private `nlohmann/json` dependency inside `.cpp` files and return typed `ConfigValidation` diagnostics including JSON paths. Parse into a temporary object, run complete semantic validation (dimensions, rectangles, periods, field names/types/ranges, table geometry), then replace active state. Use `QSaveFile` in the UI for same-directory atomic saves.

**Rationale**:

- Existing project already uses nlohmann/json privately and uses `QSaveFile` for safe UI JSON writes.
- Public core headers should not expose third-party JSON or Qt types.
- Atomic config replacement prevents partially applied invalid files; atomic save avoids corrupting a prior valid config.

**Alternatives considered**:

- `QJsonDocument` in the domain model: rejected; introduces Qt into portable core and makes non-GUI validation harder.
- Reuse workflow JSON/schema: rejected; region configurations are a separate format with separate lifecycle and must not enter workflow files.
- Store config automatically in SQLite: rejected; feature asks for portable user-controlled JSON files.

## Decision: Single scheduler with injected clock and bounded per-region work

**Decision**: A portable scheduler computes due regions and session transitions from an injected local-time clock. A Qt controller calls it on a single QTimer and dispatches eligible region jobs to a bounded worker pool. Track one in-flight job per region. If an interval expires while the same region is busy, skip that tick and expose the skipped count/status; do not queue catch-up jobs. Different regions can run independently.

**Rationale**:

- One shared clock/scheduler handles daily session edges consistently.
- A fake clock makes daily-window tests deterministic and fast.
- Per-region in-flight state preserves independent intervals without unbounded OCR backlog.

**Session rules**:

- Daily interval is local `[start, end)` and `start < end`; midnight crossing is rejected.
- `start` clicked outside the daily interval arms a waiting session until the next start.
- At `end`, stop dispatch and close the session; do not arm next day automatically.
- Only a new explicit user start can arm the next daily interval.
- Recheck actual wall time after timer wake/OS resume; if end has passed, stop without catch-up.
- Use a monotonic timer for intervals and wall clock only for daily start/end decisions.

**Alternatives considered**:

- One `QTimer` per region: rejected; complicates shared daily windows, pause/stop and testability.
- One worker queue for all regions: rejected; a slow region could starve others.
- Unbounded `QtConcurrent::run` per tick: rejected; can flood memory and runtime with stale captures.
- Automatically continue the next trading day: rejected; user asked for explicit start/end control and spec forbids unattended next-day start.

## Decision: Use a screenshot overlay selector, not direct target input

**Decision**: Capture the current full client area and show it in a selector canvas. Mouse drag in the stock-monitor UI defines selection in displayed-image coordinates; map through the preview scale/letterbox transform into exact source client pixels. The selector captures only input directed at the stock-monitor window and never forwards input to the target.

**Rationale**:

- This avoids drawing overlays on or activating the target window and keeps region selection independent of the target application's input behavior.
- Client image and region rectangles share one coordinate space.
- Preview zoom and high-DPI conversion can be unit tested separately.

**Alternative considered**:

- Transparent always-on-top desktop overlay: rejected for first implementation because it can obscure the target pixels being sampled and risks mouse interaction with the target; a snapshot-backed selector is more predictable.

## Sources

- Feature requirements: `specs/006-region-based-stock-monitoring/spec.md`
- Region guidance: `docs/选定区域识别.md`
- Existing application plan: `specs/005-stock-monitor/plan.md`
- Current model/config/OCR service: `stock_monitor/core/StockModel.h`, `StockConfig.h`, `StockMonitorService.h`
- Existing window capture: `stock_monitor/platform_win/StockWindowCapture.h/.cpp`
- OCR adapter: `stock_monitor/platform_win/RapidOcrAdapter.h/.cpp`
- Atomic JSON save precedent: `ui/MainWindow.cpp` (`QSaveFile`)
- Private JSON dependency precedent: `core/CMakeLists.txt`, `core/Json.cpp`
- Qt and test build: `stock_monitor/CMakeLists.txt`, `tests/CMakeLists.txt`
