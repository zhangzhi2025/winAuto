# FontColorAnalyzer 正式设计规格说明书

业务规则已确认

Qt 5.15.2 / C++

实施版本 V1.0

根据你确认的同花顺 9.x 字体颜色规则，正式将颜色识别纳入 A 股智能看盘助手的基础能力。

这次有一个重要的设计调整：

字体颜色不仅是视觉属性，而是同花顺已经提供给我们的股票分类信息。

我们要把它转换成结构化的股票标签，供后面的龙头识别、连板梯队、自选股监控、自定义板块分析和市场热点分析使用。

## 1. 正式确定字体颜色业务规则

## 同花顺 9.x 股票字体颜色映射

红色

BOARD_3_PLUS

三板及以上

连板等级

白色

WATCHLIST

自选股

关注属性

橙色

BOARD_1

一板

连板等级

黄色

NOT_FOLLOWED

不在自己关注的股票

关注属性

紫红色

BOARD_2

二板

连板等级

正式业务映射：

| 字体颜色 | 业务含义      | 连板等级 | 股票标签           |
| ---- | --------- | ---- | -------------- |
| 红色   | 三板及以上     | ≥3   | `BOARD_3_PLUS` |
| 白色   | 自选股       | 未知   | `WATCHLIST`    |
| 橙色   | 一板        | 1    | `BOARD_1`      |
| 黄色   | 不在自己关注的股票 | 未知   | `NOT_FOLLOWED` |
| 紫红色  | 二板        | 2    | `BOARD_2`      |

这里需要明确一个业务边界：

- 白色代表自选股，但不能仅凭白色判断它是一板、二板还是三板。
- 黄色代表不在关注范围，也不能直接判断它没有连板。
- 红、橙、紫红可以根据你提供的规则直接映射连板等级。

因此，系统应该将「连板等级」与「关注属性」分成两个独立的数据维度，避免后续业务逻辑混乱。

# 2. FontColorAnalyzer 总体设计

## 2.1 模块定位

`FontColorAnalyzer` 是独立的图像分析模块。

它不负责 OCR，不负责股票代码识别，也不直接操作同花顺窗口。

它只负责：

1. 接收截图和文字区域坐标。
2. 提取文字前景像素。
3. 统计文字实际显示颜色。
4. 识别同花顺字体颜色类别。
5. 输出颜色类别和识别置信度。
6. 将识别结果交给股票分类模块。

## 2.2 完整识别架构

见docs\看盘架构.png

## 2.3 模块之间的职责

| 模块                 | 职责          |
| ------------------ | ----------- |
| Win32ScreenCapture | 获取原始截图      |
| RapidOCR           | 识别文字和文字框坐标  |
| FontColorAnalyzer  | 识别字体颜色      |
| StockRowAssembler  | 文字和颜色按股票行合并 |
| StockClassifier    | 生成股票分类和标签   |
| StockEngine        | 管理股票数据      |
| LeaderEngine       | 分析连板梯队      |
| WatchlistEngine    | 管理自选股       |
| HotspotEngine      | 分析热点板块      |

特别注意：颜色识别必须使用 OCR 文字框与原始截图进行关联，不能使用 OCR 识别后重新绘制的图片，否则会丢失原始字体颜色信息。

# 3. 颜色识别算法设计

这是整个模块最重要的技术部分。

## 3.1 为什么不能直接取文字框中心像素？

例如一个股票名称文字框：

```
┌──────────────────────┐
│                      │
│       农尚环境       │
│                      │
└──────────────────────┘
```

文字框中包含：

- 黑色背景。
- 彩色文字。
- 抗锯齿产生的过渡像素。
- 可能存在的选中背景。
- 其他 UI 装饰元素。

因此，不能直接使用：

```
QColor color = image.pixelColor(
    textRect.center()
);
```

中心点可能恰好落在背景上。

正确方式是：

提取文字框中的有效前景像素，统计主要颜色，并过滤背景和抗锯齿噪声。

## 3.2 推荐采用 HSV + RGB 双重识别

建议使用两步算法。

第一步：RGB 前景提取。

由于同花顺截图是深色背景，文字通常比背景明亮，可以先排除背景像素。

初始条件：

