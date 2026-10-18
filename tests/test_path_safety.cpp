#include <filesystem>
#include <fstream>

#include "core/PathSafety.h"
#include "testing.h"

using namespace wa;
namespace fs = std::filesystem;

namespace {
struct Sandbox {
    fs::path root;
    Sandbox() {
        root = fs::temp_directory_path() / ("wa_path_" + std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(root);
        fs::create_directories(root / "flow" / "assets");
        fs::create_directories(root / "secret");
        std::ofstream(root / "flow" / "assets" / "a.png") << "x";
        std::ofstream(root / "secret" / "s.png") << "x";
        std::ofstream(root / "flow2.png") << "x";
    }
    ~Sandbox() { std::error_code ec; fs::remove_all(root, ec); }
    std::string flowDir() const { return (root / "flow").u8string(); }
};
}  // namespace

TEST(resolves_file_inside_base) {
    Sandbox sb;
    auto r = resolveTemplatePath(sb.flowDir(), "assets/a.png");
    CHECK(r.ok());
    CHECK(r.value.size() > 5);
}

TEST(empty_base_means_unsaved_workflow) {
    auto r = resolveTemplatePath("", "assets/a.png");
    CHECK_EQ(r.code, ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "save the workflow first");
}

TEST(missing_file_and_directory) {
    Sandbox sb;
    CHECK_CONTAINS(resolveTemplatePath(sb.flowDir(), "assets/nope.png").detail, "not found");
    CHECK_EQ(resolveTemplatePath(sb.flowDir(), "assets").code, ErrCode::InvalidWorkflow);  // 目录不是文件
    CHECK_EQ(resolveTemplatePath((sb.root / "missing").u8string(), "a.png").code, ErrCode::InvalidWorkflow);
    CHECK_EQ(resolveTemplatePath(sb.flowDir(), "").code, ErrCode::InvalidWorkflow);
}

TEST(dotdot_escape_is_rejected_even_if_validator_missed_it) {
    Sandbox sb;
    auto r = resolveTemplatePath(sb.flowDir(), "../secret/s.png");
    CHECK_EQ(r.code, ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "outside");
}

TEST(sibling_directory_with_common_prefix_is_not_inside) {
    Sandbox sb;
    fs::create_directories(sb.root / "flowEvil");
    std::ofstream(sb.root / "flowEvil" / "e.png") << "x";
    auto r = resolveTemplatePath(sb.flowDir(), "../flowEvil/e.png");
    CHECK_EQ(r.code, ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "outside");
}

TEST(symlink_pointing_outside_is_rejected) {
    Sandbox sb;
    std::error_code ec;
    fs::create_directory_symlink(sb.root / "secret", sb.root / "flow" / "link", ec);
    if (ec) return;  // 平台不允许创建符号链接（如无权限的 Windows），跳过
    auto r = resolveTemplatePath(sb.flowDir(), "link/s.png");
    CHECK_EQ(r.code, ErrCode::InvalidWorkflow);
    CHECK_CONTAINS(r.detail, "outside");
}

TEST(symlink_inside_base_is_fine) {
    Sandbox sb;
    std::error_code ec;
    fs::create_directory_symlink(sb.root / "flow" / "assets", sb.root / "flow" / "alias", ec);
    if (ec) return;
    CHECK(resolveTemplatePath(sb.flowDir(), "alias/a.png").ok());
}
