set(CMAKE_AUTOMOC ON)
add_executable(winauto-ui WIN32
    main.cpp
    MainWindow.cpp
    StepPanel.cpp
    WindowPickerDialog.cpp
    WindowsDialog.cpp
    ElementPickerDialog.cpp
    RunController.cpp
)
target_link_libraries(winauto-ui PRIVATE Qt5::Widgets winauto_core)
if(WIN32)
    # 窗口枚举、UIA、输入、停止热键都在 platform_win；没有它界面只能编辑，不能运行
    target_link_libraries(winauto-ui PRIVATE winauto_platform)
    target_compile_definitions(winauto-ui PRIVATE WINAUTO_HAVE_PLATFORM)
    # 声明 Per-Monitor V2 DPI 感知（与 main.cpp 中关闭 Qt 自动缩放配套）
    target_sources(winauto-ui PRIVATE app.manifest)
endif()
