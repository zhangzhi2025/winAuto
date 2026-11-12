# Implementation Plan: RPA OCR Integration

**Branch**: `004-ocr-integration` | **Date**: 2026-11-12 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `specs/004-ocr-integration/spec.md`

## Summary

为工作流定位器和 Python/Lua 脚本宿主增加本地 OCR 能力，使用 `third_party/RapidOcrOnnx` 的 `OcrLite::detect(const cv::Mat&, ...)` 接口识别目标窗口图像。OCR 调用集中在可注入的 core 接口，Windows 平台适配器负责窗口图像捕获、RapidOcrOnnx/OpenCV/ONNX Runtime 适配和候选框坐标转换；UI 管理独立 OCR 设置及只读预览。现有 `ElementNotFound` fallback、歧义失败和点击安全检查保持不变。

关键词为可选的文字区域过滤参数：对 RapidOcrOnnx 已识别出的每个文字区域检查其文本是否以关键词开头。工作流定位器可在关键词筛选后，按候选框中心相对目标窗口客户区中心的八方位筛选；如方向内仍有多个目标，继续按既有歧义策略处理，不按距离静默挑选。脚本识别返回仍提供全部关键词匹配候选。此行为不定义额外的锚点偏移、区域宽高或第二次裁图识别。

## Technical Context

**Language/Version**: C++17 core、Windows 平台适配器和 Qt 5.15 UI；RapidOcrOnnx 第三方源码为 C++11

**Primary Dependencies**: `third_party/RapidOcrOnnx` 的 `OcrLite`、OpenCV、ONNX Runtime；现有 core workflow locator、`IElementLocator`、脚本 worker/host protocol、Windows 截图和 Qt Widgets

**Storage**: OCR 模型目录、det/cls/rec 模型选择、字符字典及线程数作为独立用户设置保存；流程 OCR locator 的查询参数写入 workflow JSON；识别结果仅运行期存在，预览图不自动持久化

**Testing**: 现有 CMake/CTest 独立 C++ 测试；core fakes 验证 OCR locator 匹配/歧义/fallback 和脚本宿主错误；Windows adapter 使用固定图像及已配置模型进行 RapidOcrOnnx 集成测试；Qt 设置/预览验证非阻塞及状态呈现

**Target Platform**: Windows 桌面及 Windows CLI RPA 执行。Core 的接口和语义保持标准 C++17；实际 RapidOcrOnnx 适配依赖其 OpenCV/ONNX Runtime 原生库。

**Project Type**: C++17 core + Windows 平台适配器 + CLI + 可选 Qt Widgets 桌面客户端

**Performance Goals**: OCR 不在 UI 线程执行；单次识别共享已加载的模型实例，避免每个 locator 或每次脚本请求重新初始化；取消/流程截止时间应能停止等待并在 OCR 调用返回后阻止后续动作

**Constraints**: `core/` 不包含 Qt、Win32、OpenCV 或 ONNX Runtime 头文件；OCR 配置/推理错误不能折算为未找到；同一文本多重候选默认歧义；脚本只能通过受控 host broker 使用 OCR；识别框仍受现有窗口遮挡、前台、落点和风险确认规则约束；第三方 C API 的结果缓冲区实现不作为接口，优先使用 C++ API

**Scale/Scope**: 本地单机、CPU RapidOcrOnnx；中文和英文识别模型可配置选择；一轮定位/脚本请求只处理一个窗口图像，不包含 GPU、远程 OCR、训练模型或 OCR 自动写回流程

## Constitution Check

`.specify/memory/constitution.md` 仍是未填写模板，没有已批准的项目原则。按仓库架构与现有代码约束检查：

- **Core purity**: PASS WITH DESIGN REQUIREMENT — core 只定义标准 C++ OCR 请求/候选及注入接口；OpenCV 矩阵、模型生命周期和屏幕捕获留在 `platform_win/`。
- **安全和失败语义**: PASS — 只在过滤后无匹配时返回 `ElementNotFound`；方向筛选后多个候选仍歧义，不隐式按距离选取；不绕过现有遮挡/窗口安全检查；脚本请求经现有 host broker 并继承取消/截止时间。
- **可测试性**: PASS — 匹配和脚本结果可用 fake OCR provider 测试；实际推理由 Windows 集成测试单独验证。
- **敏感信息**: PASS — 识别文本仅为运行时数据，不进入常规日志、持久配置或 workflow，除非用户显式将值用于其脚本数据。
- **范围清晰度**: PASS — 关键词是对已有 OCR 文字区域的可选前缀过滤；无额外提取矩形或二次识别范围。
- **第三方构建**: NEEDS ENVIRONMENT VALIDATION — 仓库有模型文件和源码，但 OpenCV/ONNX Runtime CMake wrapper 所指向的编译库需在目标构建环境验证；依赖缺失必须给出清晰配置失败，不得悄悄把 OCR 编成不可用的占位实现。

