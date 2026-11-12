# Windows 适配器：只做薄封装，判断逻辑都在 core（已测试）。
# 阶段 1：窗口枚举/激活/提权检测、DPI、全局停止热键。
# 阶段 2：SendInput 输入、窗口坐标定位。
# 阶段 3：UI Automation（快照读取 + Invoke/Value），匹配规则在 core。
# 阶段 5：图像定位（GDI 截屏 + WIC 读图，匹配规则在 core）。
# 阶段 6：失败截图、剪贴板输入。
add_library(winauto_platform STATIC
    DpiHelper.cpp
    Win32WindowProvider.cpp
    Win32StopHotkey.cpp
    Win32InputController.cpp
    Win32PointLocator.cpp
    UiaSession.cpp
    Win32UiaLocator.cpp
    ScreenCapture.cpp
    ImageFile.cpp
    Win32ImageLocator.cpp
    Win32Screenshot.cpp
    Win32Clipboard.cpp
    ScriptWorker.cpp
    Win32ProcessLauncher.cpp
    ModelCredentialStore.cpp
    WindowGrab.cpp
)
if(WINAUTO_ENABLE_OCR)
    if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR
            "The bundled RapidOcrOnnx package is Win32 only. Configure with -A Win32, or disable OCR.")
    endif()
    set(RAPIDOCR_ROOT "${CMAKE_SOURCE_DIR}/third_party/rapidocr/win-CLIB-CPU-Win32")
    set(RAPIDOCR_INCLUDE_DIR "${RAPIDOCR_ROOT}/include")
    set(RAPIDOCR_LIBRARY "${RAPIDOCR_ROOT}/lib/RapidOcrOnnx.lib")
    set(RAPIDOCR_DLL "${RAPIDOCR_ROOT}/bin/RapidOcrOnnx.dll")
    foreach(required_file
            "${RAPIDOCR_INCLUDE_DIR}/OcrLiteCApi.h"
            "${RAPIDOCR_LIBRARY}"
            "${RAPIDOCR_DLL}")
        if(NOT EXISTS "${required_file}")
            message(FATAL_ERROR "RapidOcrOnnx Win32 package file is missing: ${required_file}")
        endif()
    endforeach()
    target_sources(winauto_platform PRIVATE OcrRuntime.cpp Win32OcrProvider.cpp)
    target_include_directories(winauto_platform SYSTEM PRIVATE "${RAPIDOCR_INCLUDE_DIR}")
    target_link_libraries(winauto_platform PRIVATE "${RAPIDOCR_LIBRARY}")
    target_compile_definitions(winauto_platform PUBLIC WINAUTO_ENABLE_OCR=1)
    set_property(TARGET winauto_platform PROPERTY WINAUTO_OCR_RUNTIME_DLLS "${RAPIDOCR_DLL}")

    set(RAPIDOCR_SOURCE_MODEL_DIR "${CMAKE_SOURCE_DIR}/third_party/rapidocr/models")
    set(RAPIDOCR_MODELS
        ch_PP-OCRv3_det_infer.onnx
        ch_ppocr_mobile_v2.0_cls_train.onnx
        ch_PP-OCRv3_rec_infer.onnx
        ppocr_keys_v1.txt)
    foreach(model IN LISTS RAPIDOCR_MODELS)
        if(NOT EXISTS "${RAPIDOCR_SOURCE_MODEL_DIR}/${model}")
            message(FATAL_ERROR "OCR model asset is missing: ${RAPIDOCR_SOURCE_MODEL_DIR}/${model}")
        endif()
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/models")
        configure_file("${RAPIDOCR_SOURCE_MODEL_DIR}/${model}"
                       "${CMAKE_BINARY_DIR}/models/${model}" COPYONLY)
    endforeach()
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/RapidOcrOnnx/images/1.jpg")
        message(FATAL_ERROR "OCR verification image is missing: third_party/RapidOcrOnnx/images/1.jpg")
    endif()
    configure_file("${CMAKE_SOURCE_DIR}/third_party/RapidOcrOnnx/images/1.jpg"
                   "${CMAKE_BINARY_DIR}/models/ocr-smoke-test.jpg" COPYONLY)
    target_compile_definitions(winauto_platform PRIVATE
        WINAUTO_OCR_MODEL_DIR="${CMAKE_BINARY_DIR}/models")
endif()
target_include_directories(winauto_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(winauto_platform SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
target_link_libraries(winauto_platform PUBLIC winauto_core)
target_link_libraries(winauto_platform PRIVATE user32 gdi32 dwmapi advapi32 crypt32 ole32 oleaut32 uuid windowscodecs)
set(WINAUTO_WORKER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/script_worker")
file(MAKE_DIRECTORY "${WINAUTO_WORKER_OUTPUT_DIR}")
configure_file(script_worker/python_worker.py "${WINAUTO_WORKER_OUTPUT_DIR}/python_worker.py" COPYONLY)
configure_file(script_worker/lua_worker.lua "${WINAUTO_WORKER_OUTPUT_DIR}/lua_worker.lua" COPYONLY)
target_compile_definitions(winauto_platform PRIVATE
    WINAUTO_SCRIPT_WORKER_DIR="${WINAUTO_WORKER_OUTPUT_DIR}")
# DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 等符号需要 Windows 10 1703（RS2）及以上的头文件
target_compile_definitions(winauto_platform PUBLIC _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000003)
