#pragma once
// 模板匹配：零均值归一化互相关（ZNCC），对整体亮度/对比度变化不敏感；金字塔由粗到精加速。
// 安全相关的规则（不猜、不默认取第一个）在这里，并有单元测试。
#include "core/CancelToken.h"
#include "core/ErrCode.h"
#include "core/GrayImage.h"
#include "core/Model.h"

namespace wa {

struct TemplateMatch {
    Rect rect;      // 匹配位置（haystack 坐标）
    double score = 0.0;  // ZNCC，[-1, 1]，1 为完全一致
};

// 在 haystack 里找 templ。
//   - 模板无对比度（纯色）→ InvalidWorkflow：这样的模板会"匹配"任何同色区域
//   - 模板比搜索图大 → ElementNotFound
//   - 最佳得分 < minConfidence → ElementNotFound，detail 带最佳得分，便于调阈值
//   - 另有一个不重叠的位置得分也 >= minConfidence：ambiguity=Error → ElementAmbiguous（列出位置）；
//     ambiguity=First → 取得分最高者，得分相近时取最靠上（再靠左）的
//   - 取消 → Cancelled
// 粗到精搜索可能漏掉在低分辨率下特征消失的目标；模板应是有明显结构的小图（图标、按钮），不要含大片渐变/动画。
Result<TemplateMatch> findTemplate(const GrayImage& haystack, const GrayImage& templ, double minConfidence,
                                   Ambiguity ambiguity, const CancelToken& token);

}  // namespace wa
