# 阶段 3 Windows 手动验证清单（UI Automation）

`platform_win/UiaSession.cpp`、`Win32UiaLocator.cpp` 和 CLI 的 `inspect` / `pick` **没有在 Windows 上编译或运行过**，
只用手写的 COM 桩做过语法检查（真实 `UIAutomation.h` 的签名未验证）。匹配、唯一性、禁用控件处理、诊断、定位建议已有单元测试。
**用空白记事本和计算器验证，不要碰有未保存内容的窗口。** 以下 `cli` 指 `build\cli\Debug\winauto-cli.exe`。

## 0. 编译
- [ ] 重新构建，`ctest -C Debug` 全部通过（应为 13 个测试项（阶段 4 起为 15 个））。
- [ ] 若 `platform_win` 或 `cli` 编译/链接报错（尤其 `UIAutomation.h`、`wrl/client.h`、`__uuidof`、链接 ole32/oleaut32/uuid），把完整错误贴给我。

## 1. inspect
- [ ] `cli inspect notepad.exe` → 输出缩进的控件树，末尾 `N elements`，有 Edit（文本区）控件。
- [ ] 把 `[Edit]` 行的 automationId / name 记下来（不同版本的记事本不同，Win11 新版与旧版差异很大）。
- [ ] `cli inspect calc.exe`（或 `CalculatorApp`，进程名以 `cli windows` 为准）→ 能看到按钮，且有 automationId（如 `num1Button`）。
- [ ] `cli inspect notepad.exe --max 5` → 只输出 5 个元素并注明截断。
- [ ] 对一个管理员权限运行的窗口 → 拒绝，提示 WindowElevated。

## 2. pick
- [ ] `cli pick notepad.exe --delay 5`，倒计时内把鼠标放到文本区 → 输出 `[Edit]` 与可粘贴的 locator JSON。
- [ ] 计算器里 pick 一个数字按钮 → 建议里是 `automationId` + `controlType: Button`，没有"名称易变"的警告。
- [ ] pick 一个没有 automationId 的控件 → 回落到 name，并有"名称随语言和版本变化"的警告。
- [ ] 鼠标不在目标窗口上时 pick → 报 "not in the window's control tree"，不崩溃。

## 3. 流程里的 UIA 定位
- [ ] 把 pick 出来的 locator 写进一个 `set_value`（文本区）流程，`cli run ... --yes`：文本被写入，**没有**用键盘模拟（可观察到文本一次性出现）。
- [ ] 计算器流程：`click` 数字键 → `wait_for_element`/`assert_element` 结果显示区。数字键应走 Invoke（点击时鼠标不动）。
- [ ] `click` 配 `"mode": "point"` → 鼠标真的移动并点击。
- [ ] 故意写错 automationId → 失败信息里有 "no element has automationId ..." 以及该类型的控件列表。
- [ ] 故意写错 controlType（如 `Buton`）→ `cli validate` 就报 `unknown_control_type`。
- [ ] 一个 locator 匹配到多个控件（如只写 `controlType: Button`）→ `ElementAmbiguous` 并列出候选，**没有**点击任何东西。
- [ ] 禁用控件：找一个灰掉的按钮 → 等到超时，报 `found but still disabled`，没有点击。

## 4. fallback
- [ ] `fallback`：第一项写错的 uia，第二项 `window_point` → 回退成功，日志里能看出第二项生效。
- [ ] 取消：运行中按 Ctrl+Alt+F10，对大控件树的窗口（浏览器、Office）也能及时停止。

## 5. 性能与稳定性
- [ ] 对浏览器/Office 这类控件很多的窗口 `inspect`：记录耗时（超过几秒请告诉我；快照一次最多 5000 个元素）。
- [ ] 连续 `inspect` 10 次，进程内存没有明显增长；程序退出时无崩溃。

## 请回报
编译错误原文；`inspect` 对哪些程序读不到元素；单次读取耗时；任何和上面预期不符的现象。
