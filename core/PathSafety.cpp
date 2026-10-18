#include "core/PathSafety.h"

#include <filesystem>
#include <system_error>

namespace wa {

namespace fs = std::filesystem;

namespace {
// 是否 child 在 base 之内（两者都已规范化）：按路径分量逐个比较，避免 "/a/bc" 被当成在 "/a/b" 之内
bool isInside(const fs::path& base, const fs::path& child) {
    auto b = base.begin();
    auto c = child.begin();
    for (; b != base.end(); ++b, ++c) {
        if (c == child.end() || *b != *c) return false;
    }
    return true;
}
}  // namespace

Result<std::string> resolveTemplatePath(const std::string& baseDir, const std::string& relative) {
    using R = Result<std::string>;
    if (baseDir.empty()) {
        return R::failure(ErrCode::InvalidWorkflow,
                          "image templates are resolved relative to the workflow file; save the workflow first");
    }
    if (relative.empty()) return R::failure(ErrCode::InvalidWorkflow, "template path is empty");

    std::error_code ec;
    const fs::path base = fs::canonical(fs::u8path(baseDir), ec);
    if (ec) return R::failure(ErrCode::InvalidWorkflow, "workflow directory is not accessible: " + baseDir);

    const fs::path joined = base / fs::u8path(relative);
    const fs::path real = fs::canonical(joined, ec);
    if (ec) return R::failure(ErrCode::InvalidWorkflow, "template file not found: " + relative);
    if (!isInside(base, real)) {
        return R::failure(ErrCode::InvalidWorkflow,
                          "template resolves outside the workflow directory (link or junction?): " + relative);
    }
    if (!fs::is_regular_file(real, ec) || ec) {
        return R::failure(ErrCode::InvalidWorkflow, "template is not a regular file: " + relative);
    }
    return R::success(real.u8string());
}

}  // namespace wa
