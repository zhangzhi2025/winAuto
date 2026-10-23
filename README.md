# WinAuto（rpaclient）

基于 C++ / Qt 的 Windows 桌面自动化工具。流程以 JSON 描述，窗口与动作分离，UI Automation 优先定位。
设计文档见项目中的《自动化设计.txt》；本文档只记录**已落地**的内容。

## 当前进度

| 阶段 | 状态 | 说明 |
|---|---|---|
| 0 工程基础 | ✅ 完成 | core 层全部落地并有测试；platform_win / ui 为占位 |
| 1 窗口管理 + 全局停止热键 | ✅ 已在 Windows 验证 | 窗口选择器 UI 在阶段 4 |
| 2 动作引擎 + `winauto-cli run` | ✅ 已在 Windows 验证 | 坐标点击、文本、组合键、信任确认、停止热键 |
| 3 UIA 定位 | ✅ Windows 编译通过 | 运行时行为按 `tests/manual/stage3-windows-checklist.md` 验证；控件树快照 + core 匹配/诊断/定位建议；`inspect` / `pick` |
| 4 流程编辑器 UI | ✅ 已在 Windows 验证 | 步骤列表/属性面板/窗口选择/控件拾取/单步调试/运行日志 |
| 5 图像识别 | 🟡 代码完成，待 Windows 编译与验证 | 灰度 ZNCC 模板匹配（core，已测试）+ GDI 截屏 + WIC 读图；`shot` / `find-image` 命令；清单见 `tests/manual/stage5-windows-checklist.md` |
| 6 截取模板 / 失败截图 / 剪贴板输入 / 条件与循环 | 🟡 代码完成，待 Windows 编译与验证 | 界面"截取模板"按钮 + core 模板评估；失败时截取目标窗口；剪贴板粘贴（备份/还原/不进历史）；`if` / `repeat` / `while`；清单见 `tests/manual/stage6-windows-checklist.md` |

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

执行流程（仅 Windows）：

```bat
winauto-cli run examples\notepad-coordinates-demo.json
winauto-cli run flow.json --yes --confirm-risks --hotkey ctrl+alt+f9
```

鼠标脚本示例见 `examples\mouse-demo.json`；`mouse_move` 与 `mouse_click` 步骤需要指定窗口别名和定位器，可通过 `winauto-cli validate` 校验、`winauto-cli run` 执行。

`run` 的流程：校验 → 展示**信任摘要**（目标程序、将输入的文本、将按的键；敏感文本不显示）→ 要求输入 `yes` 确认
（含高风险操作时还要输入 `RISK`）→ 注册紧急停止热键（**注册失败则拒绝运行**）→ 执行 → 汇总结果。

退出码：0 成功；1 失败（校验未通过、执行失败、被拒绝）；2 用法错误；3 当前平台不支持；4 被用户取消。

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
  InputPlan    文本/组合键 -> 按键序列、鼠标坐标归一化、窗口点解析（纯逻辑）
  Summary      运行前的信任摘要
  WorkflowEditor 编辑器视图模型：增删改移步骤、窗口别名重命名、撤销/重做、校验问题归属到步骤（UI 只转发）
  DebugGate    单步/暂停/继续/跳过/停止的线程安全闸门（引擎每步之前调用）
  GrayImage / TemplateMatch  灰度图与模板匹配（ZNCC，歧义检测）；PathSafety 模板路径越界检查
  UiaSnapshot  UI 树快照（纯数据）；UiaMatch 在快照上匹配、判唯一、失败诊断、生成定位建议；ControlType 控件类型表
  CancelToken  线程安全取消令牌，等待可被立即打断
  Logger       执行日志与敏感信息脱敏
