# Quickstart: 独立看盘应用与股票字体颜色识别

## Environment and dependency gate

- Windows 10+、Visual Studio 2019 或 2022、Qt 5.15.x MSVC Widgets 和 Qt SQL/QSQLITE。
- CMake 3.16+、C++17。
- RapidOcrOnnx 本地 CPU 运行依赖和中文检测/方向分类/识别模型及字典。
- 用于验收的同花顺 9.x 截图及人工标注的代码、名称框和五色标签。

仓库已有 `third_party/rapidocr/win-CLIB-CPU-Win32/bin/RapidOcrOnnx.dll` 和 `third_party/rapidocr/models` 模型，可用于普通本地文本识别。但其导出的 C API（`OcrLiteCApi.h`）只返回拼接文本，不提供逐段 `TextBlock` 坐标与分数，因此不能用于看盘应用的行关联和字体颜色采样。看盘应用必须接入并验证有 `TextBlock` 几何与分数的本地 OCR 实现；不能仅因 DLL/模型文件存在就认为文字框接口可用。

**当前验证结果**：CMake 优先选择 `onnxruntime-share/windows-x86`，将其导入库用于链接，并将 `onnxruntime.dll` 与 `onnxruntime_providers_shared.dll` 部署到程序/测试目录；仅当动态包不存在时才回退静态包及工具集兼容检查。VS2019/v142 和 VS2022/v143 下 `stock-monitor` 均已成功链接。VS2019 下 `test_stock_ocr_runtime` 使用随仓库提供的 `images/1.jpg` 完成模型初始化、OCR 推理和文字框校验。RapidOcrOnnx 对带 padding 的边缘文字可能返回图像范围之外的点，适配器会将点裁剪到原图边界。不会启用扁平 CLIB 替代文字框。

## Prepare RapidOcrOnnx native dependencies

The existing `opencv-static/OpenCVWrapperConfig.cmake` is only a selector; it expects the actual OpenCV package (headers, libraries, and CMake package files) under the architecture-specific directory. The repository's `third_party/RapidOcrOnnx/BUILD.md` recommends the prebuilt packages from:

- OpenCV: <https://github.com/RapidAI/OpenCVBuilder/releases>
- ONNX Runtime: <https://github.com/RapidAI/OnnxruntimeBuilder/releases>

The vendored RapidOcrOnnx README records OpenCV 4.6.0 and ONNX Runtime 1.14.0. Select packages for Windows **x86/Win32**, and the **MD** CRT variant to match the normal VS2019 dynamic-CRT build. Keep the existing wrapper files and extract/merge the package contents so these paths exist:

```text
third_party/RapidOcrOnnx/opencv-static/windows-x86/
third_party/RapidOcrOnnx/onnxruntime-static/windows-x86/
```

The OpenCV directory must contain the package's `OpenCVConfig.cmake` (and its headers/libraries); the ONNX Runtime directory must contain its CMake package/config, headers, and libraries. Model files already exist under `third_party/RapidOcrOnnx/models`, so do not download them again. If only OpenCV source is desired, clone the matching 4.6.0 source and build/install it for Win32 first; source checkout alone does not satisfy `find_package(OpenCV)` or provide linkable libraries, and the generated install package must be wired into the wrapper's expected `windows-x86` path.

The RapidOcrOnnx build guide notes that Debug requires matching Debug builds of OpenCV and ONNX Runtime. Validate the first integration with Release packages/configuration.

After both dependency directories are populated:

```bat
cmake -S . -B build-win32 -G "Visual Studio 16 2019" -A Win32 ^
  -DCMAKE_PREFIX_PATH=D:/Qt/qt5.15.19/msvc2019 ^
  -DSTOCK_MONITOR_ENABLE_RAPIDOCR=ON -DWINAUTO_WERROR=ON
cmake --build build-win32 --config Release --target stock-monitor
```

## Configure and build

在 repository root 使用 VS2019 Win32 和 Qt 5.15 kit 配置。实际 target 名与路径在实现落地后以 CMakeLists 为准：

```bat
cmake -S . -B build-stock-monitor -G "Visual Studio 16 2019" -A Win32 ^
  -DCMAKE_PREFIX_PATH=D:/Qt/qt5.15.19/msvc2019 ^
  -DWINAUTO_ENABLE_OCR=ON -DWINAUTO_WERROR=ON
cmake --build build-stock-monitor --config Debug --target stock-monitor
```

运行 `build-stock-monitor\stock_monitor\Debug\stock-monitor.exe`（或 CMake 实际生成路径）。确认它作为单独进程启动；WinAuto UI/CLI 不必运行。当前可用操作为导入图像、枚举窗口及主动捕获可见客户区；OCR 依赖未补齐时界面会明确显示阻塞诊断，不能执行识别。

部署到其他电脑时，将 `stock-monitor.exe`、同目录下的运行库 DLL 和旁边的 `models` 文件夹一起复制。OCR 从运行中 EXE 所在目录的 `models` 子目录加载，不依赖构建机路径或进程当前工作目录；缺少模型时错误消息会显示实际检查路径和缺失文件。

如果 RapidOcrOnnx/OpenCV/ONNX Runtime 无法在上述工具链和架构下链接，先解决或记录精确的 native dependency 缺失，不要切换到只能返回拼接文本的 CLIB 并伪造框坐标。显式启用 `-DSTOCK_MONITOR_ENABLE_RAPIDOCR=ON` 会在依赖不完整时以可读错误终止配置。

## First local analysis

1. 启动看盘应用并确认 OCR runtime 状态为 ready。
2. 选择“导入截图”加载用户授权的同花顺涨幅列表截图，或选择目标窗口并手动请求一次 client-area capture。两条路径均不执行鼠标/键盘输入。
3. 点击“分析”，等待 OCR、股票行关联和颜色分析完成；如模型/截图处理失败，查看阶段化错误提示。
4. 检查原图叠加的 OCR 框和代码/名称行；选择股票名称区域复核 RGB/HSV、前景像素、主色比例、颜色置信度、OCR 置信度及分类。
5. 调整颜色/表格布局设置后重新分析当前图像；确认未知、低置信度和名称/代码冲突被标为待确认。
6. 按需明确选择 JSON 导出或保存分类快照。未选择保存时，关闭应用后不应留下本次图像/识别结果文件。

## Validation

- 先运行 `ctest --test-dir build-stock-monitor -C Debug --output-on-failure` 中的纯 core/config/SQLite tests。
- 在目标 Windows/Qt 环境运行 OCR integration smoke test 和 `tests/manual/stock-monitor-windows-checklist.md`。
- 使用隔离的标注集计算五类颜色总体准确率、名称区域准确率、代码/名称行关联率及低置信度误分类数量；按标注样本统计，不能以合成色块代替。
- 同时运行 `winauto-ui` 和 `stock-monitor`，确认 RPA 工作流/运行状态未改变，看盘应用没有向目标窗口发送输入。
- 保存同一股票的两个分类快照，重启应用并确认二者均可查询；用未授权自动保存/导出路径测试默认不持久化。

## Configuration and runtime data

- 颜色规则和表格布局使用应用专属配置路径，不使用 workflow JSON 或 RPA OCR 设置。
- SQLite 数据库使用应用专属用户数据目录；数据库初始化或写入失败必须显示错误。
- 日志中不输出完整截图像素或不必要的识别原文。
- JSON 导出可能包含 OCR 识别文本，只在用户显式选择导出目标后生成。
