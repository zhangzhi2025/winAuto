set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
add_executable(winauto-ui WIN32
    main.cpp
    resources.qrc
    MainWindow.cpp
    WorkflowCapabilities.cpp
    WorkflowGeneration.cpp
    WorkflowProposalParser.cpp
    ModelConnectionSettings.cpp
    ModelSettingsDialog.cpp
    AgentConnectionSettings.cpp
    AgentSession.cpp
    AgentSessionDialog.cpp
    ClaudeWorkflowProvider.cpp
    OpenAIWorkflowProvider.cpp
    WorkflowGenerationDialog.cpp
    StepPanel.cpp
    WindowPickerDialog.cpp
    WindowsDialog.cpp
    ElementPickerDialog.cpp
    TemplateCaptureDialog.cpp
    RunController.cpp
)
target_link_libraries(winauto-ui PRIVATE Qt5::Widgets Qt5::Network Qt5::WebSockets winauto_core)
if(WIN32)
    # 窗口枚举、UIA、输入、停止热键都在 platform_win；没有它界面只能编辑，不能运行
    target_link_libraries(winauto-ui PRIVATE winauto_platform)
    target_compile_definitions(winauto-ui PRIVATE WINAUTO_HAVE_PLATFORM)
    if(WINAUTO_ENABLE_OCR)
        add_custom_command(TARGET winauto-ui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_BINARY_DIR}/models" "$<TARGET_FILE_DIR:winauto-ui>/models")
        get_target_property(WINAUTO_OCR_RUNTIME_DLLS winauto_platform WINAUTO_OCR_RUNTIME_DLLS)
        foreach(runtime_dll IN LISTS WINAUTO_OCR_RUNTIME_DLLS)
            add_custom_command(TARGET winauto-ui POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${runtime_dll}" "$<TARGET_FILE_DIR:winauto-ui>")
        endforeach()
    endif()
    # 声明 Per-Monitor V2 DPI 感知（与 main.cpp 中关闭 Qt 自动缩放配套）
    target_sources(winauto-ui PRIVATE app.manifest)
endif()
