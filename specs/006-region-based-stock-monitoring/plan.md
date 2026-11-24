# Implementation Plan: 看盘区域配置与定时识别

**Branch**: `006-region-based-stock-monitoring` | **Date**: 2026-11-23 | **Spec**: [spec.md](spec.md)

**Input**: `specs/006-region-based-stock-monitoring/spec.md`，参考 `docs/选定区域识别.md`，扩展现有 `stock_monitor` 独立应用。

## Summary

在现有 `stock-monitor` 中增加单窗口多区域工作台。用户选择当前窗口、基于客户区截图框选区域，配置区域采样间隔和可选表格字段模板，显式保存/打开版本化 JSON。运行时严格核验客户区物理像素宽高：只有与配置记录完全相同时才允许使用；窗口移动不影响，任何宽高变化均使整份配置不可运行，必须重新校准并保存。

识别服务对各区域截图调用已有 RapidOcrOnnx 文字框 provider，基于固定字段模板和几何规则生成普通文字或结构化表格结果；字段原文、类型化值、OCR 框/置信度和失败状态分开保留。QTimer 驱动会话调度，单区域同一时刻最多一个在途任务，工作线程完成捕获/OCR/解析后将有会话代号的结果返回 UI。配置的每日时段按本机时间生效，结束后不自动跨日运行。

## Technical Context

**Language/Version**: C++17；Windows 平台适配器；Qt Widgets 5.15.x；RapidOcrOnnx C++ 依赖及当前已验证的 Win32 动态 ONNX Runtime 包。

**Primary Dependencies**: 当前 `stock_monitor_core`、`StockWindowCapture`、`RapidOcrAdapter`；Qt Core/Widgets/Concurrent；项目私有 `nlohmann/json`；CTest。

**Storage**: 显式保存的独立版本化 JSON 配置文件；本期不持久化逐周期图像、OCR 结果、字段结果或数据库历史。

**Testing**: 便携 C++ 配置/JSON、几何裁剪、表格字段关联和类型转换单测；使用 fake clock/capture/OCR 的调度测试；Windows/Qt 人工验收；保留现有 OCR smoke test。

**Target Platform**: Windows 10+、Win32 目标、Visual Studio 2019/v142 或 Visual Studio 2022/v143、Qt 5.15 MSVC 套件。

**Project Type**: 现有独立 Qt Widgets 桌面应用 + 纯 C++17 区域配置/调度/表格解释模块 + Win32 可见客户区截图适配器。

**Performance Goals**: OCR/截图不阻塞 UI；每区域 OCR 在途数 ≤1；待执行任务有界；单个慢区域不阻止其他区域按时派发。

**Constraints**: 只读取桌面上可见目标客户区；不发送输入；不保存窗口句柄到 JSON；客户区宽高必须严格一致，禁止比例缩放；配置完整验证后原子替换；错误、缺失和低置信度不得被伪装成空值或成功；停止后不得让旧会话更新当前 UI。

**Scale/Scope**: 本地单用户、一个目标窗口、多采集区域；固定行列模板；本地时间、单日非跨午夜运行窗；不做自动页面理解、跨窗口会话、云端和采集历史库。

## Constitution Check

按仓库架构约束和规格核对：

- **独立进程与只读目标交互**: PASS — 仅扩展 `stock-monitor`，复用只读 capture；不链接输入控制器或工作流执行路径。
- **Core purity**: PASS WITH DESIGN REQUIREMENT — JSON/config 验证、区域映射、表格字段关联、值解析和调度策略放在不依赖 Qt/Win32/OpenCV 的 C++17 core；QTimer/窗口句柄/图像截屏留在 adapter/UI。
- **配置安全**: PASS — schema 版本化；完整解析校验后替换；区域坐标仅在客户区物理宽高严格匹配时生效；句柄只保留进程内。
- **生命周期和线程**: PASS WITH DESIGN REQUIREMENT — 会话代号和每区域 in-flight 状态阻止重复任务及过期回调；停止后未开始任务丢弃，在途调用明确完成或取消。
- **隐私**: PASS — 配置只由用户显式保存；截图和识别结果仅运行期持有，不自动落盘或外传。
- **OCR/颜色复用**: PASS — 复用已通过样例推理的 C++ TextBlock adapter 和原图颜色算法，不使用拼接文本猜坐标。
- **准确性边界**: PASS WITH CALIBRATION REQUIREMENT — 固定字段模板不自动推断页面语义；结构或格式不匹配时标为缺失/解析失败。

## Project Structure

### Documentation (this feature)

```text
specs/006-region-based-stock-monitoring/
├── spec.md
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── tasks.md                 # 由 /speckit-tasks 生成
```

### Source Code (repository root)

```text
stock_monitor/
├── core/
│   ├── StockModel.h/.cpp             # 既有图像/OCR/股票模型
│   ├── StockConfig.h/.cpp             # 既有股票颜色配置；只在确有共享验证时扩展
│   ├── RegionConfig.h/.cpp            # 配置模型、字段类型、客户区尺寸校验
│   ├── RegionConfigJson.h/.cpp        # 版本化 JSON parse/serialize、路径化诊断
│   ├── RegionGeometry.h/.cpp          # crop/clamp/坐标变换；尺寸严格匹配策略
│   ├── RegionTableParser.h/.cpp       # OCR block 到模板字段、行和值类型转换
│   └── RegionScheduler.h/.cpp         # 可注入 Clock/任务回调的纯状态调度逻辑
├── platform_win/
│   ├── StockWindowCapture.h/.cpp      # 扩展获取客户区尺寸及指定 client rect 可见截图
│   └── RapidOcrAdapter.h/.cpp          # 既有共享 OCR provider
├── ui/
│   ├── StockMonitorWindow.*           # 主工作台：配置文件、窗口、开始/结束和结果
│   ├── RegionSelector.*                # 客户区截图预览上的拖动选区交互
│   ├── RegionEditor.*                  # 区域间隔、类型和表格字段模板编辑
│   └── RegionSessionController.*       # QTimer、QtConcurrent worker、会话取消和结果投递
└── CMakeLists.txt                      # 注册 core、UI、平台源文件和测试依赖

tests/
├── test_region_config.cpp
├── test_region_config_json.cpp
├── test_region_geometry.cpp
├── test_region_table_parser.cpp
├── test_region_scheduler.cpp
└── manual/stock-monitor-windows-checklist.md
```

