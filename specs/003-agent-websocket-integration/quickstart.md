# Agent WebSocket 对接快速验证

## 构建

Qt 桌面目标需 Qt 5.15 Widgets、Network 和 WebSockets 模块。按 README 中现有 Windows CMake 命令配置项目；Qt WebSockets 若未安装，UI/Agent 目标应报告缺失依赖并跳过，core、CLI 和非 Qt 测试仍应正常构建。

```bat
cmake -S . -B build -G "Visual Studio 16 2019" -A Win32 -DCMAKE_PREFIX_PATH=D:/Qt/qt5.15/msvc2019 -DWINAUTO_WERROR=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

## 使用参考桥接手动验证

1. 确认参考服务的权限处理不会自动批准工具操作。服务默认关闭 `COPILOT_AUTO_APPROVE`；需要显式逐项批准时不要将其设为 `true`。
2. 按服务自身文档安装并启动 Copilot CLI/SDK WebSocket 桥接；默认监听地址为 `ws://127.0.0.1:8765`。WinAuto 不启动或管理服务。
3. 在 Agent 会话入口填写 WebSocket 地址和真实存在的项目目录，检查目录后保存设置。
4. 清空 session ID 并新建会话；确认服务返回的 session ID 和实际目录显示正确。断开后将 ID 重新填入并恢复。
5. 发送非空任务，确认增量文本、工具活动、开始/完成状态可区分；发送过程中再次提交应被拒绝。
6. 分别测试中止、服务端错误、非法 JSON、未知事件、网络断开和连接超时；均不得显示为成功，失败后输入和设置可重试。
7. 在流程编辑器保留未保存编辑并配置大模型服务；打开/关闭 Agent、保存其设置并触发失败，确认编辑器内容、草稿和模型设置均未变化。
8. 确认日志及错误窗口中不包含完整任务、原始敏感 URL 或完整工具事件。

## 无 Copilot 的界面演示

可用本地模拟服务检查 UI 和基本消息流程，无需 Copilot CLI 登录：

```bat
python -m pip install websockets
python docs\server\demo_server.py
```

在 WinAuto Agent 对话框连接 `ws://127.0.0.1:8765`，选择存在的目录并发送消息。演示服务会返回模拟的流式文本和 `demo.echo` 工具活动，但不会调用模型或执行任何工具；按 `Ctrl+C` 关闭服务。

## 自动化测试范围

`test_agent_session` 使用本机 Qt WebSocket 测试服务器按契约返回新建/恢复结果及各轮事件。测试覆盖无效 URL/目录、session 恢复失败、单请求限制、错误优先级、完成与结束区分、中止、帧尺寸限制、断连清理和配置键隔离；不依赖 Copilot 登录、外部网络或真实凭据。
