# Stock-monitor screenshot fixtures

No Tonghuashun screenshot is currently checked in. Do not copy screenshots containing account,
portfolio, or other personal information into this repository without explicit owner approval.
Until an approved and manually labeled set is available, SC-002 and SC-003 accuracy acceptance
remain blocked; generated color patches are unit tests only.

When an approved sample is supplied, store each image with a same-basename JSON annotation:

```json
{
  "schemaVersion": "1",
  "application": "Tonghuashun 9.x",
  "image": "sample-001.png",
  "imageSize": {"width": 1920, "height": 1080},
  "rows": [
    {
      "code": {"text": "600001", "bounds": [120, 310, 64, 22], "color": "red"},
      "name": {"text": "Example", "bounds": [220, 310, 88, 22], "color": "red"}
    }
  ]
}
```

Coordinates are half-open source-image pixel rectangles `[x, y, width, height]`. `color` must be
one of `red`, `orange`, `magenta`, `white`, `yellow`, or `unknown`. Add dataset provenance,
permission status, and a stable split identifier in the enclosing dataset manifest; do not include
account identifiers or unrelated screenshot regions.
