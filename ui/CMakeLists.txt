set(CMAKE_AUTOMOC ON)
add_executable(winauto-ui WIN32 main.cpp)
target_link_libraries(winauto-ui PRIVATE Qt5::Widgets winauto_core)
if(WIN32)
    # 声明 Per-Monitor V2 DPI 感知（与 main.cpp 中关闭 Qt 自动缩放配套）
    target_sources(winauto-ui PRIVATE app.manifest)
endif()
