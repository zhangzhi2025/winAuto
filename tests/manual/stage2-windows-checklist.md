# 阶段 2 Windows 手动验证清单

阶段 2 的 Win32 代码（`Win32InputController`、`Win32PointLocator`、CLI `run`）**同样没有在 Windows 上编译或运行过**。
输入计划（文本 → 按键、组合键顺序、坐标换算、窗口点解析）和引擎的安全规则已有单元测试；这份清单验证平台层本身。
**建议在测试机或空白记事本上做，不要在有重要未保存内容的窗口上做。**

前置：阶段 1 清单已通过。以下 `cli` 指 `build\cli\Debug\winauto-cli.exe`。

## 0. 编译
- [ ] 重新构建并 `ctest -C Debug` 全部通过（应为 12 个测试项（阶段 3 起为 13 个，阶段 4 起为 15 个））。
- [ ] 若 `platform_win` 编译报错，把完整错误贴给我。

## 1. 校验与确认流程（不会操作任何窗口）
- [ ] `cli validate examples\notepad-coordinates-demo.json` → `OK`。
- [ ] `cli run examples\notepad-coordinates-demo.json`，在 `输入 yes` 处输入 `no` → 提示 `not confirmed`，什么都没发生。
- [ ] 摘要里能看到：目标进程 notepad.exe、将输入的文本、将按的键。

## 2. 紧急停止（先验证这个，再验证别的）
- [ ] 打开一个空白记事本。写一个 `wait` 10 秒的流程（或临时把 A2 的 durationMs 改成 10000），
  `cli run ... --yes`，运行中按 **Ctrl+Alt+F10** → 输出 `已取消`，退出码 4（`echo %errorlevel%`）。
- [ ] 占用热键：先开 `cli stop-hotkey-test`，再另开终端 `cli run ... --yes` → 拒绝运行，提示无法注册紧急停止热键。
- [ ] `--hotkey ctrl+alt+f9` 换热键后同样有效。

## 3. 坐标点击 + 文本输入（主流程）
空白记事本，默认大小。
- [ ] `cli run examples\notepad-coordinates-demo.json --yes`：
  记事本被带到前台，光标点在编辑区中央，输入：
  ```
  Hello 你好 😀
  第二行	缩进
  ```
  （第二行有一个 Tab 缩进；表情符号正确显示——说明代理对发送正确）然后全选。
- [ ] 终端逐步输出 `A1 ... ok`，最后 `完成：全部 5 步成功`，退出码 0。
- [ ] 换行是真实的换行（不是方块字符）。

## 4. 安全拒绝（最重要）
- [ ] **前台校验**：去掉 A1（activate），让别的窗口在前台，运行 → A4 报 `InputBlocked ... not in the foreground`，
  **没有任何字符被输入到别的窗口**。
- [ ] **遮挡校验**：运行前先用别的窗口完全盖住记事本的中心点，再运行（保留 A1 激活步骤——它会把记事本带到前台，
  所以此项需要另外构造：把 A1 删掉，手动让记事本在前台但用一个置顶的小窗口（如计算器置顶）盖住中心点）→
  点击步骤报 `InputBlocked ... not on the target window`，没有发生点击。
- [ ] **最小化**：最小化记事本、去掉 A1 → 点击步骤报 `window is minimized`，没有点击。
- [ ] **提权目标**：管理员身份运行记事本，普通终端运行流程 → 第一步之前就 `WindowElevated`，没有任何输入。
- [ ] **点击落点越界**：把 A3 的 ratio 改成 `x: 1.5` → 校验阶段就报错，什么都没运行；
  改成 window_point `x: 5000, y: 5000`（top_left） → 运行时 `ElementNotFound ... outside the window client area`。

## 5. DPI 与多显示器（点击是否点在正确位置）
- [ ] 显示缩放 100%、150%、200% 各跑一次主流程，点击都落在记事本编辑区中央（不偏位）。
- [ ] 窗口拖到副屏（若有，且缩放不同）再跑，仍然点在中央。
- [ ] 副屏在主屏**左侧**（虚拟桌面 x 为负）时同样正确——这是坐标归一化最容易出错的场景。
- [ ] 把记事本放大/缩小后再跑：`ratio` 定位仍在窗口中央，`top_left` 的固定偏移则保持不变。

## 6. 组合键
- [ ] 流程里 `CTRL+A` 确实全选了文本。
- [ ] 跑完后 Ctrl 键没有"卡住"（随便敲个字母，不会触发快捷键）。
- [ ] 带扩展键的组合：`["CTRL","HOME"]`、`["SHIFT","END"]`、`["ALT","F4"]`（会要求 RISK 确认！在空白记事本上测，
  它会关闭窗口——这正是要验证确认机制的原因）。

## 7. 运行时询问（textFrom: prompt）
用 `examples\fallback-prompt-demo.json` 的思路构造一个 `input_text` + `textFrom: prompt` 的流程（目标记事本）：
- [ ] 运行到该步骤时提示输入，**输入时终端不回显**。
- [ ] 输入的内容被正确键入记事本。
- [ ] 终端输出、失败信息里都没有出现输入的内容。
- [ ] 用 `echo x | cli run ...` 重定向输入 → 报 `needs an interactive console`，不会读到内容。

## 已知限制（预期行为，不算失败）
- 弹出菜单、下拉框、对话框是独立顶层窗口，点击它们的位置会被"不在目标窗口上"拒绝；它们需要 UIA（阶段 3）。
- 剪贴板输入未实现，会报 `Unsupported`。
- `uia` 定位器在本阶段返回 `Unsupported`；放在 `fallback` 里会被跳过，直接用坐标兜底。
- 一次性发送的长文本无法中途取消（停止热键在文本发送完成后生效）。
