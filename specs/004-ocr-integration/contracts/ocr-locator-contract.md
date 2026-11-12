# OCR Locator Contract

This contract adds an OCR strategy to the existing workflow locator union. OCR locators must flow through the same parser, validator, editor, engine, fallback and input-safety behavior as existing locators.

## Query semantics

- Required UTF-8 query text; whitespace-only queries are invalid.
- `exact` compares the entire recognized candidate text; `contains` searches within a candidate text.
- Apply one documented normalization rule consistently (at minimum valid UTF-8 and stable whitespace treatment); do not silently use locale-dependent matching.
- Candidates below `minConfidence` are excluded from matching.
- An optional keyword retains candidates whose recognized text starts with the keyword. An optional direction then retains candidates whose box center lies in that direction sector relative to the center of the target window client area. Apply filters in this order: confidence, keyword, direction.
- Direction values are `left`, `left_front` (upper-left), `front` (up), `right_front` (upper-right), `right`, `right_back` (lower-right), `back` (down), and `left_back` (lower-left). Measure angle from the client-area center with 0 degrees to the right and increasing clockwise in screen coordinates; sectors are 45 degrees wide and centered on those directions. A sector includes its clockwise boundary and excludes its counter-clockwise boundary. A candidate centered exactly on the reference point belongs to no direction.
- A query with no qualifying candidate is `ElementNotFound`.
- More than one qualifying candidate is `ElementAmbiguous` by default. `first` is an explicit opt-in and resolves deterministically top-to-bottom, then left-to-right.
- Direction is a filter, not a ranking: if multiple candidates remain in the selected sector, preserve existing ambiguity behavior; never silently choose the nearest candidate.
- OCR setup, model load, capture, timeout, cancellation and inference errors retain their own error and never become `ElementNotFound`.

## Geometry

The provider returns the OCR quadrilateral-derived image bounds and screen physical-pixel bounds. Image coordinates are relative to the captured target client image; screen coordinates are translated by the capture origin. Crops are expressed in the same client-image physical-pixel coordinate space and must be positive, non-empty, and fully within the image.

The existing input layer remains responsible for confirming that a click point is on the target window and not occluded. OCR candidate bounds alone do not authorize any input.

## Optional keyword filter

An optional keyword filters detected text regions by prefix of their recognized text. Return every region that matches and preserve each region's full recognized text and original bounds. If no region matches, locator selection reports `ElementNotFound`.

An optional direction filter may further narrow workflow locator candidates relative to the target window client-area center. It is applied after the keyword filter. All candidates in the requested sector are retained; if more than one remains, the locator's existing ambiguity policy applies. The direction filter does not specify a crop, offset, region size, second OCR pass, or nearest-candidate preference.

## Workflow integration

The OCR locator is a leaf strategy and may be used anywhere the existing model accepts a locator, including as a member of `fallback`. Fallback advances only when the OCR strategy reports a genuine `ElementNotFound`; ambiguity and provider failures stop the chain. JSON parsing, serialization, schema examples and semantic validation must agree on every OCR-specific field.
