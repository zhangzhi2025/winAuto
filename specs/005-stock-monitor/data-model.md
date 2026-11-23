# Data Model: 独立看盘应用与股票字体颜色识别

算法和业务数据在 `stock_monitor/core` 中使用 C++17 标准类型定义。Qt `QImage`、Win32 handles、OpenCV `cv::Mat`、RapidOcrOnnx objects 只存在于 UI/platform adapter，不跨入领域模型。分析结果在运行期持有；持久化只通过用户明确触发的 JSON 导出或快照保存完成。

## StockFontColor

`Unknown`, `Red`, `White`, `Orange`, `Yellow`, `Magenta`。`Unknown` 表示没有足够证据，不是 OCR 或平台层失败。

## BoardStage and FollowStatus

两个相互独立的枚举维度：

| Entity | Values | Mapping |
|---|---|---|
| `BoardStage` | `Unknown`, `FirstBoard`, `SecondBoard`, `ThirdBoardPlus` | Orange → FirstBoard; Magenta → SecondBoard; Red → ThirdBoardPlus |
| `FollowStatus` | `Unknown`, `Watchlist`, `NotFollowed` | White → Watchlist; Yellow → NotFollowed |

颜色分类不能自动推出另一维度：白/黄不会设置板级，红/橙/紫红不会设置关注状态。

## OcrTextBlock

| Field | Type | Meaning |
|---|---|---|
| `text` | UTF-8 string | OCR 完整识别文本 |
| `bounds` | integer rect | 相对于原始输入图像左上角的 axis-aligned bounding box |
| `quad` | fixed 4-point collection | RapidOcrOnnx quadrilateral，供调试叠加/转换 bounds |
| `detectionConfidence` | float in `[0,1]` | 文本检测 box score |
| `recognitionConfidence` | float in `[0,1]` | 基于 character scores 的稳定聚合分数；聚合定义必须固定并测试 |
| `order` | non-negative integer | provider 输出顺序，用于确定性排序 |

无框、坐标越界和非法置信度是 OCR adapter 的结果错误，不以可用空 block 伪造成功。行关联和颜色采样共用源图像坐标；所有 bounds 在采样前裁到图像区域，并记录裁剪/无效状态。

## FontColorConfig

| Field | Type | Meaning |
|---|---|---|
| `version` | string | 配置格式版本 |
| `enabled` | bool | 是否应用字体颜色分析 |
| `sampleMode` | enum | 首期为 `stock_name` |
| `fallbackMode` | enum | 首期支持 `stock_code` 作为名称缺失时的辅助采样策略；不得把辅助结果伪装成名称结果 |
| `minForegroundPixels` | positive integer | 最小有效前景像素数，设计起始值 8 |
| `minDominantRatio` | `[0,1]` | 主色像素最小占比，设计起始值 0.65 |
| `minConfidence` | `[0,1]` | 已知颜色最低置信度，设计起始值 0.80 |
| `minBrightness` / `minSaturation` | bounded integer | 前景及彩色像素阈值 |
| `whiteMinBrightness` / `whiteMaxSaturation` | bounded integer | 白色低饱和、高亮度条件 |
| `hueRanges` | color → ranges | 各颜色色相区间；重叠/非法范围须配置校验失败 |

数值默认值为调试起点。未完成人工标注样本校准前，配置不得描述为已达设计准确率。

## FontColorResult

| Field | Type | Meaning |
|---|---|---|
| `color` | `StockFontColor` | 主色分类或 Unknown |
| `dominantRgb` | RGB triple | 代表性主色 |
| `sourceBounds` | rect | 输入图像上的文字区域 |
| `foregroundPixelCount` | non-negative integer | 通过背景/亮度筛选的有效像素数 |
| `dominantPixelCount` | non-negative integer | 主色像素数 |
| `dominantRatio` | `[0,1]` | 主色/有效前景像素比 |
| `confidence` | `[0,1]` | 主色占比、颜色纯度和样本量组合后的评分 |
| `valid` | bool | 区域和采样量是否有效；`valid=false` 时颜色必须为 Unknown |

单像素无效或未知像素不计入五种颜色类别。前景像素不足、主色比例不足或置信度低于配置阈值必须输出 Unknown。

