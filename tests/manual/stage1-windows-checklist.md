# 阶段 1 Windows 手动验证清单

阶段 1 的 Win32 代码（`platform_win/`）是在没有 Windows 编译器的环境里写的，**从未编译或运行过**。
core 里的判断逻辑已有单元测试；这份清单验证平台层本身。请先确认编译通过，再逐项执行。

构建：见 README。下文 `cli` 指 `build\cli\Debug\winauto-cli.exe`。

## 0. 编译
- [ ] `cmake --build build --config Debug` 无错误。
- [ ] `ctest -C Debug` 全部通过（与 Linux 结果一致）。
- 若 `platform_win` 编译报错，把完整错误贴给我，通常是 SDK 版本差异，几分钟能修。

## 1. 窗口枚举
先打开：记事本、一个浏览器、资源管理器。
- [ ] `cli windows` 列出它们，进程名、PID、标题正确，中文标题不乱码。
- [ ] 最小化的记事本仍然在列表里（最小化不等于不可见）。
- [ ] 列表里**没有**无标题的辅助窗口、没有 UWP 被挂起的幽灵窗口。
- [ ] 末行显示 `this process is not elevated`（普通终端启动时）。

## 2. 唯一匹配
- [ ] `cli find notepad.exe` 只开一个记事本 → 输出这一个窗口。
- [ ] 再开第二个记事本 → 报 `WindowNotFound: window match is not unique ... add titleContains`，并列出两个候选。
- [ ] `cli find notepad "无标题"`（按实际标题调整）→ 重新唯一。
- [ ] `cli find 不存在的程序.exe` → `WindowNotFound`。
- [ ] 进程名大小写、是否带 `.exe` 都能匹配：`NOTEPAD`、`notepad`。

## 3. 激活
- [ ] 把记事本最小化，`cli activate notepad.exe` → 窗口恢复并到前台，输出 `activated`。
- [ ] 被其他窗口遮挡时同样能到前台。
- [ ] 若 Windows 拒绝前台切换（偶发），应输出 `InputBlocked: Windows refused ...`，而不是假装成功。
  （这是设计行为：不使用 AttachThreadInput 等绕过技巧。）

## 4. 提权检测（最重要的安全项）
- [ ] 以**管理员身份**运行记事本（右键 → 以管理员身份运行），再用**普通**终端：
  - `cli windows` 该窗口标记 `ELEVATED`。
  - `cli activate notepad.exe` → `WindowElevated`，**不会**尝试激活。
- [ ] 用**管理员**终端运行 `cli windows`，末行为 `this process is ELEVATED`，此时 `activate` 允许。
- [ ] 任务管理器、`services.msc` 等系统窗口应显示 `ELEVATED` 或 `(inaccessible)`，都按"拒绝"处理。

## 5. 全局停止热键
- [ ] `cli stop-hotkey-test`：输出 `Registered CTRL+ALT+F10`。切到**别的程序**（如记事本）后按 Ctrl+Alt+F10，
  终端输出 `Hotkey received`。
- [ ] 30 秒内不按 → 输出 `Hotkey was NOT triggered`，进程正常退出（无残留线程、无卡死）。
- [ ] 同时开两个 `stop-hotkey-test`，第二个报 `InputBlocked ... already used by another program`。
- [ ] 按住热键不放，只触发一次（MOD_NOREPEAT）。
- [ ] 换一个热键：`cli stop-hotkey-test ctrl+shift+f9` 可用；`cli stop-hotkey-test f10`
  报"needs at least one modifier"。

## 6. DPI
把显示缩放设为 150% 或 200%（或接第二块不同缩放的显示器）：
- [ ] `cli windows` 中记事本的坐标（若后续阶段打印）与实际像素一致，不出现 /1.5、/2 的偏差。
- [ ] 把窗口拖到另一块不同 DPI 的显示器后，`windows` 输出的位置仍是物理像素。
  （当前 CLI 未打印 bounds；阶段 2 的坐标点击会真正依赖它。若你想现在验证，告诉我，我加一个 `--bounds` 选项。）

## 7. 句柄复用
- [ ] 手动验证较难。依赖 `Win32WindowProvider::sameWindow` 对比 PID，已有逻辑审阅；
  可作为阶段 2 端到端测试（关闭目标窗口后继续执行）的一部分。
