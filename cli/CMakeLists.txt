add_executable(winauto-cli main.cpp)
target_link_libraries(winauto-cli PRIVATE winauto_core)
if(WIN32)
    target_link_libraries(winauto-cli PRIVATE winauto_platform shell32)
    target_compile_definitions(winauto-cli PRIVATE WINAUTO_HAVE_PLATFORM)
    if(WINAUTO_ENABLE_OCR)
        add_custom_command(TARGET winauto-cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_BINARY_DIR}/models" "$<TARGET_FILE_DIR:winauto-cli>/models")
        get_target_property(WINAUTO_OCR_RUNTIME_DLLS winauto_platform WINAUTO_OCR_RUNTIME_DLLS)
        foreach(runtime_dll IN LISTS WINAUTO_OCR_RUNTIME_DLLS)
            add_custom_command(TARGET winauto-cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${runtime_dll}" "$<TARGET_FILE_DIR:winauto-cli>")
        endforeach()
    endif()
endif()
