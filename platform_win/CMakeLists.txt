# Windows 适配器：只做薄封装，判断逻辑都在 core（已测试）。
# 阶段 1：窗口枚举/激活/提权检测、DPI、全局停止热键。
# 阶段 2：SendInput 输入、窗口坐标定位。阶段 3 加入 UIA。
add_library(winauto_platform STATIC
    DpiHelper.cpp
    Win32WindowProvider.cpp
    Win32StopHotkey.cpp
    Win32InputController.cpp
    Win32PointLocator.cpp
)
target_include_directories(winauto_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(winauto_platform PUBLIC winauto_core)
target_link_libraries(winauto_platform PRIVATE user32 dwmapi advapi32)
# DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 等符号需要 Windows 10 1703（RS2）及以上的头文件
target_compile_definitions(winauto_platform PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000003)
