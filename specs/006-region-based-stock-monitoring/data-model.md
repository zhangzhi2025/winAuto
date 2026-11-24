# Data Model: 看盘区域配置与定时识别

区域配置、验证、调度策略和表格解释在 `stock_monitor/core` 使用标准 C++17 类型。JSON、Qt、Win32、RapidOcrOnnx 类型不得进入公开领域模型头文件。配置 JSON 和运行期识别结果是两个不同对象：配置由用户显式保存，运行结果默认只在内存中存在。

## RegionCaptureConfig

| Field | Type | Meaning |
|---|---|---|
| `schemaVersion` | string | 配置格式主版本；初版为 `"1"` |
| `configId` | string | 稳定配置标识 |
| `name` | UTF-8 string | 用户可读配置名称 |
| `targetWindow` | `WindowIdentityHint` | 可读窗口标题/进程名提示，不是授权，不含 HWND |
| `referenceClientSize` | `Size` | 创建区域时客户区物理像素宽高 |
| `referenceDpi` | optional integer | 选择时的 DPI 诊断信息；不用于自动缩放 |
| `dailyWindow` | `DailyWindow` | 本地开始/结束时间，`[start,end)` |
| `regions` | list of `CaptureRegion` | 一个目标窗口中的一个或多个区域 |

`referenceClientSize.width` 与 `.height` 均须为正。运行时客户区任一维不相等即整份配置无效，不能只停用越界区域或尝试比例映射。

## WindowIdentityHint

| Field | Type | Meaning |
|---|---|---|
| `title` | string | 保存时窗口标题，仅用于帮助用户重选 |
| `processName` | string | 可选进程名提示 |

重新加载配置时须由用户选取有效窗口。句柄只放在进程运行期 `CaptureSession`，不得序列化或依赖窗口标题相同来自动绑定。

## CaptureRegion

| Field | Type | Meaning |
|---|---|---|
| `id` | string | 配置内稳定唯一 ID |
| `name` | string | 区域显示名 |
| `rect` | `Rect` | 相对于客户区左上角的物理像素矩形 |
| `intervalMs` | positive integer | 周期采集间隔 |
| `enabled` | bool | 是否参与定时会话 |
| `mode` | enum | `ocr` 或 `table` |
| `table` | optional `TableTemplate` | `table` 模式必需 |
| `analyzeFontColor` | bool | 是否在原图文字框上运行现有颜色分析 |

区域 rect 必须正尺寸且完全包含于 reference client size。配置验证失败时给出例如 `regions[1].rect.width` 的路径化错误。

## DailyWindow

| Field | Type | Meaning |
|---|---|---|
| `start` | `HH:mm:ss` string | 本地每日开始时间 |
| `end` | `HH:mm:ss` string | 本地每日结束时间 |

要求合法 24 小时时间且 `start < end`。语义为本地时间 `[start,end)`；首版拒绝跨午夜区间。配置打开不会开始 session，结束后不隐式 arm 下一日。

## TableTemplate

| Field | Type | Meaning |
|---|---|---|
| `firstRowY` | non-negative integer | 首个数据行中心/顶部基准，相对 region 顶部 |
| `rowHeight` | positive integer | 数据行垂直容许带高度 |
| `rowPitch` | positive integer | 相邻行基准间距，须不小于 rowHeight |
| `fields` | list of `FieldDefinition` | 一行内的字段模板，至少一个 |

模板校验确保数据行带在区域高度内可解释，且每个字段列范围有效。若最后一行不完整，仍输出字段缺失状态，不读取区域外像素。

## FieldDefinition

| Field | Type | Meaning |
|---|---|---|
| `id` | string | 模板内唯一 ID |
| `name` | string | 用户定义的字段名 |
| `x` / `width` | positive integers | 相对区域左边缘的像素范围 |
| `valueType` | enum | `text`, `integer`, `decimal`, `percent` |
| `analyzeFontColor` | bool | 可选字段颜色分析；采样原始图像像素 |

列范围不得越过区域宽度。首期列范围必须互不重叠并按 x 升序，避免 block 归属歧义。字段 ID/name 的唯一性按同一模板校验。

## TypedFieldValue

| Field | Type | Meaning |
|---|---|---|
| `fieldId` | string | 对应 FieldDefinition |
| `rawText` | optional UTF-8 string | OCR 原文；缺失时为空 |
| `value` | typed variant | `string`, `int64`, finite `double` 或空 |
| `ocrConfidence` | optional float `[0,1]` | 来源 block 置信度 |
| `sourceBounds` | optional `Rect` | 区域截图坐标中的 OCR block bounds |
| `status` | enum | `parsed`, `missing`, `invalid_format`, `ambiguous`, `low_confidence` |
| `message` | string | 可诊断字段状态，不含猜测值 |
| `fontColor` | optional `FontColorResult` | 字段启用颜色分析时的结果 |