```
bool isForegroundPixel(const QColor& c)
{
    return c.value() > 100 &&
           (c.red() > 100 ||
            c.green() > 100 ||
            c.blue() > 100);
}
```

这个条件只用于初步筛选，不直接决定颜色类别。

第二步：HSV 颜色分类。

将有效像素转换为 HSV：

```
QColor::getHsv(
    int* h,
    int* s,
    int* v
);
```

采用以下分类策略：

| 颜色  | 初始 HSV 条件     |
| --- | ------------- |
| 红色  | H 约 0° 或 360° |
| 橙色  | H 约 15°～40°   |
| 黄色  | H 约 40°～70°   |
| 紫红色 | H 约 280°～350° |
| 白色  | S 较低、V 较高     |

这里的 HSV 色相范围仅作为初始参数，实际要根据同花顺截图采集到的像素校准。

尤其是白色与黄色，要重点区分亮度和饱和度。

## 3.3 颜色分类不能只使用单个像素

建议统计整个文字区域的有效像素。

例如：

```
文字区域：
红色像素：  850
橙色像素：   12
黄色像素：    8
白色像素：    5
背景像素： 3200
```

红色占有效前景像素的比例：

\\[ P\_{red}=\frac{850}{875}\times100\\% \\]

如果占比超过设定阈值，可以将该文字框识别为红色。

建议初始规则：

| 指标       | 初始值  |
| -------- | ---- |
| 最小有效前景像素 | 8    |
| 最小颜色占比   | 65%  |
| 颜色分类置信度  | 0.80 |
| 最大背景亮度干扰 | 配置化  |
| 最小文字框尺寸  | 配置化  |

这里的 65% 是初始建议值，后续通过实际截图集优化。

## 3.4 增加颜色置信度

建议计算：

\\[ Confidence = P\_{dominant} \times Q\_{purity} \times Q\_{sample} \\]

其中：

- \\(P\_{dominant}\\)：主要颜色像素占比。
- \\(Q\_{purity}\\)：颜色与目标颜色中心的接近程度。
- \\(Q\_{sample}\\)：有效采样像素数量的质量因子。

例如：

颜色识别结果示意

红色

0.98

橙色

0.03

黄色

0.01

仅为算法结果示意，并非实际截图检测结果。

当置信度不足时，返回 `Unknown`，不应该强行分类。

# 4. C++ 数据结构正式设计

建议不要简单地在 `StockQuote` 里面增加一个 `fontColor` 字段就结束。

后续还要支持连板梯队、股票标签、数据来源和置信度，因此需要将视觉识别结果与业务属性分离。

## 4.1 StockFontColor.h

```
#pragma once

#include <QColor>
#include <QString>

enum class StockFontColor
{
    Unknown = 0,

    Red,        // 三板及以上
    White,      // 自选股
    Orange,     // 一板
    Yellow,     // 非关注股票
    Magenta     // 二板
};

inline QString toString(StockFontColor color)
{
    switch (color)
    {
    case StockFontColor::Red:
        return QStringLiteral("red");

    case StockFontColor::White:
        return QStringLiteral("white");

    case StockFontColor::Orange:
        return QStringLiteral("orange");

    case StockFontColor::Yellow:
        return QStringLiteral("yellow");

    case StockFontColor::Magenta:
        return QStringLiteral("magenta");

    default:
        return QStringLiteral("unknown");
    }
}
```

## 4.2 StockClassification.h

```
#pragma once

#include <QStringList>

enum class BoardStage
{
    Unknown = 0,
    FirstBoard = 1,
    SecondBoard = 2,
    ThirdBoardPlus = 3
};

enum class FollowStatus
{
    Unknown = 0,
    Watchlist,
    NotFollowed
};

struct StockClassification
{
    BoardStage boardStage =
        BoardStage::Unknown;

    FollowStatus followStatus =
        FollowStatus::Unknown;

    QStringList tags;

    bool colorRecognized = false;

    double colorConfidence = 0.0;
};
```

这里将连板等级和关注状态拆成两个字段，是为了以后支持更多数据来源。

例如：

- 同花顺颜色识别连板等级。
- 本地自选股列表确认关注状态。
- 后续涨停板页面验证连板数量。

