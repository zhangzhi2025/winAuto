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
    if(NOT WIN32)
        message(FATAL_ERROR "WINAUTO_ENABLE_OCR is supported only on Windows.")
    endif()
    set(RAPIDOCR_ROOT "${CMAKE_SOURCE_DIR}/third_party/RapidOcrOnnx")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(RAPIDOCR_ARCH "windows-x64")
    else()
        set(RAPIDOCR_ARCH "windows-x86")
    endif()
    set(ONNXRUNTIME_CONFIG_DIR "${RAPIDOCR_ROOT}/onnxruntime-static/${RAPIDOCR_ARCH}")
    set(OPENCV_CONFIG_DIR "${RAPIDOCR_ROOT}/opencv-static/${RAPIDOCR_ARCH}")
    if(NOT EXISTS "${ONNXRUNTIME_CONFIG_DIR}/OnnxRuntimeConfig.cmake")
        message(FATAL_ERROR
            "OCR was requested, but the bundled ONNX Runtime package is missing for ${RAPIDOCR_ARCH}. "
            "Install the RapidOcrOnnx MSVC libraries under ${ONNXRUNTIME_CONFIG_DIR}, or configure with "
            "-DWINAUTO_ENABLE_OCR=OFF.")
    endif()
    if(NOT EXISTS "${OPENCV_CONFIG_DIR}/OpenCVConfig.cmake")
        message(FATAL_ERROR
            "OCR was requested, but the bundled OpenCV package is missing for ${RAPIDOCR_ARCH}. "
            "Install the RapidOcrOnnx MSVC libraries under ${OPENCV_CONFIG_DIR}, or configure with "
            "-DWINAUTO_ENABLE_OCR=OFF.")
    endif()
    find_package(OnnxRuntime CONFIG REQUIRED PATHS "${ONNXRUNTIME_CONFIG_DIR}" NO_DEFAULT_PATH)
    find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc PATHS "${OPENCV_CONFIG_DIR}" NO_DEFAULT_PATH)

    set(RAPIDOCR_SOURCE_DIR "${RAPIDOCR_ROOT}/src")
    set(RAPIDOCR_SOURCES
        "${RAPIDOCR_SOURCE_DIR}/AngleNet.cpp"
        "${RAPIDOCR_SOURCE_DIR}/clipper.cpp"
        "${RAPIDOCR_SOURCE_DIR}/CrnnNet.cpp"
        "${RAPIDOCR_SOURCE_DIR}/DbNet.cpp"
        "${RAPIDOCR_SOURCE_DIR}/OcrLite.cpp"
        "${RAPIDOCR_SOURCE_DIR}/OcrUtils.cpp")
    target_sources(winauto_platform PRIVATE OcrRuntime.cpp Win32OcrProvider.cpp ${RAPIDOCR_SOURCES})
    target_include_directories(winauto_platform SYSTEM PRIVATE
        "${RAPIDOCR_ROOT}/include"
        ${OnnxRuntime_INCLUDE_DIRS}
        ${OpenCV_INCLUDE_DIRS})
    target_link_directories(winauto_platform PRIVATE ${OnnxRuntime_LIBRARY_DIRS})
    target_link_libraries(winauto_platform PRIVATE ${OnnxRuntime_LIBS} ${OpenCV_LIBS})
    target_compile_definitions(winauto_platform PUBLIC WINAUTO_ENABLE_OCR=1)
    file(GLOB RAPIDOCR_RUNTIME_DLLS
        "${ONNXRUNTIME_CONFIG_DIR}/bin/*.dll"
        "${ONNXRUNTIME_CONFIG_DIR}/lib/*.dll"
        "${OPENCV_CONFIG_DIR}/bin/*.dll")
    set_property(TARGET winauto_platform PROPERTY WINAUTO_OCR_RUNTIME_DLLS "${RAPIDOCR_RUNTIME_DLLS}")

    set(RAPIDOCR_MODEL_DIR "${RAPIDOCR_ROOT}/models")
    foreach(model
            ch_PP-OCRv3_det_infer.onnx
            ch_ppocr_mobile_v2.0_cls_train.onnx
            ch_PP-OCRv3_rec_infer.onnx
            en_PP-OCRv3_rec_infer.onnx
            ppocr_keys_v1.txt)
        if(NOT EXISTS "${RAPIDOCR_MODEL_DIR}/${model}")
            message(FATAL_ERROR "OCR model asset is missing: ${RAPIDOCR_MODEL_DIR}/${model}")
        endif()
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/models")
        configure_file("${RAPIDOCR_MODEL_DIR}/${model}" "${CMAKE_BINARY_DIR}/models/${model}" COPYONLY)
    endforeach()
    target_compile_definitions(winauto_platform PRIVATE
        WINAUTO_OCR_MODEL_DIR="${CMAKE_BINARY_DIR}/models")
    if(MSVC)
        set_source_files_properties(${RAPIDOCR_SOURCES} PROPERTIES COMPILE_OPTIONS "/W0")
    else()
        set_source_files_properties(${RAPIDOCR_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
    endif()
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