Parsing contract:

- `text`: 保留 OCR 字符串；按明确的空白策略去除首尾空白，不更改内部字符。
- `integer`: 严格十进制有符号 64-bit，无取整、指数和千分位猜测。
- `decimal`: 严格有限十进制值，允许可选正负号和一个小数点；不允许 locale 猜测、NaN/Inf。
- `percent`: 必须出现一个结尾 `%`，解析数值部分为百分比点值，如 `"12.50%"` → `12.5`；不除以 100。
- 一个字段的多个 OCR block 默认标记 `ambiguous`，不得隐式合并或选最近者。
- 解析失败时 `rawText` 保留、`value` 为空、`status` 非 `parsed`。

## RegionCaptureResult

| Field | Type | Meaning |
|---|---|---|
| `sessionId` | string | 进程运行期 session 标识 |
| `regionId` | string | 来源 CaptureRegion |
| `capturedAt` | local/ISO-8601 timestamp | 截图时间及偏移信息 |
| `clientSize` | `Size` | 捕获时实际客户区尺寸 |
| `imageSize` | `Size` | 区域截图尺寸 |
| `captureStatus` | enum | `success`, `window_unavailable`, `client_size_mismatch`, `capture_failed`, `cancelled` |
| `ocrStatus` | enum | `not_run`, `success`, `failed`, `cancelled` |
| `blocks` | list of `OcrTextBlock` | 区域本地坐标的原始 OCR 候选 |
| `rows` | list of `TableRowResult` | table 模式字段结果 |
| `skippedTicks` | non-negative integer | 区域忙碌时跳过的周期数 |
| `message` | string | 错误或状态原因；不写入持久化日志 |

成功且无文字与捕获/OCR 失败须使用不同状态。新周期失败时不能显示为本周期成功空结果；若 UI 保留上次成功结果，必须标注其采集时间和过期状态。

## TableRowResult

| Field | Type | Meaning |
|---|---|---|
| `rowIndex` | non-negative integer | 模板行序号 |
| `bounds` | `Rect` | 区域本地行带 |
| `fields` | list of `TypedFieldValue` | 按 FieldDefinition 顺序排列 |
| `status` | enum | `complete`, `incomplete`, `ambiguous` |

行状态不得覆盖单字段状态；一个字段失败不影响同行其他字段可用性。

## CaptureSession (runtime only)

| Field | Type | Meaning |
|---|---|---|
| `sessionId` / `generation` | string / integer | 过期任务隔离令牌 |
| `windowHandle` | native handle wrapper | 当前进程绑定的窗口句柄；绝不序列化 |
| `configSnapshot` | immutable `RegionCaptureConfig` | 开始时完整验证的配置快照 |
| `state` | enum | `stopped`, `waiting_for_daily_start`, `running`, `ended`, `failed` |
| `inFlightRegionIds` | set of IDs | 每个 ID 最多一个任务 |
| `skippedTicks` | map ID → count | 可观察的忙碌周期跳过数 |
| `cancelToken` | shared cancellation state | 手动停止/关闭取消标记 |

运行中不得原地更改 `configSnapshot`。用户编辑/加载新配置或切换窗口前须先结束 session。

## Version 1 JSON Example

```json
{
  "schemaVersion": "1",
  "configId": "a3f8f1b3-7af9-47ed-bfb3-f791ca9c1763",
  "name": "Tonghuashun rankings",
  "targetWindow": {
    "title": "同花顺",
    "processName": "hexin.exe"
  },
  "referenceClientSize": {
    "width": 1280,
    "height": 720
  },
  "referenceDpi": 96,
  "dailyWindow": {
    "start": "09:25:00",
    "end": "15:05:00"
  },
  "regions": [
    {
      "id": "gainers",
      "name": "今日涨幅排名",
      "rect": {
        "x": 20,
        "y": 80,
        "width": 480,
        "height": 520
      },
      "intervalMs": 1000,
      "enabled": true,
      "mode": "table",
      "analyzeFontColor": true,
      "table": {
        "firstRowY": 42,
        "rowHeight": 24,
        "rowPitch": 26,
        "fields": [
          {
            "id": "code",
            "name": "股票代码",
            "x": 0,
            "width": 76,
            "valueType": "text",
            "analyzeFontColor": false
          },
          {
            "id": "change",
            "name": "涨幅",
            "x": 300,
            "width": 80,
            "valueType": "percent",
            "analyzeFontColor": false
          }
        ]
      }
    }
  ]
}
```

The HWND, runtime session ID, current running state, screenshots and recognized result records are deliberately absent from the persisted config.