多来源信息可以互相补充，但不能无依据地互相覆盖。

## 4.3 FontColorResult.h

```
#pragma once

#include "StockFontColor.h"

#include <QColor>
#include <QRect>

struct FontColorResult
{
    StockFontColor color =
        StockFontColor::Unknown;

    QColor dominantColor;

    QRect sourceRect;

    int foregroundPixelCount = 0;

    int dominantPixelCount = 0;

    double dominantRatio = 0.0;

    double confidence = 0.0;

    bool valid = false;
};
```

## 4.4 StockClassifier.h

```
#pragma once

#include "FontColorResult.h"
#include "StockClassification.h"

class StockClassifier
{
public:

    StockClassification classify(
        const FontColorResult& result
    ) const;

};
```

核心映射：

```
StockClassification StockClassifier::classify(
    const FontColorResult& result) const
{
    StockClassification data;

    if (!result.valid ||
        result.confidence < 0.80)
    {
        return data;
    }

    data.colorRecognized = true;
    data.colorConfidence = result.confidence;

    switch (result.color)
    {
    case StockFontColor::Red:
        data.boardStage =
            BoardStage::ThirdBoardPlus;

        data.tags << QStringLiteral("BOARD_3_PLUS");
        break;

    case StockFontColor::Orange:
        data.boardStage =
            BoardStage::FirstBoard;

        data.tags << QStringLiteral("BOARD_1");
        break;

    case StockFontColor::Magenta:
        data.boardStage =
            BoardStage::SecondBoard;

        data.tags << QStringLiteral("BOARD_2");
        break;

    case StockFontColor::White:
        data.followStatus =
            FollowStatus::Watchlist;

        data.tags << QStringLiteral("WATCHLIST");
        break;

    case StockFontColor::Yellow:
        data.followStatus =
            FollowStatus::NotFollowed;

        data.tags << QStringLiteral("NOT_FOLLOWED");
        break;

    default:
        break;
    }

    return data;
}
```

# 5. FontColorAnalyzer 正式类接口

建议提供统一接口，避免 OCR、股票业务层直接操作图像颜色算法。

## 5.1 FontColorAnalyzer.h

```
#pragma once

#include "FontColorResult.h"

#include <QImage>
#include <QRect>
#include <QVector>

struct FontColorConfig
{
    int minForegroundPixels = 8;

    double minDominantRatio = 0.65;

    double minConfidence = 0.80;

    int minBrightness = 100;

    int minSaturation = 50;

    int whiteMinBrightness = 180;

    int whiteMaxSaturation = 55;
};

class FontColorAnalyzer
{
public:

    explicit FontColorAnalyzer(
        const FontColorConfig& config = {}
    );

    FontColorResult analyze(
        const QImage& image,
        const QRect& textRect
    ) const;

    QVector<FontColorResult> analyzeBatch(
        const QImage& image,
        const QVector<QRect>& textRects
    ) const;

    void setConfig(
        const FontColorConfig& config
    );

private:

    bool isForegroundPixel(
        const QColor& color
    ) const;

    StockFontColor classifyPixel(
        const QColor& color
    ) const;

    double calculateConfidence(
        double dominantRatio,
        int sampleCount
    ) const;

private:

    FontColorConfig m_config;
};
```

## 5.2 核心实现逻辑

建议实现步骤：

```
FontColorResult FontColorAnalyzer::analyze(
    const QImage& image,
    const QRect& textRect) const
{
    FontColorResult result;
    result.sourceRect = textRect;

    if (image.isNull() || textRect.isEmpty())
        return result;

    const QRect rect =
        textRect.intersected(image.rect());

    if (rect.isEmpty())
        return result;

    // 1. 遍历文字区域
    // 2. 排除背景像素
    // 3. 提取 RGB / HSV
    // 4. 对像素进行颜色分类
    // 5. 统计各颜色数量
    // 6. 获取主要颜色
    // 7. 计算颜色占比和置信度
    // 8. 输出识别结果

    // 这里省略具体统计容器实现

    return result;
}
```

正式实现时，建议使用固定大小的计数数组，而不是每个像素都动态分配对象。

例如：

```
int colorCounts[6] = {0};
```

对应：