## StockRowCandidate

| Field | Type | Meaning |
|---|---|---|
| `rowBounds` | rect | 推导出的源图像行区域 |
| `codeBlock` / `nameBlock` | optional `OcrTextBlock` | 行中识别到的代码和名称 |
| `otherBlocks` | list of `OcrTextBlock` | 不作为颜色分类依据的其他列/文字 |
| `nameColor` / `codeColor` | `FontColorResult` | 名称主要样本、代码交叉核验样本 |
| `classification` | `StockClassification` | 分离的板级和关注属性 |
| `rowStatus` | enum | `Ready`, `Incomplete`, `Ambiguous`, `ColorConflict` |

行分配以 OCR box 的垂直位置/重叠及用户设置的列布局为依据。无法可靠识别/分组的块保留给调试视图，不猜测字段。

## StockClassification

| Field | Type | Meaning |
|---|---|---|
| `boardStage` | `BoardStage` | 红/橙/紫红映射或 Unknown |
| `followStatus` | `FollowStatus` | 白/黄映射或 Unknown |
| `tags` | list of stable strings | `BOARD_3_PLUS`, `BOARD_1`, `BOARD_2`, `WATCHLIST`, `NOT_FOLLOWED` |
| `colorRecognized` | bool | 至少一个有效颜色被判定 |
| `colorConfidence` | `[0,1]` | 用于主要名称颜色的置信度 |
| `classificationStatus` | enum | `Classified`, `Unknown`, `ColorConflict`, `NeedsReview` |

真实的代码/名称颜色同时有效且不一致时状态为 `ColorConflict`，保留两处原始结果与置信度。用户手动复核如在实现范围中应作为独立 annotation 字段保存，不能改写算法分数。

## StockMonitorConfig

用户级配置与 RPA workflow、RapidOCR locator 设置、Agent 和模型服务设置分离。

| Field | Type | Meaning |
|---|---|---|
| `schemaVersion` | string | 配置版本 |
| `fontColorRules` | `FontColorConfig` | 颜色判定规则 |
| `tableLayout` | `TableLayoutConfig` | 分辨率无关/相对列边界与行容差 |
| `ocr` | OCR settings reference | 模型/字典选择；原生路径由 adapter 验证 |

配置先解析并完整校验；只有成功后才替换当前有效配置。未知主要版本或无效阈值必须给出路径化诊断并保留上次有效值。

## AnalysisResult

一次用户触发的结果包含唯一分析标识、源类型（导入文件/窗口捕获）、采集时间、图像尺寸、非敏感来源标识、OCR blocks、行候选及整体状态。原始图像本身不隐式存入 SQLite。JSON 导出由用户指定路径后生成；它可能包含 OCR 文本，文件权限/目的地由用户负责。

## StockClassificationSnapshot (SQLite)

推荐列：

| Column | Type | Meaning |
|---|---|---|
| `id` | integer primary key | 独立快照 ID |
| `analysis_id` | text | 同一张图像/分析的关联标识 |
| `stock_code` | text | 股票代码；未识别结果不得写成假代码 |
| `stock_name` | text nullable | 识别到的名称 |
| `font_color` | text | 标准颜色 ID 或 `unknown` |
| `board_stage` | integer | 0 unknown、1 first、2 second、3 third-plus |
| `follow_status` | integer | 0 unknown、1 watchlist、2 not-followed |
| `color_confidence` | real | `[0,1]` |
| `source_page` | text nullable | 用户提供或选定的页面名称 |
| `captured_at` | ISO-8601 text | 本次采集/分析时间 |
| `row_status` | text | ready/incomplete/ambiguous/conflict/review |

保存操作使用事务；每次用户确认都新增记录，不按 `stock_code` upsert。首期不默认保存完整截图、所有 OCR 原文或个人账户内容。

## Analysis JSON Export

顶层包含 `schemaVersion`, `analysisId`, `capturedAt`, `source`, `imageSize`, `rows`, `unassignedBlocks`。每行输出代码/名称文本、source bounds、名称/代码颜色结果、两个独立分类维度、tags、置信度和状态。坐标均为原始图像像素。导出格式 versioned，绝不写入 workflow JSON。
