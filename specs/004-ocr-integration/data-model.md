# OCR Integration Data Model

OCR types crossing the `core/` boundary use standard C++ types only. OpenCV matrices, RapidOcrOnnx objects and native model handles stay in `platform_win/`.

## OcrLocator

One strategy in the existing workflow `Locator` variant:

| Field | Type | Meaning |
|---|---|---|
| `text` | UTF-8 string | Required target text; whitespace-only queries are invalid. |
| `match` | enum | `exact` or `contains`; comparison operates on normalized UTF-8 text according to a documented stable rule. |
| `minConfidence` | number in `[0,1]` | Minimum recognition confidence for a candidate to be considered. |
| `searchRegion` | optional rectangle | Crop relative to target window client image, in physical pixels; absent means the full captured window client image. |
| `direction` | optional enum | One of `left`, `left_front`, `front`, `right_front`, `right`, `right_back`, `back`, `left_back`; candidate box center is classified relative to target client-area center. |
| `ambiguity` | enum | Defaults to `error`; `first` is explicit opt-in and follows a deterministic top-to-bottom, then left-to-right order after all filters. |

OCR locator JSON must round-trip with the existing JSON reader/writer and be checked by the central validator. Apply filters in this order: recognition confidence, optional keyword prefix, optional direction sector, then ambiguity handling. For direction classification, measure the candidate box center's angle from the client-area center, with 0 degrees to the right and increasing clockwise in screen coordinates. Divide the circle into eight 45-degree sectors centered on right, right-back, back, left-back, left, left-front, front and right-front. A sector includes its clockwise boundary and excludes its counter-clockwise boundary. A candidate centered exactly on the reference point belongs to no direction. Direction filtering does not rank candidates by distance.

## OcrRequest

A runtime provider request:

| Field | Type | Meaning |
|---|---|---|
| `windowHandle` | opaque core boundary integer | Existing engine-resolved target window; never serialized to workflow JSON or exposed to script workers. |
| `region` | optional rectangle | Bounded crop in target client-image physical pixels. |
| `recognizer` | configured selection | Selected model/character dictionary pair, loaded by provider configuration and not supplied as arbitrary script paths. |
| `minConfidence` | number | Threshold to include returned candidates. |
| `deadline` / cancellation | existing runtime controls | Provider stops before reporting results after cancellation/deadline. |

`windowHandle` is resolved from the workflow alias by the engine before a provider call. OCR itself is read-only and cannot issue desktop input.

## OcrTextCandidate

One recognized text block:

| Field | Type | Meaning |
|---|---|---|
| `text` | UTF-8 string | Recognized text; transient runtime value. |
| `imageBounds` | rectangle | Axis-aligned bounds of the OCR quadrilateral in captured image coordinates. |
| `screenBounds` | rectangle | Same bounds translated to desktop physical pixels. |
| `recognitionConfidence` | number | Confidence derived from the recognizer's character scores using the agreed aggregation rule. |
| `detectionConfidence` | number | RapidOcrOnnx text-box detection score. |
| `order` | integer | Stable reading/order index from provider output after documented sort if needed. |

Do not persist candidates or put their text in ordinary execution logs. The caller may intentionally consume the returned value in script memory.

## OcrProviderResult

The provider returns a collection of candidates or a typed failure. Distinguish:

- successful inference with zero detected text blocks;
- successful inference with blocks but zero query matches (`ElementNotFound` at locator selection);
- unique match;
- multiple matching candidates (`ElementAmbiguous` unless explicitly configured otherwise);
- unavailable/misconfigured models, capture failure, inference failure, cancellation and timeout.

Infrastructure errors are never translated to an empty candidate list.

## OcrRuntimeSettings

User-level settings stored independently of workflow JSON and existing model/Agent configuration:

| Field | Type | Meaning |
|---|---|---|
| `modelDirectory` | path | Directory containing default RapidOcrOnnx assets. |
| `detModelPath` | path | Detection ONNX model. |
| `clsModelPath` | path | Angle-classification ONNX model. |
| `recModelPath` | path | Selected language recognition model. |
| `keysPath` | path | Character dictionary paired with recognizer. |
| `threadCount` | bounded integer | CPU inference thread count. |

The UI may offer shipped model defaults and allow path overrides. Readiness means model initialization and a verification inference succeeded, not just that paths exist.

## Script OcrRequest / response

Python and Lua submit equivalent JSON through the existing host protocol. The request names a workflow window alias and bounded OCR options; it does not contain a native handle or arbitrary model path. The response is a JSON object with an ordered candidate array and the same text, image/screen bounds and scores as the core provider result. Host failures use the existing typed host error response.

An optional `keyword` filters candidates whose recognized text starts with the keyword. All matching regions are returned. The keyword does not alter candidate bounds or cause a second OCR pass.

Workflow locators may additionally specify the optional eight-way `direction` filter relative to the target window client-area center. It is applied after keyword filtering; scripts that request recognition without locator selection receive the complete candidate list and do not apply this locator-only filter.
