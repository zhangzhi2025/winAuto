add_library(stock_monitor_core STATIC
    core/StockConfig.cpp
    core/FontColorAnalyzer.cpp
    core/StockRowAssembler.cpp
    core/StockClassifier.cpp
    core/StockMonitorService.cpp
)
target_include_directories(stock_monitor_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core)

option(STOCK_MONITOR_ENABLE_RAPIDOCR "Build the RapidOcrOnnx C++ text-block adapter" OFF)
set(STOCK_MONITOR_OCR_AVAILABLE OFF)
if(STOCK_MONITOR_ENABLE_RAPIDOCR)
    set(RAPIDOCR_SOURCE_ROOT "${CMAKE_SOURCE_DIR}/third_party/RapidOcrOnnx")
    set(RAPIDOCR_OPENCV_ROOT "${RAPIDOCR_SOURCE_ROOT}/opencv-static/windows-x86")
    set(RAPIDOCR_ONNX_SHARED_ROOT "${RAPIDOCR_SOURCE_ROOT}/onnxruntime-share/windows-x86")
    set(RAPIDOCR_ONNX_STATIC_ROOT "${RAPIDOCR_SOURCE_ROOT}/onnxruntime-static/windows-x86")
    if(EXISTS "${RAPIDOCR_ONNX_SHARED_ROOT}/bin/onnxruntime.dll"
       AND EXISTS "${RAPIDOCR_ONNX_SHARED_ROOT}/lib/onnxruntime.lib")
        set(RAPIDOCR_ONNX_ROOT "${RAPIDOCR_ONNX_SHARED_ROOT}")
        set(STOCK_MONITOR_ONNX_SHARED ON)
    else()
        set(RAPIDOCR_ONNX_ROOT "${RAPIDOCR_ONNX_STATIC_ROOT}")
        set(STOCK_MONITOR_ONNX_SHARED OFF)
    endif()
    foreach(required_path
            "${RAPIDOCR_SOURCE_ROOT}/include/OcrLite.h"
            "${RAPIDOCR_SOURCE_ROOT}/src/OcrLite.cpp"
            "${RAPIDOCR_OPENCV_ROOT}"
            "${RAPIDOCR_ONNX_ROOT}/include/onnxruntime_cxx_api.h"
            "${RAPIDOCR_ONNX_ROOT}/lib/onnxruntime.lib")
        if(NOT EXISTS "${required_path}")
            message(FATAL_ERROR
                "Stock monitor RapidOcrOnnx C++ dependency is missing: ${required_path}. "
                "Install matching Win32 OpenCV and ONNX Runtime packages, or configure "
                "with -DSTOCK_MONITOR_ENABLE_RAPIDOCR=OFF. The flattened CLIB API is not supported.")
        endif()
    endforeach()

    if(NOT STOCK_MONITOR_ONNX_SHARED)
        set(ONNXRUNTIME_BUILD_LOG "${RAPIDOCR_ONNX_ROOT}/link.log")
        string(REGEX MATCH "^19\\.([0-9]+)" MSVC_TOOLSET_VERSION_MATCH "${CMAKE_CXX_COMPILER_VERSION}")
        set(MSVC_TOOLSET_MINOR "${CMAKE_MATCH_1}")
        if(MSVC AND "${MSVC_TOOLSET_MINOR}" LESS 30 AND EXISTS "${ONNXRUNTIME_BUILD_LOG}")
            file(READ "${ONNXRUNTIME_BUILD_LOG}" ONNXRUNTIME_BUILD_METADATA_HEX HEX)
            string(TOLOWER "${ONNXRUNTIME_BUILD_METADATA_HEX}" ONNXRUNTIME_BUILD_METADATA_HEX_LOWER)
            string(FIND "${ONNXRUNTIME_BUILD_METADATA_HEX_LOWER}" "5600310034003300" ONNXRUNTIME_V143_UTF16_POSITION)
            string(FIND "${ONNXRUNTIME_BUILD_METADATA_HEX_LOWER}" "56313433" ONNXRUNTIME_V143_ASCII_POSITION)
            if(NOT ONNXRUNTIME_V143_UTF16_POSITION EQUAL -1 OR NOT ONNXRUNTIME_V143_ASCII_POSITION EQUAL -1)
                message(FATAL_ERROR
                    "The ONNX Runtime static package at ${RAPIDOCR_ONNX_ROOT} was built with MSVC v143 "
                    "(Visual Studio 2022), but this build uses MSVC ${CMAKE_CXX_COMPILER_VERSION} "
                    "(Visual Studio 2019/v142). Use the shared ONNX Runtime package, rebuild ONNX Runtime "
                    "with VS2019/v142, or use a compatible VS2022 toolchain.")
            endif()
        endif()
    endif()

    include("${RAPIDOCR_SOURCE_ROOT}/opencv-static/OpenCVWrapperConfig.cmake")
    set(OpenCV_STATIC ON)
    find_package(OpenCV REQUIRED)
    set(OnnxRuntime_DIR "${RAPIDOCR_ONNX_ROOT}")
    find_package(OnnxRuntime REQUIRED)
    set(OnnxRuntime_LIBS "${RAPIDOCR_ONNX_ROOT}/lib/onnxruntime.lib")
    if(STOCK_MONITOR_ONNX_SHARED)
        set(OnnxRuntime_RUNTIME_DLLS
            "${RAPIDOCR_ONNX_ROOT}/bin/onnxruntime.dll"
            "${RAPIDOCR_ONNX_ROOT}/bin/onnxruntime_providers_shared.dll"
        )
    endif()

    add_library(stock_monitor_rapidocr STATIC
        "${RAPIDOCR_SOURCE_ROOT}/src/AngleNet.cpp"
        "${RAPIDOCR_SOURCE_ROOT}/src/clipper.cpp"
        "${RAPIDOCR_SOURCE_ROOT}/src/CrnnNet.cpp"
        "${RAPIDOCR_SOURCE_ROOT}/src/DbNet.cpp"
        "${RAPIDOCR_SOURCE_ROOT}/src/OcrLite.cpp"
        "${RAPIDOCR_SOURCE_ROOT}/src/OcrUtils.cpp"
        platform_win/RapidOcrAdapter.cpp
    )
    target_include_directories(stock_monitor_rapidocr SYSTEM PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/platform_win/compat"
        "${RAPIDOCR_SOURCE_ROOT}/include"
        ${OpenCV_INCLUDE_DIRS}
        ${OnnxRuntime_INCLUDE_DIRS}
    )
    target_include_directories(stock_monitor_rapidocr PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/core")
    target_link_libraries(stock_monitor_rapidocr PRIVATE ${OpenCV_LIBS} ${OnnxRuntime_LIBS})
    target_link_libraries(stock_monitor_rapidocr PUBLIC stock_monitor_core)
    set_property(TARGET stock_monitor_rapidocr PROPERTY
        STOCK_MONITOR_RUNTIME_DLLS "${OnnxRuntime_RUNTIME_DLLS};${OpenCV_RUNTIME_DLLS}"
    )
    if(MSVC)
        target_compile_options(stock_monitor_rapidocr PRIVATE /W0 /WX-)
    endif()

    set(STOCK_MONITOR_MODEL_SOURCE_DIR "${RAPIDOCR_SOURCE_ROOT}/models")
    set(STOCK_MONITOR_MODEL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/models")
    set(STOCK_MONITOR_MODEL_FILES
        ch_PP-OCRv3_det_infer.onnx
        ch_ppocr_mobile_v2.0_cls_train.onnx
        ch_PP-OCRv3_rec_infer.onnx
        ppocr_keys_v1.txt
    )
    foreach(model IN LISTS STOCK_MONITOR_MODEL_FILES)
        if(NOT EXISTS "${STOCK_MONITOR_MODEL_SOURCE_DIR}/${model}")
            message(FATAL_ERROR "Stock monitor OCR model file is missing: ${STOCK_MONITOR_MODEL_SOURCE_DIR}/${model}")
        endif()
        file(MAKE_DIRECTORY "${STOCK_MONITOR_MODEL_OUTPUT_DIR}")
        configure_file("${STOCK_MONITOR_MODEL_SOURCE_DIR}/${model}"
                       "${STOCK_MONITOR_MODEL_OUTPUT_DIR}/${model}" COPYONLY)
    endforeach()
    target_compile_definitions(stock_monitor_rapidocr PRIVATE
        STOCK_MONITOR_MODEL_DIR="${STOCK_MONITOR_MODEL_OUTPUT_DIR}"
    )
    set(STOCK_MONITOR_OCR_AVAILABLE ON)
endif()

if(WIN32)
    find_package(Qt5 5.15 COMPONENTS Widgets QUIET)
    if(STOCK_MONITOR_OCR_AVAILABLE)
        find_package(Qt5 5.15 COMPONENTS Concurrent REQUIRED)
    endif()
    if(Qt5Widgets_FOUND)
        add_executable(stock-monitor
            platform_win/StockWindowCapture.cpp
            ui/main.cpp
            ui/StockMonitorWindow.cpp
        )
        if(STOCK_MONITOR_OCR_AVAILABLE)
            target_sources(stock-monitor PRIVATE platform_win/RapidOcrAdapter.h)
            target_link_libraries(stock-monitor PRIVATE stock_monitor_rapidocr)
            target_compile_definitions(stock-monitor PRIVATE STOCK_MONITOR_HAS_RAPIDOCR=1)
            add_custom_command(TARGET stock-monitor POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        "${STOCK_MONITOR_MODEL_OUTPUT_DIR}"
                        "$<TARGET_FILE_DIR:stock-monitor>/models"
            )
            foreach(runtime_dll IN LISTS OnnxRuntime_RUNTIME_DLLS OpenCV_RUNTIME_DLLS)
                if(EXISTS "${runtime_dll}")
                    add_custom_command(TARGET stock-monitor POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                "${runtime_dll}" "$<TARGET_FILE_DIR:stock-monitor>"
                    )
                endif()
            endforeach()
        endif()
        target_include_directories(stock-monitor PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/core
            ${CMAKE_CURRENT_SOURCE_DIR}/platform_win
            ${CMAKE_CURRENT_SOURCE_DIR}/ui
        )
        target_link_libraries(stock-monitor PRIVATE stock_monitor_core Qt5::Widgets user32 gdi32)
        if(STOCK_MONITOR_OCR_AVAILABLE)
            target_link_libraries(stock-monitor PRIVATE Qt5::Concurrent)
        endif()
        target_compile_definitions(stock-monitor PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN
            _WIN32_WINNT=0x0A00 WINVER=0x0A00
        )
        set_target_properties(stock-monitor PROPERTIES
            OUTPUT_NAME "stock-monitor"
            WIN32_EXECUTABLE TRUE
        )
    else()
        message(STATUS "Qt5 Widgets not found; building stock_monitor_core without the GUI.")
    endif()
endif()
