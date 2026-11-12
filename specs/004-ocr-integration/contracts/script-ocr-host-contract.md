# Script OCR Host Contract

Python and Lua workers use the same existing JSON-lines host broker. OCR is exposed as a read-only host operation; scripts do not load RapidOcrOnnx or receive native window handles.

## Request

The operation name is `ocr.recognize`. Its arguments identify a workflow window alias and optional `keyword` and `searchRegion` options. A window alias must resolve to a handle already validated by the workflow engine. Arbitrary image paths and model paths are not accepted by this runtime operation.

The exact JSON field names should match the core OCR request/result types and be documented in the script API examples. An optional keyword filters detected text regions by recognized-text prefix; all matching regions are returned with full text and original bounds. It does not cause a second crop or OCR pass.

## Response

On success, return an object containing an ordered `candidates` array. Each candidate contains:

- recognized UTF-8 `text`;
- `image_bounds` and `screen_bounds` rectangles with integer physical-pixel coordinates;
- `recognition_confidence` and `detection_confidence` values.

Both script languages expose this as `automation.ocr(window, keyword=None, search_region=None)` in Python and `automation.ocr(window, keyword, search_region)` in Lua. The method returns the candidate array. Scripts may use the result in memory; normal logs must not include the recognized text.

## Failure and limits

- Unknown workflow alias, invalid options, unavailable OCR configuration, capture failure, inference failure, cancellation, and deadline expiration return explicit host errors.
- Successful recognition with no candidates returns an empty `candidates` array; it is not used to disguise a provider error.
- The operation inherits the current script step deadline and cancellation token. Existing host request/protocol size limits apply; OCR cannot block the worker indefinitely.
- OCR performs no desktop input and does not change window focus. Any subsequent script input remains subject to existing broker checks.
