#pragma once
// 模板文件路径解析：必须落在流程文件所在目录之内。
// 校验器只检查字面路径（不含 .. 、不是绝对路径）；符号链接 / 目录联接（junction）要到运行时才能看出来，所以这里再用
// 规范化后的真实路径做一次检查，防止 "assets" 是指向别处的链接而读到不该读的文件。
#include <string>

#include "core/ErrCode.h"

namespace wa {

// baseDir：流程文件所在目录（UTF-8，绝对或相对均可）；relative：流程里的 template 路径（UTF-8）。
// 成功返回模板文件的规范化绝对路径（UTF-8）。
//   baseDir 为空（流程还没保存）→ InvalidWorkflow
//   文件不存在 / 不是普通文件 → InvalidWorkflow
//   真实路径不在 baseDir 之内 → InvalidWorkflow
Result<std::string> resolveTemplatePath(const std::string& baseDir, const std::string& relative);

}  // namespace wa