**Gate result**: OCR locator、provider、Python/Lua host 和关键词前缀过滤均有明确语义，可进入设计与任务拆分。

## Project Structure

### Documentation (this feature)

```text
specs/004-ocr-integration/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
└── contracts/
    ├── ocr-locator-contract.md
    └── script-ocr-host-contract.md
```

### Source Code (repository root)

```text
core/
├── Model.h / Model.cpp                 # OCR locator 与候选/区域数据
├── Interfaces.h                        # 标准 C++ IOcrProvider 接口及输入图像语义
├── Json.cpp                             # OCR locator JSON 读写
├── Validator.cpp                        # 文本、区域和阈值检查
└── Engine.cpp                           # locator 注入、脚本 ocr.recognize host operation

platform_win/
├── Win32OcrProvider.*                   # 窗口截图、RapidOcrOnnx 调用、坐标换算
├── OcrRuntime.*                         # 模型文件加载、缓存和资源生命周期
└── CMakeLists.txt                       # OpenCV / ONNX Runtime / OCR 源文件链接及运行时部署

ui/
├── OcrSettings.*                        # 独立 QSettings 模型路径/语言配置
├── OcrSettingsDialog.*                  # 配置、状态、静态图片验证
├── OcrPreviewDialog.*                   # 目标窗口 OCR 结果只读呈现
├── StepPanel.*                          # OCR locator 编辑
└── RunController.*                      # 将 OCR provider 注入执行依赖

cli/
└── main.cpp                             # 运行配置载入及 OCR provider 注入

schemas/
└── workflow.schema.json                 # OCR locator 格式说明

tests/
├── test_ocr_locator.cpp                 # 匹配、歧义、区域裁剪/边界和错误映射
├── test_script_runtime.cpp              # 脚本 OCR host broker
└── manual/ocr-windows-checklist.md      # RapidOcrOnnx 原生运行时与窗口坐标检查
```

**Structure Decision**: OCR 推理作为 core 可注入能力，和已有窗口定位/脚本 broker 一样由平台层提供真实实现。`OcrRuntime` 持有 RapidOcrOnnx 模型，`Win32OcrProvider` 将窗口截图送入 `cv::Mat` 并将候选四边形变换到屏幕物理像素；core locator 负责稳定文本比较、区域规则、候选唯一性。Python 和 Lua 共用 `ocr.recognize` host operation 及 JSON 结果，不在两个 worker 内重复集成 OCR。UI 和 CLI 分别从独立配置加载运行时；工作流 JSON 只保存 locator 行为，不保存模型文件路径。

## Phase 0: Research

见 [research.md](research.md)。确认 RapidOcrOnnx C++ API 可直接接受内存图像；明确第三方构建子项目的全局 CMake 选项副作用、模型字典搭配验证及关键词前缀过滤规则。

## Phase 1: Design

- [Data model](data-model.md) 定义 OCR locator、OCR 识别结果/provider、脚本 OCR 请求和独立模型设置。
- [OCR locator contract](contracts/ocr-locator-contract.md) 定义工作流文本匹配、坐标、阈值、歧义和错误语义。
- [Script host contract](contracts/script-ocr-host-contract.md) 定义 Python/Lua 一致的宿主请求/返回结构和错误映射。
- [Quickstart](quickstart.md) 说明原生依赖构建、模型配置、locator/脚本测试及 Windows 手动检查。

## Constitution Check (Post-Design)

- **Core purity**: PASS — 所有 RapidOcrOnnx/OpenCV 依赖留在平台目标；公共核心只暴露普通 C++ 数据。
- **Locator consistency**: PASS — OCR 作为既有 `Locator` variant，通过同一 JSON/validator/editor/engine 路径使用；关键词及八方位方向按固定顺序筛选，fallback 仅对 `ElementNotFound` 前进。
- **Script parity**: PASS — Python 和 Lua 使用同一 core broker 和结构化结果格式，不向 worker 暴露原生句柄。
- **Input safety**: PASS — OCR 只提供候选框，不授予输入权限；既有窗口和落点检查仍是最后防线。
- **Keyword/direction filtering**: PASS — 关键词筛选文本前缀，工作流 locator 再按窗口中心八方位筛选；仍有多个候选时沿用歧义策略，不引入隐式最近目标行为。
- **Deployment**: NEEDS WINDOWS BUILD — OpenCV/ONNX Runtime 的目标架构、CRT 和动态库部署须通过 Windows 构建/手动 checklist 证实。

## Complexity Tracking

没有需要架构豁免的扩展。标准 C++ provider 接口是为保持 core 纯净并让脚本/locator 共用同一推理实现所必需；OCR 运行时和 native dependencies 不进入 UI 或脚本 worker。