```
0 Unknown
1 Red
2 White
3 Orange
4 Yellow
5 Magenta
```

这样对每秒多次截图的处理更加轻量。

# 6. OCR 文字区域与颜色关联

这里有一个很容易出现问题的地方：不能对整个股票行进行颜色识别。

例如：

```
代码       名称       最新价       涨幅
301313    君逸数码     59.16      +20.00%
```

涨幅列可能因为行情显示规则使用红色或绿色，这与股票连板属性没有关系。

因此，建议采用以下策略：

股票行颜色采样区域

代码

采样

名称

主采样

最新价

不采样

涨幅

不采样

示意表格。以股票名称为主要颜色来源，代码颜色用于交叉验证。

推荐优先级：

1. 股票名称文字颜色：主要分类依据。
2. 股票代码文字颜色：辅助验证。
3. 最新价、涨幅、成交额：不参与字体业务分类。

如果名称区域和代码区域的颜色识别结果不一致，应该标记为 `COLOR_CONFLICT`，不直接强行合并。

## 6.1 文字框坐标关联

RapidOCR 返回的文字区域一般包含：

```
struct OcrTextBlock
{
    QString text;

    QRect boundingRect;

    double confidence = 0.0;
};
```

通过坐标将文字框分配到股票行：

```
struct StockRowCandidate
{
    QRect rowRect;

    OcrTextBlock codeBlock;

    OcrTextBlock nameBlock;

    OcrTextBlock priceBlock;

    OcrTextBlock changeBlock;

    FontColorResult nameColor;

    FontColorResult codeColor;
};
```

最终组合：

```
OCR 识别股票名称
       +
OCR 文字框坐标
       +
原始截图颜色
       |
       v
StockRowCandidate
       |
       v
StockClassifier
       |
       v
StockClassification
```

## 6.2 行级颜色判定规则

建议制定：

| 名称颜色 | 代码颜色 | 结果          |
| ---- | ---- | ----------- |
| 红色   | 红色   | 三板及以上       |
| 红色   | 未识别  | 三板及以上，降低置信度 |
| 白色   | 白色   | 自选股         |
| 橙色   | 橙色   | 一板          |
| 黄色   | 黄色   | 非关注股票       |
| 紫红色  | 紫红色  | 二板          |
| 红色   | 橙色   | 冲突，待确认      |
| 未识别  | 未识别  | 未知          |

这里允许代码颜色缺失，但不建议在两个区域出现明确冲突时自动选取其中一个结果。

# 7. 颜色分类算法具体实现

下面给出一个适用于 Qt 5.15.2 的基础实现方向。

建议使用 `QColor` 完成 HSV 转换，不必为了颜色识别单独引入 OpenCV。

## 7.1 前景像素识别

```
bool FontColorAnalyzer::isForegroundPixel(
    const QColor& color) const
{
    if (color.alpha() < 180)
        return false;

    const int r = color.red();
    const int g = color.green();
    const int b = color.blue();

    const int brightness =
        qMax(r, qMax(g, b));

    return brightness >= m_config.minBrightness;
}
```

## 7.2 单像素分类

```
StockFontColor FontColorAnalyzer::classifyPixel(
    const QColor& color) const
{
    int h = -1;
    int s = 0;
    int v = 0;

    color.getHsv(&h, &s, &v);

    if (v < m_config.minBrightness)
        return StockFontColor::Unknown;

    // 白色：低饱和度、高亮度
    if (v >= m_config.whiteMinBrightness &&
        s <= m_config.whiteMaxSaturation)
    {
        return StockFontColor::White;
    }

    if (s < m_config.minSaturation || h < 0)
        return StockFontColor::Unknown;

    // 红色
    if (h <= 12 || h >= 345)
        return StockFontColor::Red;

    // 橙色
    if (h >= 15 && h <= 40)
        return StockFontColor::Orange;

    // 黄色
    if (h > 40 && h <= 70)
        return StockFontColor::Yellow;

    // 紫红色
    if (h >= 280 && h <= 340)
        return StockFontColor::Magenta;

    return StockFontColor::Unknown;
}
```

注意：这段代码是算法初版，不能直接作为最终生产阈值。实际开发必须通过截图样本校准。

尤其需要解决：

