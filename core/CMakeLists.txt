add_library(winauto_core STATIC
    ErrCode.cpp
    Model.cpp
    Json.cpp
    Validator.cpp
    ExecutionState.cpp
    CancelToken.cpp
    Clock.cpp
    Logger.cpp
    Engine.cpp
    WindowMatcher.cpp
    VirtualKey.cpp
    StopHotkey.cpp
    InputPlan.cpp
    Summary.cpp
    ControlType.cpp
    UiaMatch.cpp
    GrayImage.cpp
    TemplateMatch.cpp
    PathSafety.cpp
    WorkflowEditor.cpp
    DebugGate.cpp
    StepTree.cpp
    TemplateAssess.cpp
)
target_include_directories(winauto_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
# nlohmann/json 仅在 .cpp 内使用，不暴露给使用者
target_include_directories(winauto_core SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../third_party)
find_package(Threads REQUIRED)
target_link_libraries(winauto_core PUBLIC Threads::Threads)
