#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/Validator.h"
#include "core/WorkflowJson.h"
#include "tests/TestFramework.h"

#ifndef WINAUTO_SOURCE_DIR
#error "WINAUTO_SOURCE_DIR must be defined by CMake"
#endif

using namespace winauto;
namespace fs = std::filesystem;

TEST(Examples, everyExampleLoadsAndValidates) {
    const fs::path dir = fs::path(WINAUTO_SOURCE_DIR) / "examples";
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        ++count;
        Workflow wf;
        ValidationReport r = loadWorkflowFile(entry.path(), &wf);
        if (r.hasErrors()) tf::report(__FILE__, __LINE__, entry.path().string() + ":\n" + formatReport(r));
        CHECK(!r.needsConfirmation());
        CHECK(!wf.steps.empty());
    }
    CHECK(count >= 2);
}

TEST(Examples, notepadDemoMatchesTheDocumentedFlow) {
    Workflow wf;
    ValidationReport r = loadWorkflowFile(fs::path(WINAUTO_SOURCE_DIR) / "examples" / "notepad-demo.json", &wf);
    REQUIRE(!r.hasErrors());
    CHECK_EQ(wf.id, std::string("WF-NOTEPAD-001"));
    REQUIRE(wf.steps.size() == 4);
    CHECK(wf.steps[0].type == StepType::ActivateWindow);
    CHECK(wf.steps[1].type == StepType::Click);
    CHECK(wf.steps[2].type == StepType::InputText);
    CHECK(wf.steps[3].type == StepType::Hotkey);
    CHECK_EQ(wf.windows[0].spec.processName, std::string("notepad.exe"));
}

TEST(Examples, examplesRoundTripThroughWriter) {
    const fs::path dir = fs::path(WINAUTO_SOURCE_DIR) / "examples";
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        std::ostringstream os;
        os << in.rdbuf();
        JsonParseResult original = parseJson(os.str());
        REQUIRE(original.ok);
        Workflow wf;
        REQUIRE(readWorkflow(original.value, &wf, nullptr));
        if (!(writeWorkflow(wf) == original.value))
            tf::report(__FILE__, __LINE__, "round trip changed " + entry.path().string());
    }
}

TEST(Examples, schemaFileIsPresentAndParses) {
    std::ifstream in(fs::path(WINAUTO_SOURCE_DIR) / "schemas" / "workflow.schema.json", std::ios::binary);
    REQUIRE(static_cast<bool>(in));
    std::ostringstream os;
    os << in.rdbuf();
    JsonParseResult r = parseJson(os.str());
    CHECK(r.ok);
}
