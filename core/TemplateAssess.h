#pragma once
// 截取模板时的质量评估：用户在窗口截图上框出一块区域，保存为模板之前先判断它"好不好用"。
// 规则都在 core（有测试），界面只负责显示结论。结论面向用户，所以消息是中文。
#include <optional>
#include <string>
#include <vector>

#include "core/CancelToken.h"
#include "core/GrayImage.h"
#include "core/Model.h"

namespace wa {

struct TemplateAssessment {
    bool usable = false;                // false：有错误，不应保存
    Rect selection;                     // 裁到截图范围内之后的选区
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    // 模板在窗口里出现多处时，建议把搜索范围限定在选区周围（相对截图左上角，即 client 区域左上角）
    std::optional<Rect> suggestedRegion;
};

inline constexpr int kMinTemplateSide = 8;

// window：目标窗口 client 区域的灰度截图；selection：选区（截图坐标）。
// 做三件事：检查尺寸；用匹配器自检（纯色 → 错误；在原图里找不到自己 → 错误；出现多处 → 警告并给出建议范围）。
TemplateAssessment assessTemplate(const GrayImage& window, Rect selection, const CancelToken& token);

// 以 selection 为中心向四周扩 margin 像素，并裁到 [0,w)×[0,h) 内
Rect expandRegion(Rect selection, int margin, int imageW, int imageH);

}  // namespace wa
