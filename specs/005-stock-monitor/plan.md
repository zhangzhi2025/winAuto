# Implementation Plan: 独立看盘应用与股票字体颜色识别

**Branch**: `005-stock-monitor` | **Date**: 2026-11-22 | **Spec**: [spec.md](spec.md)

**Input**: `specs/005-stock-monitor/spec.md`，依据 `docs/rpa看盘设计.md`

## Summary

在现有 C++17 / Qt 5.15 Windows 工程中新增 `stock-monitor` 独立 GUI 可执行程序，作为单独进程启动，不嵌入 RPA、不依赖 RPA 生命周期，也不具备对目标窗口发送输入的业务能力。首期交付主动导入截图/捕获同花顺窗口、RapidOcrOnnx 文字框识别、股票行组装、字体颜色分类、调试校准、JSON 显式导出和 SQLite 分类历史快照。

算法和业务模型放在平台无关的 `stock_monitor/core`；Qt 界面、Win32 截图和 OCR 原生依赖分别留在专用 UI/platform 目标。不得把 RapidOcrOnnx 的 CLIB 拼接文本当作带坐标 OCR 使用。必须评估并验证以仓库 RapidOcrOnnx C++ `OcrLite::detect` / `OcrResult::textBlocks` 为基础的本地构建及其 OpenCV/ONNX Runtime 依赖；原生依赖或真实截图样本未就绪时，保留明确构建/验收阻塞，不用猜测文字框坐标或合成数据冒充真实准确率。

## Technical Context

**Language/Version**: C++17，Windows 平台适配器，Qt Widgets 5.15.x；RapidOcrOnnx 源码自身按 C++11 构建

**Primary Dependencies**: 仓库 `third_party/RapidOcrOnnx`、OpenCV、ONNX Runtime、Qt Widgets / Qt SQL(QSQLITE)、现有 Windows GDI 截图能力

**Storage**: 独立的看盘用户配置 JSON（颜色规则/表格布局）；SQLite 历史分类快照；分析图像和结果只在用户显式保存/导出时持久化

**Testing**: CTest 独立算法测试、人工标注图像回归测试、RapidOcrOnnx Windows 集成测试、schema/config/SQLite 测试、Qt 手动验收及与 RPA 同时运行测试

**Target Platform**: Windows 10+、Visual Studio 2019、Qt 5.15.x；首期按项目当前可用的 Win32 OCR 运行库部署环境验证

**Project Type**: C++17 领域/视觉库 + Windows OCR/截图适配器 + 独立 Qt Widgets 桌面应用

**Performance Goals**: 单个文字框颜色分析目标 ≤5 ms；截图、OCR 和批量分析不得阻塞 GUI 主线程；使用共享 OCR runtime，避免每个区域重复初始化模型

**Constraints**: 看盘应用必须是独立进程；不得调用 RPA 输入控制器或改变目标窗口；算法 core 不依赖 Qt/Win32/OpenCV/ONNX；只允许真实 OCR 文字框坐标映射；低置信度输出 Unknown，颜色冲突保留待确认；首期不实现行情网络、定时监控、热点/晋级策略、交易和 RPA IPC

**Scale/Scope**: 本地单用户、主动的一次性截图分析；首期聚焦同花顺 9.x 股票列表中股票代码/名称及其字体颜色，不包含其他行情页面的自动适配

## Constitution Check

`.specify/memory/constitution.md` 当前仍是空白模板，不含已批准的工程原则。按仓库架构约束和该功能规格进行检查：

- **独立进程和责任边界**: PASS — 通过新的 `stock-monitor` executable 目标交付；不在 `winauto-ui` 菜单内承载主界面，不依赖 RPA 进程或 workflow。
- **Core purity**: PASS WITH DESIGN REQUIREMENT — 股票领域模型、HSV/RGB 分类和行关联规则必须不依赖 Qt/Win32/OpenCV/ONNX；平台采集和 RapidOCR 实现留在适配层。
- **Native dependency feasibility**: BLOCKED UNTIL VALIDATED — 现有 RapidOcrOnnx DLL C API 仅返回拼接文本。C++ 源码的 `OcrResult::textBlocks` 提供区域和评分，但需确认 OpenCV/ONNX Runtime 依赖在目标 VS2019 Win32 构建环境中可用并可部署。
- **安全与隐私**: PASS — 应用只读截图，不发起输入/交易；截图和 OCR 文本不自动持久化、不发送网络；结果通过用户显式导出/保存操作持久化。
- **准确性和审计**: PASS WITH DATA REQUIREMENT — 低置信度/未知/冲突必须显式表达；真实颜色准确率须通过用户提供并人工标注的五色截图验证，不能用纯色合成样本代替。
- **测试和可维护性**: PASS — 颜色算法、股票行关联和分类器用离线单元测试；OCR/捕获与 Qt 生命周期通过 adapter/integration/manual 验证；RPA 原有目标和配置单独回归。