- 红色与橙色的边界。
- 黄色与白色的边界。
- 紫红色与红色的边界。
- 抗锯齿像素导致的颜色偏移。

## 7.3 进一步优化：颜色聚类

如果发现同花顺字体颜色并不是固定 RGB 值，建议增加颜色中心聚类。

例如：

```
红色中心：RGB(255, 0, 0)
橙色中心：RGB(255, 128, 0)
黄色中心：RGB(255, 255, 0)
白色中心：RGB(255, 255, 255)
紫红中心：RGB(255, 0, 255)
```

这些仅为示意色值，不代表实际同花顺配置。

可以使用 HSV 距离或归一化 RGB 距离进行分类，并通过配置调整颜色中心。

对于初期开发，我更推荐：

固定 HSV 范围 + 实际截图颜色样本校准 + 未知类别兜底。

先不要引入机器学习分类器，当前颜色类别数量有限，规则算法更容易解释和维护。

# 8. 配置文件正式设计

新增：

```
config/
└── stock_monitor/
    ├── font_color_rules.json
    └── table_layout.json
```

`font_color_rules.json`：

```
{
  "version": "1.0",
  "enabled": true,
  "sampleMode": "stock_name",
  "fallbackMode": "stock_code",
  "minForegroundPixels": 8,
  "minDominantRatio": 0.65,
  "minConfidence": 0.80,
  "white": {
    "minBrightness": 180,
    "maxSaturation": 55
  },
  "colors": [
    {
      "id": "red",
      "tag": "BOARD_3_PLUS",
      "meaning": "三板及以上",
      "hueRanges": [[0, 12], [345, 359]]
    },
    {
      "id": "white",
      "tag": "WATCHLIST",
      "meaning": "自选股"
    },
    {
      "id": "orange",
      "tag": "BOARD_1",
      "meaning": "一板",
      "hueRanges": [[15, 40]]
    },
    {
      "id": "yellow",
      "tag": "NOT_FOLLOWED",
      "meaning": "不在自己关注的股票",
      "hueRanges": [[41, 70]]
    },
    {
      "id": "magenta",
      "tag": "BOARD_2",
      "meaning": "二板",
      "hueRanges": [[280, 340]]
    }
  ]
}
```

这里将业务含义与颜色阈值分离。

以后如果你在同花顺中修改字体颜色，可以调整颜色参数，而不需要修改 `StockClassifier` 的业务代码。

# 9. 数据库扩展

建议在原有 `stocks` 表之外增加股票分类快照表。

```
CREATE TABLE IF NOT EXISTS stock_classifications (
    id INTEGER PRIMARY KEY AUTOINCREMENT,

    stock_code TEXT NOT NULL,

    font_color TEXT,

    board_stage INTEGER DEFAULT 0,

    follow_status INTEGER DEFAULT 0,

    color_confidence REAL DEFAULT 0,

    source_page TEXT,

    capture_time TEXT NOT NULL
);
```

字段含义：

| 字段               | 说明       |
| ---------------- | -------- |
| stock_code       | 股票代码     |
| font_color       | 原始字体颜色类别 |
| board_stage      | 连板等级     |
| follow_status    | 关注状态     |
| color_confidence | 颜色识别置信度  |
| source_page      | 来源页面     |
| capture_time     | 采集时间     |

`board_stage`：

| 数值 | 意义    |
| -- | ----- |
| 0  | 未知    |
| 1  | 一板    |
| 2  | 二板    |
| 3  | 三板及以上 |

`follow_status`：

| 数值 | 意义    |
| -- | ----- |
| 0  | 未知    |
| 1  | 自选股   |
| 2  | 非关注股票 |

建议保留历史快照，不要只更新股票基础表。这样后续可以分析连板梯队变化，例如：

```
09:35  一板
10:15  二板
11:20  三板及以上
```

但要注意，历史颜色快照只表示同花顺当时显示的分类。若要认定某只股票确实完成晋级，还应结合交易日、涨停状态和可靠的连板数据进行验证。

# 10. Qt 5.15.2 项目模块与目录调整

结合前面确定的 `stock_monitor` 目录，新增以下模块：