**Structure Decision**: 在 `stock_monitor/core` 增加不依赖 GUI 的可测试配置和解析能力，在 `platform_win` 只补充按客户区矩形捕获能力，在 `ui` 编排工作台和后台任务。不得将区域规则塞入现有 RPA `core/`，不得复用/链接 RPA 输入控制器。JSON 以 `.cpp` 私有包含 `third_party/nlohmann/json.hpp`，不把 JSON 库类型暴露到公开头文件。

## Phase 0: Design and Dependency Checks

详见 [research.md](research.md)。实现前先确认：

- 现有窗口捕获可以一次性返回当前客户区宽高、桌面原点及像素；区域捕获不会错误解释物理像素坐标。
- Qt 5.15 当前部署可以链接 `Qt5::Concurrent`，继续复用稳定的 RapidOcrAdapter。
- 既有 C++17 测试可通过 fake clock/capture/OCR 验证调度状态，不需要依赖实际桌面时间或窗口。

## Phase 1: Domain and Persistence

- [Data model](data-model.md) 定义 `RegionCaptureConfig`、窗口尺寸指纹、区域/字段模板、类型化值、会话和结果状态。
- JSON codec 必须将未知主版本、字段错误和无效尺寸路径化报告；加载到临时对象并完整校验后才提交。
- `QSaveFile` 或等价的同目录临时文件原子替换负责 UI 的显式保存；core codec 保持纯函数，不承担文件对话框。
- 字段解析先支持 text、int64、十进制和百分比；原文保留，失败状态不可产生类型化成功值。

## Phase 2: Region Capture and Table Interpretation

- 在 Win32 capture adapter 中新增读取客户区尺寸/rect 的结果类型；每次采集前重新读取客户区尺寸并与配置的 width/height 精确比较。
- 区域选择以客户区截图作为坐标画布；保存 pixel rect，禁止归一化映射。捕获时先取得完整客户区可见截图，再从内存图像裁切配置区域，避免重复 GDI 资源申请和坐标原点偏移。
- 区域 OCR 返回相对区域图像的文字框；表格解释器以字段 x 区间和配置行带分配 block。对跨列/多候选/越界情况返回 ambiguous 或 field error，不静默择一。
- 普通 OCR 模式保留全部 OCR block；若按规格选择启用字段颜色分析，使用区域原图和 block 坐标执行既有分析器。

## Phase 3: Session Scheduling and UI

- 以单一 session controller 和一个基础 QTimer 驱动调度；core scheduler 可注入时间源并返回每次应派发区域，不依赖 Qt。
- 开始时校验窗口仍有效、可见且非最小化，读取客户区尺寸并要求与配置匹配；若当前在日窗口外则显示等待状态。
- 每区域最多一个 in-flight 操作；各区域任务运行在有界线程池。正在执行时到期则记为 skipped，不入队，其他区域保持独立。
- 任务完成回 UI 时携带 session generation 和 region ID；停止/重新开始递增 generation，旧结果丢弃。
- 到结束时间停止派发并结束该 session；只有用户再次点击开始，才可等待下一日窗口。系统休眠/时间跳变恢复时重新核验当前时间和尺寸，越过结束边界则停止。
- 窗口变化、配置编辑/载入、区域编辑均要求停止会话；关闭应用请求停止，等待/取消所有 watcher 生命周期收敛。

## Phase 4: Verification

- CTest 覆盖 schema round-trip、无效 JSON 保留当前配置、区域边界及尺寸 mismatch、窗口移动不失效、任一维尺寸变化失效、字段类型格式和缺失状态。
- Fake clock 用短时间推进测试每日窗口、开始前等待、结束后不派发、跨日必须显式重启和时间边界。
- Fake worker 测试同区域不重入、周期跳过、不同区域并行、stop 清除 pending、旧 session 回调丢弃。
- Win32 手工清单覆盖框选/编辑/保存重开、窗口移动、改变客户区宽高、最小化/关闭/遮挡、DPI 和区域截图；记录基于可见屏幕像素的限制。
- 保持 OCR smoke test 运行，验证区域裁剪后文字框位于 crop bounds；既有看盘单测和 `stock_monitor_core_purity` 必须通过。

## Constitution Check (Post-Design)

- **Window geometry invariant**: PASS — 只接受 `currentClientWidth == configuredClientWidth && currentClientHeight == configuredClientHeight`；移动窗口不改变 client rect 坐标，改变任一维则整配置停止工作。
- **No guessed fields**: PASS — 每个 OCR block 依显式字段区间/行带匹配；重复匹配、跨边界和解析失败有独立状态。
- **No unbounded backlog**: PASS — 每区域单个在途任务；到期忙碌时计数/展示 skipped，不排队补跑。
- **No implicit persistence**: PASS — 配置只显式保存；运行期结果不自动记录图像或数据。
- **No target input**: PASS — capture path remains read-only and no input target is linked.

## Complexity Tracking

无架构原则豁免。新增配置 codec、固定表格 parser 和 session scheduler 分别承担版本验证、显式规则解释和有界多区域生命周期；不引入数据库、自动布局学习或后台常驻服务。