**Gate result**: 可进入设计和任务拆分；需将 native OCR build feasibility 和真实截图标注集列为实现早期验收 gate。

## Project Structure

### Documentation (this feature)

```text
specs/005-stock-monitor/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── tasks.md              # 由 /speckit-tasks 生成
```

### Source Code (repository root)

```text
stock_monitor/
├── core/
│   ├── StockModel.h                 # 类型、股票分类、分析快照
│   ├── FontColorAnalyzer.*          # 图像区域前景像素和 HSV/RGB 统计
│   ├── StockRowAssembler.*          # OCR 文本框到股票行的组合
│   ├── StockClassifier.*            # 颜色到板级/关注属性映射
│   ├── StockMonitorService.*       # 导入图像到分析结果的纯业务编排
│   └── StockConfig.*                # 颜色/布局配置及字段验证
├── platform_win/
│   ├── StockWindowCapture.*         # 目标窗口列表与只读 client-area 截图
│   └── RapidOcrAdapter.*            # C++ RapidOcrOnnx、文字框和置信度映射
├── repository/
│   ├── StockSnapshotRepository.*    # SQLite 历史分类快照
│   └── AnalysisJson.*                # 显式导出结构化分析结果
├── ui/
│   ├── StockMonitorWindow.*         # 图片导入/窗口捕获/状态及结果概览
│   ├── FontColorDebugWidget.*       # 原图、文字区域及阈值调试视图
│   ├── StockSettingsDialog.*        # 颜色规则和表格布局设置
│   └── SnapshotHistoryWidget.*      # 历史快照查询与保存
└── CMakeLists.txt                    # 可单独构建/运行的 stock-monitor executable

tests/
├── test_font_color_analyzer.cpp
├── test_stock_row_assembler.cpp
├── test_stock_classifier.cpp
├── test_stock_config.cpp
├── test_stock_snapshot_repository.cpp
├── fixtures/
│   └── stock-monitor/                # 经许可并人工标注的真实截图/区域标签
└── manual/
    └── stock-monitor-windows-checklist.md

config/
└── stock_monitor/
    ├── font_color_rules.json
    └── table_layout.json
```

**Structure Decision**: 新建 `stock_monitor/` 专属模块和 CMake executable；独立存储目录、QSettings organization/application ID、配置文件及 SQLite 数据库均使用看盘应用命名空间。通过共享源代码或只读采集 helper 重用已验证的 GDI/DPI 行为，但看盘应用的依赖图不应要求 `winauto-ui`、workflow Engine 或输入控制能力。若复用 `winauto_platform` 会把 RPA 输入/脚本/OCR CLIB 带入目标依赖，则应抽取最小只读 capture target 或实现专用薄适配器，不链接完整 RPA 运行路径。

## Phase 0: Research

详见 [research.md](research.md)。确认 C++ OCR 可用的文字框/置信度 API、native build 可行性、独立进程与只读截图边界、颜色采样和股票行关联策略、QSQLITE 部署以及真实样本验收前置条件。

## Phase 1: Design

- [Data model](data-model.md) 定义 OCR block、颜色分析结果、股票行、分类维度、用户配置和历史快照。
- [Quickstart](quickstart.md) 说明 VS2019/Qt 配置、独立目标构建、OCR runtime 校验、首次导入截图/采集窗口与结果持久化。
- JSON 导出格式在数据模型中定义；SQLite schema 和配置格式采用版本字段并有兼容/拒绝策略。
- 任务实现前先验证 RapidOcrOnnx C++ API 在目标架构能链接及运行一次 smoke inference；没有标注截图前，算法单测可推进，但真实准确率验收须保持 blocked。

## Constitution Check (Post-Design)

- **Process isolation**: PASS — 单独 `stock-monitor.exe`、独立 GUI 主窗口与设置/数据库命名空间，不和 RPA 自动 IPC。
- **Read-only target interaction**: PASS — 目标窗口只枚举/截图；无鼠标、键盘、工作流执行或交易写入路径。
- **Platform-independent decisions**: PASS — HSV 范围、前景像素统计、行组装和分类规则属于纯 C++17 core。
- **Failure semantics**: PASS — 捕获/OCR/配置/保存错误保留独立失败状态；未识别和低置信度为 Unknown，不混淆基础设施失败。
- **Persistence**: PASS — 分析图片和结果仅在明确保存/导出后持久化；SQLite 每次快照 append，不更新覆盖历史。
- **Native dependency and dataset gates**: OPEN — 必须在实际 Win32/VS2019 环境验证源构建和部署；98%/99%准确率需人工标注真实数据集后统计。

## Complexity Tracking

没有需要架构豁免的已批准原则。新增单独可执行目标是用户明确要求的进程边界；core/platform/UI/repository 分层服务于可测试算法、native dependency 隔离和本地历史存储，不得扩展成多进程服务或 RPA IPC 框架。