```
stock_monitor/
├── domain/
│   ├── StockFontColor.h
│   ├── StockClassification.h
│   ├── FontColorResult.h
│   └── StockRowCandidate.h
│
├── ocr/
│   ├── OcrAdapter.h
│   └── TableParser.h
│
├── vision/
│   ├── FontColorAnalyzer.h
│   ├── FontColorAnalyzer.cpp
│   ├── ColorClassifier.h
│   ├── ColorClassifier.cpp
│   ├── ColorConfig.h
│   └── ColorConfig.cpp
│
├── classifier/
│   ├── StockClassifier.h
│   ├── StockClassifier.cpp
│   ├── StockRowAssembler.h
│   └── StockRowAssembler.cpp
│
├── repository/
│   └── StockClassificationRepository.h
│
├── config/
│   └── font_color_rules.json
│
└── tests/
    ├── test_font_color_analyzer.cpp
    ├── test_stock_classifier.cpp
    ├── test_stock_row_assembler.cpp
    └── fixtures/
```

模块依赖必须遵循：

```
domain
  ↑
vision
  ↑
classifier
  ↑
collector
  ↑
service
  ↑
ui
```

其中 `vision` 不依赖 `StockEngine`，`StockClassifier` 不依赖 Qt UI。

这样未来其他页面也需要颜色识别时，可以直接复用。

# 11. TDD 开发任务拆分

建议将字体颜色识别独立为一个开发子项目，任务编号 T24～T34。

| 编号  | 任务           | 交付内容                      |
| --- | ------------ | ------------------------- |
| T24 | 定义颜色枚举及业务模型  | StockFontColor、BoardStage |
| T25 | 实现颜色配置加载     | JSON 配置解析                 |
| T26 | 实现前景像素提取     | 背景过滤                      |
| T27 | 实现 HSV 颜色分类  | 五种颜色识别                    |
| T28 | 实现颜色统计和置信度   | 主色及置信度                    |
| T29 | 实现批量文字框分析    | analyzeBatch              |
| T30 | 实现 OCR 文字框关联 | StockRowAssembler         |
| T31 | 实现股票分类映射     | StockClassifier           |
| T32 | 实现分类数据持久化    | SQLite Repository         |
| T33 | 实现颜色调试工具     | 颜色区域可视化                   |
| T34 | 完成真实截图测试     | 测试报告                      |

## 11.1 推荐开发顺序

1. T24～T25：数据模型和配置

   先固定颜色规则和业务含义，完成配置文件读取及单元测试。
2. T26～T28：颜色识别引擎

   使用实际截图，逐步校准 RGB、HSV、背景过滤、颜色统计。
3. T29～T30：OCR 集成

   把 OCR 文字框和原始截图关联起来，完成股票行颜色识别。
4. T31～T32：业务分类和存储

   输出股票连板等级、关注状态，保存分类历史。
5. T33～T34：调试与验收

   提供颜色调试界面，完成真实同花顺测试。

# 12. 测试和验收标准

## 12.1 颜色识别测试

必须为五种颜色分别建立测试样本。

| 测试编号  | 输入        | 预期结果           |
| ----- | --------- | -------------- |
| FC001 | 红色字体      | BOARD_3_PLUS   |
| FC002 | 白色字体      | WATCHLIST      |
| FC003 | 橙色字体      | BOARD_1        |
| FC004 | 黄色字体      | NOT_FOLLOWED   |
| FC005 | 紫红色字体     | BOARD_2        |
| FC006 | 黑色背景      | UNKNOWN        |
| FC007 | 灰色字体      | UNKNOWN        |
| FC008 | 混合颜色      | 按主色或 UNKNOWN   |
| FC009 | 文字区域过小    | UNKNOWN        |
| FC010 | 颜色置信度不足   | UNKNOWN        |
| FC011 | 代码和名称颜色冲突 | COLOR_CONFLICT |
| FC012 | 涨幅列红色     | 不应识别成三板及以上     |

## 12.2 正式验收指标

| 指标          | V1.0 目标 |
| ----------- | ------- |
| 五类颜色分类准确率   | ≥98%    |
| 股票名称颜色识别准确率 | ≥98%    |
| 代码与名称行关联正确率 | ≥99%    |
| 低置信度结果错误分类  | 0 次     |
| 颜色配置动态加载    | 支持      |
| 批量颜色分析      | 支持      |
| 单个文字框分析耗时   | 目标 ≤5ms |
| UI 主线程阻塞    | 0       |

