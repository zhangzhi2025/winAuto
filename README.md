# WinAuto（rpaclient）

基于 C++ / Qt 的 Windows 桌面自动化工具。流程以 JSON 描述，窗口与动作分离，UI Automation 优先定位。
设计文档见项目中的《自动化设计.txt》；本文档只记录**已落地**的内容。

## 当前进度

| 阶段 | 状态 | 说明 |
|---|---|---|
| 0 工程基础 | ✅ 完成 | core 层全部落地并有测试；platform_win / ui 为占位 |
| 1 窗口管理 + 全局停止热键 | 🟡 代码完成，待 Windows 验证 | 判断逻辑已测试；Win32 层未编译过，见 `tests/manual/stage1-windows-checklist.md`。窗口选择器 UI 并入阶段 4 |
| 2 动作引擎 + `winauto-cli run` | 未开始 | 引擎已能用 Fake 跑通，缺平台适配器 |
| 3 UIA 定位 | 未开始 | |
| 4 流程编辑器 UI | 未开始 | |
| 5 图像识别 | 未开始（第二里程碑） | |

## 构建（Windows，Visual Studio 2019 / 2022 + MSVC）

前置：CMake ≥ 3.16，Qt **5.15.2**（msvc2019_64 套件，仅 ui 需要）。

```bat
cd E:\develop\rpaclient

:: VS 2022；VS 2019 把生成器换成 "Visual Studio 16 2019"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
cmake --build build --config Debug

cd build
ctest -C Debug --output-on-failure
```

- 找不到 Qt 时会自动跳过 `ui`，core、cli、测试照常构建。
- 也可以直接用 Visual Studio 的"打开本地文件夹"打开本目录（CMake 项目）。
- 非 Windows 平台只能构建 core、cli 和测试（CI 友好）。

校验流程文件：

```bat
build\cli\Debug\winauto-cli.exe validate examples\notepad-demo.json
```

Windows 上还有几个用于探测和验证的命令：

```bat
winauto-cli windows                         :: 列出可见顶层窗口（含 PID、是否提权）
winauto-cli find notepad.exe [标题片段]      :: 按流程里同样的规则查找唯一窗口
winauto-cli activate notepad.exe [标题片段]  :: 查找并激活，拒绝提权目标
winauto-cli stop-hotkey-test [ctrl+alt+f10] :: 验证全局停止热键
```

退出码：0 通过；1 失败；2 用法错误；3 功能未实现（`run` 将在阶段 2 提供）。

## 目录结构

```
core/          纯标准 C++17，不依赖 Qt / Win32（CTest 的 core_purity 强制检查）
  ErrCode      错误码与 Result<T>
  Model        领域模型（Locator 用 std::variant，每种策略只带自己的字段）
  Json         流程读写；错误带路径，如 steps[2].locator
  Validator    语义校验 —— 流程规则的唯一权威
  Engine       顺序执行、重试、超时、取消、失败策略、后置校验
  Interfaces   4 个接口：IWindowProvider / IElementLocator / IInputController / IClock
  WindowMatcher 窗口匹配与唯一性判定（纯逻辑）
  VirtualKey   按键名 <-> 虚拟键码，全局热键解析
  StopHotkey   IStopHotkey 接口 + 热键到取消令牌的守卫
  CancelToken  线程安全取消令牌，等待可被立即打断
  Logger       执行日志与敏感信息脱敏
platform_win/  Windows 适配器：窗口枚举/激活/提权检测、DPI、全局热键线程（薄封装，逻辑在 core）
cli/           winauto-cli
ui/            Qt 5.15 Widgets（目前仅占位窗口）
schemas/       workflow.schema.json（draft-07，仅作文档和编辑器提示）
examples/      示例流程
tests/         单元测试、Fake 适配器、Schema / 纯净性检查
third_party/   nlohmann/json 3.11.3（仅在 core/*.cpp 内使用）
```

## 流程 JSON v1 要点

