set(WINAUTO_TEST_SOURCES
    test_errcode.cpp
    test_json.cpp
    test_validator.cpp
    test_state_cancel.cpp
    test_logger.cpp
    test_engine.cpp
    test_window_hotkey.cpp
    test_input_plan.cpp
    test_uia_match.cpp
    test_editor.cpp
    test_debug_gate.cpp
    test_template_match.cpp
    test_path_safety.cpp
    test_flow_control.cpp
    test_script_runtime.cpp
    test_template_assess.cpp
)

foreach(src ${WINAUTO_TEST_SOURCES})
    get_filename_component(name ${src} NAME_WE)
    add_executable(${name} ${src} testing_main.cpp)
    target_link_libraries(${name} PRIVATE winauto_core)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${name} SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
    target_compile_definitions(${name} PRIVATE WINAUTO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    add_test(NAME ${name} COMMAND ${name})
endforeach()

if(Qt5_FOUND)
    add_executable(test_workflow_generation
        test_workflow_generation.cpp
        testing_main.cpp
        ${CMAKE_SOURCE_DIR}/ui/WorkflowGeneration.cpp
        ${CMAKE_SOURCE_DIR}/ui/WorkflowCapabilities.cpp
        ${CMAKE_SOURCE_DIR}/ui/WorkflowProposalParser.cpp
        ${CMAKE_SOURCE_DIR}/ui/ClaudeWorkflowProvider.cpp
        ${CMAKE_SOURCE_DIR}/ui/OpenAIWorkflowProvider.cpp
    )
    set_target_properties(test_workflow_generation PROPERTIES AUTOMOC ON)
    target_include_directories(test_workflow_generation PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/ui
    )
    target_link_libraries(test_workflow_generation PRIVATE Qt5::Core Qt5::Network winauto_core)
    add_test(NAME test_workflow_generation COMMAND test_workflow_generation)
endif()

# core 纯净性检查：core/ 下不得引用 Win32 / COM / Qt 头文件
add_test(NAME core_purity
    COMMAND ${CMAKE_COMMAND} -DCORE_DIR=${CMAKE_SOURCE_DIR}/core -P ${CMAKE_CURRENT_SOURCE_DIR}/check_core_purity.cmake)

# CLI 冒烟测试
add_test(NAME cli_validate_examples
    COMMAND winauto-cli validate ${CMAKE_SOURCE_DIR}/examples/notepad-demo.json
                                 ${CMAKE_SOURCE_DIR}/examples/fallback-prompt-demo.json
                ${CMAKE_SOURCE_DIR}/examples/notepad-coordinates-demo.json
                ${CMAKE_SOURCE_DIR}/examples/image-fallback-demo.json
                ${CMAKE_SOURCE_DIR}/examples/flow-control-demo.json
                ${CMAKE_SOURCE_DIR}/examples/mouse-demo.json
                ${CMAKE_SOURCE_DIR}/examples/launch-program-demo.json)
add_test(NAME cli_rejects_invalid_workflow
    COMMAND winauto-cli validate ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/invalid-empty.json)
set_tests_properties(cli_rejects_invalid_workflow PROPERTIES WILL_FAIL TRUE)

# Schema 检查：需要 Python 与 jsonschema，没有则跳过（不影响构建）
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME schema_examples
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_schema.py
                ${CMAKE_SOURCE_DIR}/schemas/workflow.schema.json
                ${CMAKE_SOURCE_DIR}/examples/notepad-demo.json
                ${CMAKE_SOURCE_DIR}/examples/fallback-prompt-demo.json
                ${CMAKE_SOURCE_DIR}/examples/notepad-coordinates-demo.json
                ${CMAKE_SOURCE_DIR}/examples/image-fallback-demo.json
                ${CMAKE_SOURCE_DIR}/examples/flow-control-demo.json
                ${CMAKE_SOURCE_DIR}/examples/mouse-demo.json
                ${CMAKE_SOURCE_DIR}/examples/launch-program-demo.json)
    set_tests_properties(schema_examples PROPERTIES SKIP_RETURN_CODE 77)
endif()