性能目标以实际测试机器、截图分辨率和文字框大小为准。

特别强调：颜色分类准确率应该在人工标注的真实字体样本上统计，而不是使用人为生成的纯色图片作为唯一验收依据。

## 12.3 颜色调试界面

我建议专门做一个 `FontColorDebugWidget`。

字体颜色调试工具（界面设计示意）

301313  股票A  +20.00%

300812  股票B  +20.00%

301132  股票C  +20.00%

600340  股票D  +10.39%

301288  股票E  +10.03%

识别颜色

橙色

识别属性

一板

颜色置信度

98.5%

OCR 置信度

99.1%

模拟测试数据，不代表截图中实际股票的分类。

建议调试工具提供：

- 加载截图。
- 鼠标选择文字区域。
- 显示 RGB。
- 显示 HSV。
- 显示主要颜色。
- 显示颜色占比。
- 显示分类结果。
- 修改 HSV 阈值并立即重新测试。
- 导出当前颜色样本。

这个工具非常重要。后续如果同花顺升级或者修改配色，不需要重新开发算法，只需重新校准。

# 13. 与 A 股智能看盘业务的结合

颜色识别完成后，可以直接为后续业务提供基础数据。

例如：

连板梯队监控（示意）

# 12

一板

BOARD_1

# 6

二板

BOARD_2

# 3

三板及以上

BOARD_3_PLUS

股票关注属性

自选股

WATCHLIST

非关注股票

NOT_FOLLOWED

后续可以实现：

1. 自动统计一板、二板、三板以上股票数量。
2. 统计连板晋级梯队变化。
3. 发现自选股进入二板、三板梯队。
4. 发现非关注股票突然进入连板梯队。
5. 统计各热点板块中的连板股票分布。
6. 识别板块内高辨识度股票。

例如：

```
热点板块：人工智能

三板及以上：2只
二板：4只
一板：8只

自选股命中：3只

板块梯队：
一板 → 二板 → 三板及以上
```

这里的统计是后续功能设计示意。颜色分类只是行情界面所显示的分类，不能单独证明涨停、晋级或板块归属。

# 14. 最终实施决策

我建议正式确定以下技术决策：

| 设计项目   | 最终决定                 |
| ------ | -------------------- |
| 颜色分析   | 独立 FontColorAnalyzer |
| 颜色空间   | HSV 为主，RGB 辅助        |
| 背景过滤   | 前景像素提取               |
| 主要采样区域 | 股票名称                 |
| 辅助采样区域 | 股票代码                 |
| 分类算法   | 规则算法 + 颜色统计          |
| 阈值管理   | JSON 配置化             |
| 低置信度处理 | UNKNOWN              |
| 股票分类   | StockClassifier      |
| 数据存储   | SQLite 历史快照          |
| 开发方式   | SDD + TDD            |
| 开发语言   | C++                  |
| GUI 框架 | Qt 5.15.2            |

还有一个我认为值得特别重视的设计：

不要把颜色识别写成仅供同花顺涨幅榜使用的临时代码。

应该把它设计成一个通用的 `vision` 能力模块。未来如果需要识别同花顺其他排行榜、涨停板页面、板块成分股页面，都能直接复用。

## 15. 下一步建议

建议正式开始 `T24～T28`，但在编码前先做一次真实截图颜色标定。

你提供的这张截图可以作为第一份测试样本：

```
tests/fixtures/
    tonghuashun_9x_gainers_001.png
```

建议再提供一张同花顺正常交易时段的「今日涨幅排名」截图，最好能同时包含五种颜色，并且股票名称文字清晰。

第一阶段先交付一个独立的 FontColorDebugDemo：

- 加载截图。
- OCR 识别股票名称。
- 自动采样文字颜色。
- 显示五种颜色分类。
- 输出股票连板等级和关注属性。
- 导出 JSON 识别结果。

完成这个 Demo 并通过真实截图验收后，再将模块集成到现有 Qt RPA 工程。

这样可以先把颜色识别准确率验证清楚，再进入完整的智能看盘开发。