- `windows` 是别名表，步骤用 `window` 引用别名，因此支持多窗口流程。
- 定位策略字面值：`uia`、`window_point`、`screen_point`、`image`、`fallback`。
- `window_point` 基准是窗口 **client 区域**，物理像素；`anchor` 可选 `top_left`（默认）、`bottom_right`、`ratio`（0..1 比例）。
- `fallback` 只在 `ElementNotFound` 时进入下一个定位器；`ElementAmbiguous` 直接失败。fallback 不能嵌套。
- 敏感文本：`"textFrom": "prompt"` 表示运行时询问，不写入文件；`"sensitive": true` 的文本在日志与报告中脱敏。
- `input_text.method` 为 `unicode`（默认）或 `clipboard`。通过 UIA ValuePattern 写值是独立的 `set_value` 动作。
- 每个步骤可带 `expect`（后置校验）、`retry`、`onFailure`，覆盖全局 `execution` 设置。
- 拼写错误的字段（如 `automationID`）会被当作错误报告，而不是被静默忽略。

## 引擎行为约定

| 错误码 | 是否重试 |
|---|---|
| `ElementNotFound`、`Timeout`、`ExpectationFailed` | 是 |
| `ElementAmbiguous`、`WindowNotFound`、`WindowElevated`、`InputBlocked`、`Unsupported`、`Cancelled`、`Internal` | 否 |

- 所有目标窗口在**第一步之前**全部解析并检查提权状态；目标权限高于本程序则拒绝运行。
- 窗口匹配不唯一时拒绝运行，不猜。
- `onFailure: continue` 时会继续执行后续步骤，但整个流程最终状态仍为 `Failed`，错误码取第一个失败步骤的。
- 含高风险操作（Alt+F4、Delete、Shift+Delete、Win+L）的流程，必须由用户确认（`RunOptions::confirmedRisks`）后才能运行。

## 窗口与安全约定（阶段 1）

- 枚举只包含可见、未被 DWM 隐藏、非工具窗口、**标题非空**的顶层窗口；自己的进程窗口永远被排除。
- 匹配必须**唯一**：同一进程的主窗口和对话框是不同窗口，按进程名匹配会因"不唯一"被拒绝，请加 `titleContains`。
- 提权检测是保守的：无法检查（打不开进程或令牌）一律按"已提权"拒绝。
- 窗口激活不假定成功，会确认窗口真的到了前台；被 Windows 前台锁拒绝时报 `InputBlocked`，不使用绕过技巧。
- 窗口句柄记录了 PID，句柄被系统回收给其他进程时会判为失效。
- 全局停止热键默认 `CTRL+ALT+F10`（Pause 键在许多笔记本上没有，F12 被调试器保留）。
  热键必须带修饰键。注册失败（被占用）时，调用方应拒绝启动流程。

## 已知限制

1. **重试会重新执行动作本身。** 对 `input_text` 配置 `expect` + `retry` 时，若校验失败，文本会被再输入一次。
   对有副作用的动作，请谨慎配置重试。
2. **步骤超时无法抢占阻塞中的平台调用。** 引擎在等待和轮询处响应取消与超时；一次卡死的 UIA / Win32 调用需要平台层自己设置超时（阶段 1/3 处理）。
3. `image` 定位器能被读取和校验，但会给出 `unsupported_in_mvp` 警告，运行时不可用（阶段 5）。
4. `screenshotOnFailure` 字段已进入模型，截图本身尚未实现。
5. **`platform_win/`、CLI 的 Windows 分支和 `ui/` 从未在 Windows 上编译过**（开发环境是 Linux 容器）。
   它们只用手写的 Win32 桩做过语法检查，签名是否与真实 SDK 一致尚未验证。
   core、cli（非 Windows 分支）、测试已在 GCC 13 / clang 下通过，含 ASan / UBSan / TSan，`-Werror` 零警告。
6. 示例里的控件 `automationId` 只用于演示，必须用实际窗口探测确认；不同版本的记事本控件树不同。

## 测试

```
test_errcode / test_json / test_validator / test_state_cancel / test_logger / test_engine / test_window_hotkey
core_purity            core/ 不得引用 Win32 / COM / Qt 头文件
cli_validate_examples  CLI 冒烟
schema_examples        Schema 校验示例（需要 Python + jsonschema，没有则自动跳过）
```

测试使用仓库内的极简框架（`tests/testing.h`），不依赖 QtTest，因此 core 可在任何环境测试。
