# 阶段 5 Windows 手动验证清单（图像定位）

`platform_win/ScreenCapture.cpp`、`ImageFile.cpp`（WIC）、`Win32ImageLocator.cpp` 和 CLI 的 `shot` / `find-image` **没有在 Windows 上编译或运行过**，
只用手写的 GDI/WIC 桩做过语法检查。模板匹配算法（ZNCC、歧义、平坦模板、粗到精）、路径越界检查已有单元测试。
**用空白记事本和计算器验证，不要碰有未保存内容的窗口。** 以下 `cli` 指 `winauto-cli.exe`。

## 0. 编译
- [ ] 重新构建，`ctest -C Debug` 全部通过（应为 17 个测试项）。
- [ ] 若 `platform_win` 编译/链接报错（`wincodec.h`、`windowscodecs`、`CreateDIBSection`、`BitBlt`……），把完整错误贴给我。
- [ ] `winauto-ui` 仍能编译（`RunController` 的构造参数有改动）。

## 1. 截取模板
- [ ] 打开记事本，保持在最前、不被遮挡。`cli shot notepad.exe --out assets\text-area.png` → 提示 `saved WxH ...`，用看图软件打开确认是记事本的客户区。
- [ ] `cli shot notepad.exe --region 0,0,120,40 --out assets\corner.png` → 只截左上 120x40（相对客户区），图像内容对得上。
- [ ] 窗口最小化时 `shot` → 提示 "cannot read the window area"，不崩溃。
- [ ] 管理员窗口 → 拒绝（WindowElevated）。

## 2. find-image（最重要）
- [ ] 窗口保持原样：`cli find-image notepad.exe assets\corner.png` → `found: score 1.000...`，`window-relative (0,0)`。
- [ ] 在 `--region 0,0,300,200` 内重复，结果相同；`--region 400,300,50,50`（不含目标）→ 报 ElementNotFound，信息里有 best match score。
- [ ] 移动窗口后再运行 → 窗口相对坐标不变（区别于屏幕坐标）。
- [ ] 把记事本拖到副屏 / 改 125%、150% 缩放后各试一次：仍能找到（缩放变了模板像素会变，这时**重新截模板**才是正确做法；
      如果缩放不变但找不到，请告诉我）。
- [ ] 用另一个窗口盖住记事本中的目标位置 → 报 "covered by another window"，不是匹配成功。
- [ ] 同一图案出现两处（如在记事本里输入两行相同文字，截其中一个字做模板）→ `ElementAmbiguous`，列出位置；加 `--first` 取最靠上的一处。
- [ ] 纯色模板（截一块空白区域）→ 报 "no contrast"。
- [ ] `--min 0.5` 时能否匹配到不该匹配的东西？（记录一下，用于确认默认 0.9 合适。）

## 3. 流程里的图像定位
- [ ] `cli run examples\image-fallback-demo.json`：先把 `assets\notepad-text-area.png` 放到示例同目录（用 `shot` 截）。
      第一项 uia 若找不到会回退到 image；确认日志/输出里能看出最终用了哪一项。
- [ ] 删掉模板文件再运行 → 明确提示 "template file not found"，**不会**静默回退到 window_point（配置错误应当失败，而不是被跳过）。
- [ ] 把 `assets` 换成指向别处的符号链接/联接（`mklink /J`）→ 提示 "outside the workflow directory"。
- [ ] 带透明像素的 PNG 模板 → 提示 "has transparent pixels"。
- [ ] 运行中按 Ctrl+Alt+F10：对大窗口（浏览器全屏）的图像搜索也能及时停止。

## 4. 性能
- [ ] 全屏 1080p 窗口里 `find-image`，记录耗时（我预期 < 0.5 秒）；4K 屏请也试一次。
- [ ] 小模板（如 16x16）在全屏里搜索的耗时。超过 2 秒请告诉我分辨率和模板尺寸。

## 5. 界面
- [ ] `winauto-ui` 打开示例流程，运行：图像模板相对**流程文件所在目录**解析；新建未保存的流程里用图像定位 → 提示先保存。

## 请回报
编译错误原文；`find-image` 在哪些程序/缩放下找不到或误匹配；耗时；任何与上面预期不符的现象。
