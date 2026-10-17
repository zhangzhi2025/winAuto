cmake_minimum_required(VERSION 3.16)
project(WinAuto VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WINAUTO_BUILD_TESTS "Build unit tests" ON)
option(WINAUTO_WERROR "Treat warnings as errors" OFF)

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /Zc:__cplusplus)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
    if(WINAUTO_WERROR)
        add_compile_options(/WX)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(WINAUTO_WERROR)
        add_compile_options(-Werror)
    endif()
endif()

# core: 纯标准 C++17，不依赖 Qt / Win32，可在任何平台编译和测试
add_subdirectory(core)

# platform_win: Windows 适配器（阶段 1 起填充）
if(WIN32)
    add_subdirectory(platform_win)
endif()

# 命令行工具（阶段 0 仅 validate，阶段 2 起支持 run）
add_subdirectory(cli)

# ui: Qt 5.15 Widgets，找不到 Qt 时自动跳过
find_package(Qt5 5.15 COMPONENTS Widgets QUIET)
if(Qt5_FOUND)
    message(STATUS "Qt5 ${Qt5_VERSION} found, building ui")
    add_subdirectory(ui)
else()
    message(STATUS "Qt5 not found, skipping ui (set CMAKE_PREFIX_PATH to your Qt 5.15 kit)")
endif()

if(WINAUTO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
