#include "core/TemplateAssess.h"

#include <algorithm>

#include "core/TemplateMatch.h"

namespace wa {

Rect expandRegion(Rect s, int margin, int imageW, int imageH) {
    const long long x0 = std::max<long long>(0, static_cast<long long>(s.x) - margin);
    const long long y0 = std::max<long long>(0, static_cast<long long>(s.y) - margin);
    const long long x1 = std::min<long long>(imageW, static_cast<long long>(s.x) + s.w + margin);
    const long long y1 = std::min<long long>(imageH, static_cast<long long>(s.y) + s.h + margin);
    if (x1 <= x0 || y1 <= y0) return Rect{};
    return Rect{static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1 - x0), static_cast<int>(y1 - y0)};
}

TemplateAssessment assessTemplate(const GrayImage& window, Rect selection, const CancelToken& token) {
    TemplateAssessment out;
    if (!window.valid()) {
        out.errors.push_back("窗口截图无效");
        return out;
    }
    int ox = 0, oy = 0;
    Result<GrayImage> crop = cropGray(window, selection.x, selection.y, selection.w, selection.h, &ox, &oy);
    if (!crop.ok()) {
        out.errors.push_back("选区在窗口之外");
        return out;
    }
    const GrayImage& templ = crop.value;
    out.selection = Rect{ox, oy, templ.w, templ.h};

    if (templ.w < kMinTemplateSide || templ.h < kMinTemplateSide) {
        out.errors.push_back("选区太小（至少 " + std::to_string(kMinTemplateSide) + "×" +
                             std::to_string(kMinTemplateSide) + " 像素），特征不足以可靠匹配");
        return out;
    }
    if (static_cast<long long>(templ.w) * templ.h * 4 > static_cast<long long>(window.w) * window.h) {
        out.warnings.push_back("选区很大（超过窗口面积的 1/4）：匹配会变慢，也更容易被界面的细微变化打断；"
                               "建议只框住图标或按钮本身");
    }

    // 自检：把模板放回原图里找。得分阈值取 0.9（和运行时默认一致）
    Result<TemplateMatch> m = findTemplate(window, templ, 0.9, Ambiguity::Error, token);
    switch (m.code) {
        case ErrCode::Ok:
            break;
        case ErrCode::InvalidWorkflow:
            out.errors.push_back("选区几乎是纯色，没有可识别的特征：它会\"匹配\"任何同色区域。请框选有图案或文字的部分");
            break;
        case ErrCode::ElementAmbiguous:
            out.warnings.push_back("窗口里有不止一处和它相同的图案。运行时遇到这种情况会拒绝点击（不猜）。"
                                   "建议限定搜索范围，或换一块更独特的区域");
            out.suggestedRegion = expandRegion(out.selection, 40, window.w, window.h);
            break;
        case ErrCode::Cancelled:
            out.errors.push_back("已取消");
            break;
        default:
            out.errors.push_back("自检失败：在原截图里找不到这块选区本身（可能含有动画、渐变或闪烁内容）。"
                                 "请换一块稳定的区域");
            break;
    }
    out.usable = out.errors.empty();
    return out;
}

}  // namespace wa