platform_win/  Windows 适配器：窗口枚举/激活/提权检测、DPI、全局热键线程、SendInput、UI Automation（薄封装，逻辑在 core）
cli/           winauto-cli
ui/            Qt 5.15 Widgets：MainWindow、StepPanel、WindowPickerDialog、WindowsDialog、ElementPickerDialog、RunController（逻辑都在 core，这里只做界面）
schemas/       workflow.schema.json（draft-07，仅作文档和编辑器提示）
examples/      示例流程
tests/         单元测试、Fake 适配器、Schema / 纯净性检查
third_party/   nlohmann/json 3.11.3（仅在 core/*.cpp 内使用）
```

## 流程 JSON v1 要点

- `windows` 是别名表，步骤用 `window` 引用别名，因此支持多窗口流程。
- 定位策略字面值：`uia`、`window_point`、`screen_point`、`image`、`fallback`。
- `window_point` 基准是窗口 **client 区域**，物理像素；`anchor` 可选 `top_left`（默认）、`bottom_right`、`ratio`（0..1 比例）。
- 脚本专用鼠标动作：`mouse_move` 只移动指针，`mouse_click` 强制移动并真实单击；两者都用 `locator` 指定落点，可用 `window_point`、`screen_point`、UIA 或 image 定位器。它们由 `winauto-cli run` 执行，落点被其他窗口遮挡时拒绝操作；`mouse_click` 不走 UIA Invoke。
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

## 输入与点击的安全约定（阶段 2）

- **键盘输入前必须确认目标窗口在前台**，否则 `InputBlocked`，不会把字符打到别的窗口。流程里需要先有 `activate_window`。
- **点击前确认落点最上层窗口属于目标窗口**，被遮挡、落在弹出菜单/对话框上或窗口之外时拒绝点击。
- 点击要求进程为 Per-Monitor V2 DPI 感知（CLI 启动时自动设置），否则拒绝，避免偏位。
- `window_point` 基准是 client 区域；`ratio` 的 0 和 1 分别是第一个和最后一个像素；落在 client 区域外或窗口最小化 → `ElementNotFound`。
- 文本经严格的 UTF-8 校验后按 Unicode 事件发送（代理对正确处理）；`\n`/`\r\n` 发送为回车，`\t` 为 Tab；其他控制字符被拒绝。
- 组合键：修饰键先按、主键后按，反向释放；发送中途失败会尽力释放所有键，防止修饰键卡住。
- 敏感或运行时询问的文本不允许使用剪贴板输入（剪贴板历史/同步会留存），校验阶段直接报错。
- 向更高权限窗口发送输入会被系统**静默丢弃**且不报错（UIPI），所以引擎在第一步前的提权预检是必需的，不能绕过。
- `fallback` 会跳过"当前版本不支持"的策略（如阶段 2 的 `uia`），只有全部不支持时才报 `Unsupported`；歧义仍然直接失败。

## UI Automation 约定（阶段 3）
- 平台层只把目标窗口的控件树读成快照（控件视图，缓存请求，一次读取），匹配规则全在 core：
  `automationId` 精确且区分大小写；`controlType` 忽略大小写；`name` 按 `match`（exact / contains）。
- **多个候选默认报 `ElementAmbiguous`，并列出候选，绝不取第一个**；只有显式 `ambiguity: first` 才取第一个。
- 屏幕外或零尺寸的控件不参与匹配（点不到）。
- `click` / `set_value` 找到**被禁用**的控件时继续轮询到超时（按钮常在表单填完后才启用），
  超时报 `element found but still disabled`，绝不对禁用控件下手；`wait_for_element` / `assert_element` 不要求启用。
- 找不到时的错误信息带诊断：automationId 存在但类型不符、名字相近（建议 `match: contains`）、该类型有哪些控件、
  控件类型拼错、窗口完全没有 UIA 元素（建议改用 window_point）。
- 未知 `controlType` 在校验阶段就报错（`unknown_control_type`）。
- 元素引用带代数：重新读取控件树后旧引用失效并报错，不会误操作到别的元素。
- 命令：`inspect <进程> [标题] [--max N]` 输出缩进的控件树；`pick <进程> [标题] [--delay S]` 倒计时后取鼠标下的控件并输出可直接粘贴的 locator（优先 automationId，其次 name 并警告名称易变，都不唯一则建议 window_point）。
- 不可用时（COM 初始化失败等）uia 策略返回 `Unsupported`，fallback 链会跳过它。

## 图形界面约定（阶段 4）
- `winauto-ui [流程.json]`。左侧步骤列表（红 = 有错误，黄 = 有警告，悬停看原因），右侧属性面板，底部运行日志。
- 编辑规则全在 core 的 `WorkflowEditor`（有单元测试）：步骤 id 唯一；窗口别名重命名会同步所有引用；仍被引用的别名不能删除；全部操作可撤销/重做。
- 定位器在面板里以 JSON 编辑（支持 fallback），也可点"拾取控件…"：读取控件树 → 选中或"鼠标拾取" → 自动生成最稳的定位器；JSON 有误时不会提交半成品。
- 运行前一定弹出**信任摘要**（目标程序、将输入的文本、将按的键；敏感文本不显示），默认按钮是"取消"；高风险操作还需输入 `RISK`。
- 没有紧急停止热键（Ctrl+Alt+F10）就拒绝运行；运行在后台线程，界面保持响应。
- 调试：F5 运行全部 · Ctrl+F5 只运行选中步骤 · F11 从头单步 · F10 执行当前步 · F9 跳过 · F8 继续 · F6 暂停 · Shift+F5 停止。
  停在某一步之前时该行高亮；跳过的步骤在日志里标记。只运行选中步骤时，窗口预检只检查这一步用到的窗口。
- 运行时询问的敏感文本（`textFrom: prompt`）用密码框询问，不落盘。
- 保存使用原子写入（QSaveFile），不会写一半损坏原文件。

## 图像定位约定（阶段 5）
- 用法：`"strategy": "image"`，`template`（相对**流程文件所在目录**，只接受 .png / .bmp）、`minConfidence`（默认 0.9）、
  `searchRegion`（相对目标窗口客户区左上角，像素；缩小范围更快也更不易误匹配）、`ambiguity`（error | first）。
- 匹配：灰度 ZNCC（对整体亮度/对比度变化不敏感）。**多处达标默认 `ElementAmbiguous`，不猜**；`first` 取最靠上再靠左的一处。
  纯色（无对比度）的模板直接拒绝；`minConfidence < 0.7` 校验时警告。
- 命中点必须落在目标窗口自己身上（被别的窗口盖住时视为没找到）；点击仍会再做一次落点检查。
- 模板路径用规范化后的真实路径再检查一次，符号链接/联接指向目录之外会被拒绝；带透明像素的模板被拒绝；模板缺失是配置错误，
  不会被 fallback 悄悄跳过。
- 截屏受限：硬件加速/受保护内容可能截到黑块，此时请改用 uia 或 window_point。主题、缩放、字体变化会让模板失效，需要重新截取。
- 命令：`shot <进程> [标题] --out <file.png> [--region x,y,w,h]` 截模板；`find-image <进程> [标题] <模板.png> [--min 0.9] [--region x,y,w,h] [--first]` 试匹配并给出得分和窗口内坐标。
- 性能：模板较大时先在下采样图上搜索（对网格相位各做一份模板再精修），1080p 全窗口搜索 40x40 模板约 0.15 秒（容器内 Release）。

## 阶段 6 约定

**条件与循环**（流程 JSON 里的块语句，可嵌套，最多 8 层）
- `if`：`condition` 成立执行 `then`，否则执行 `else`（可省略）。`while`：每轮先判断条件，成立才执行 `body`。`repeat`：固定 `times` 次。
- 条件 `type`：`element_exists`（控件存在时成立）/ `element_missing`（不存在时成立），定位器和普通步骤一样（含 fallback、图像）。
  `timeoutMs>0` 表示最多等这么久让条件**变为成立**，超时按"不成立"处理；缺省只检查一次。
- **条件里定位出错不等于不存在**：歧义、窗口丢失、策略不可用都会让该块失败，不会悄悄走 else。
- `while` 必须写 `maxIterations`（≤1000）：跑满后条件仍成立 → `LimitExceeded` 失败，绝不无限循环。`intervalMs` 是每轮间隔，缺省 100 ms。
- 整个运行另有总量保护 `execution.maxStepExecutions`（缺省 10000，含块语句本身和循环里的每一次），超过即 `LimitExceeded`，**不受 onFailure 影响**。
- 块语句不支持 `expect` / `retry`（放在块内的步骤上）；`onFailure` 对块本身有效。步骤 id 在整个流程里唯一（含嵌套）。
- 运行范围、调试闸门、界面列表使用同一套**先序行号**（父步骤 → then/body → else）。只运行选中的嵌套步骤时，忽略外层条件与循环。
- 运行记录 `StepRecord.path` 形如 `S3#2/S4`（S3 循环第 2 轮里的 S4）；块语句的记录排在它的子步骤之前。

**失败自动截图**
- 需要同时满足：`execution.screenshotOnFailure=true`、调用方给了目录（界面：流程旁 `screenshots/<时间戳>/`；CLI：`--screenshot-dir`，缺省同界面）。
- 只截**目标窗口所在的屏幕区域**（所见即所得，能拍到盖在上面的弹窗），不截整个桌面；文件名 `fail-001-<步骤id>.png`；一次运行最多 20 张。
- **执行过敏感/运行时询问的步骤之后不再截图**（窗口里可能显示着内容）；校验器会给出提示。截图失败只记日志，不改变步骤结果。
- 截图是明文图片文件，可能含窗口里的任何信息；分享流程目录前请检查 `screenshots/`。

**剪贴板输入**（`input_text` 的 `method: "clipboard"`）
- 流程：保存剪贴板全部格式 → 写入文本（标记为不进入 Win+V 历史、不同步云剪贴板）→ 发送 Ctrl+V → 等 250 ms → 还原。还原一定执行（含失败、取消）。
- 若等待期间剪贴板已被别的程序/用户改动，则**不覆盖**，只记一条日志。无法无损保存的格式（如延迟渲染的句柄）会在日志里说明。
- 敏感/运行时询问文本**禁止**走剪贴板（`sensitive_clipboard`）；文本上限 1 MiB。目标程序的粘贴快捷键不是 Ctrl+V（如部分终端）时不适用。

**截取模板按钮**（属性面板的定位器 / expect / 条件旁）
- 先把本程序窗口隐藏、目标窗口切到前台并截取其 client 区域，再在截图上拖拽选区。保存前由 core 的评估器自检：纯色、太小、窗口内出现多处都会提示。
- 模板存为流程文件旁的 `templates/<步骤id>-N.png`（因此流程需要先保存）；自动填入 `image` 定位器，可勾选"限定搜索范围"生成 `searchRegion`。

## 已知限制

0. 弹出菜单、下拉框是独立顶层窗口：UIA 定位只读取目标窗口自己的控件树，读不到它们；坐标点击也会因"不在目标窗口上"被拒绝。需要时可把弹出窗口配成另一个窗口别名（用 titleContains 区分）。
   一次性发送的长文本无法中途取消。
1. **重试会重新执行动作本身。** 对 `input_text` 配置 `expect` + `retry` 时，若校验失败，文本会被再输入一次。
   对有副作用的动作，请谨慎配置重试。
2. **步骤超时无法抢占阻塞中的平台调用。** 引擎在等待和轮询处响应取消与超时；一次卡死的 UIA / Win32 调用需要平台层自己设置超时（阶段 1/3 处理）。
3. 图像定位依赖屏幕像素：主题/缩放/字体变化会使模板失效；窗口被遮挡时找不到。
4. 条件目前只有"控件存在/不存在"两种；没有变量、比较和 break/continue。`while` 条件超出上限一律判失败（暂无"到上限就正常退出"的选项）。
5. **阶段 5、6 的 Windows 代码（截屏、WIC 读图、图像定位、剪贴板、失败截图、截取模板对话框及界面改动）尚未在 Windows 上编译或运行**（开发环境是 Linux 容器），只用手写的 GDI/WIC 桩做过语法检查；阶段 1–4 的 Windows 部分已由你编译/验证。
   core、cli（非 Windows 分支）、测试已在 GCC 13 / clang 下通过，含 ASan / UBSan / TSan，`-Werror` 零警告。
6. 示例里的控件 `automationId` 只用于演示，必须用实际窗口探测确认；不同版本的记事本控件树不同。

## 测试

```
test_errcode / test_json / test_validator / test_state_cancel / test_logger / test_engine / test_window_hotkey / test_input_plan / test_uia_match / test_editor / test_debug_gate / test_template_match / test_path_safety / test_flow_control / test_template_assess
core_purity            core/ 不得引用 Win32 / COM / Qt 头文件
cli_validate_examples  CLI 冒烟
schema_examples        Schema 校验示例（需要 Python + jsonschema，没有则自动跳过）
```

测试使用仓库内的极简框架（`tests/testing.h`），不依赖 QtTest，因此 core 可在任何环境测试。
