#include "core/Summary.h"

#include <sstream>

namespace wa {

namespace {

// 按 UTF-8 字符（而不是字节）截断，避免切坏多字节字符
std::string truncateUtf8(const std::string& s, size_t maxChars) {
    size_t chars = 0;
    size_t i = 0;
    while (i < s.size()) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (chars == maxChars) return s.substr(0, i) + "…";
            ++chars;
        }
        ++i;
    }
    return s;
}

std::string describeLeaf(const LeafLocator& l) {
    std::ostringstream os;
    if (const auto* u = std::get_if<UiaLocator>(&l)) {
        os << "uia(";
        bool first = true;
        auto add = [&](const char* k, const std::string& v) {
            if (v.empty()) return;
            os << (first ? "" : ", ") << k << "=" << v;
            first = false;
        };
        add("automationId", u->automationId);
        add("name", u->name);
        add("controlType", u->controlType);
        os << ")";
    } else if (const auto* w = std::get_if<WindowPointLocator>(&l)) {
        os << "window_point(" << w->x << "," << w->y << (w->anchor == Anchor::Ratio ? ", ratio" : "") << ")";
    } else if (const auto* s = std::get_if<ScreenPointLocator>(&l)) {
        os << "screen_point(" << s->x << "," << s->y << ")";
    } else if (const auto* i = std::get_if<ImageLocator>(&l)) {
        os << "image(" << i->templatePath << ")";
    }
    return os.str();
}

std::string describeLocator(const Locator& loc) {
    if (const auto* f = std::get_if<FallbackLocator>(&loc)) {
        std::string out = "fallback[";
        for (size_t i = 0; i < f->items.size(); ++i) out += (i ? " -> " : "") + describeLeaf(f->items[i]);
        return out + "]";
    }
    return std::visit(
        [](auto&& v) -> std::string {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, FallbackLocator>) return {};
            else return describeLeaf(LeafLocator{v});
        },
        loc);
}

std::string describeKeys(const std::vector<std::string>& keys) {
    std::string out;
    for (size_t i = 0; i < keys.size(); ++i) out += (i ? "+" : "") + keys[i];
    return out;
}

}  // namespace

std::string summarizeWorkflow(const Workflow& wf, const ValidationReport& report) {
    std::ostringstream os;
    os << "流程：" << wf.name << "（" << wf.id << "），共 " << wf.steps.size() << " 步\n\n";

    os << "将操作的窗口：\n";
    for (const auto& [alias, spec] : wf.windows) {
        os << "  " << alias << " -> ";
        if (!spec.processName.empty()) os << "进程 " << spec.processName;
        if (!spec.titleContains.empty()) {
            os << (spec.processName.empty() ? "" : "，") << "标题含 \"" << spec.titleContains << "\"";
        }
        os << "\n";
    }

    os << "\n将执行的操作：\n";
    for (const Step& s : wf.steps) {
        os << "  " << s.id << "  ";
        switch (s.type) {
            case StepType::ActivateWindow: os << "激活窗口 " << s.window; break;
            case StepType::Click:
                os << "点击 " << (s.locator ? describeLocator(*s.locator) : "?");
                break;
            case StepType::SetValue:
            case StepType::InputText: {
                os << (s.type == StepType::SetValue ? "设置值 " : "输入文本 ");
                if (s.textFromPrompt) os << "[运行时询问，不显示]";
                else if (s.sensitive) os << "[已隐藏]";
                else os << "\"" << truncateUtf8(s.text.value_or(""), 40) << "\"";
                if (s.type == StepType::SetValue && s.locator) os << " -> " << describeLocator(*s.locator);
                break;
            }
            case StepType::Hotkey: os << "按键 " << describeKeys(s.keys); break;
            case StepType::Wait: os << "等待 " << s.durationMs.value_or(0) << " ms"; break;
            case StepType::WaitForElement:
                os << "等待控件 " << (s.locator ? describeLocator(*s.locator) : "?");
                break;
            case StepType::AssertElement:
                os << "检查控件 " << (s.locator ? describeLocator(*s.locator) : "?");
                break;
            case StepType::Unknown: os << "未知操作 " << s.rawType; break;
        }
        os << "\n";
    }

    bool anyRisk = false;
    for (const auto& i : report.issues) {
        if (!i.requiresConfirmation) continue;
        if (!anyRisk) os << "\n⚠ 高风险操作（需要确认）：\n";
        anyRisk = true;
        os << "  " << i.path << ": " << i.message << "\n";
    }

    bool anyWarn = false;
    for (const auto& i : report.issues) {
        if (i.severity != Severity::Warning || i.requiresConfirmation) continue;
        if (!anyWarn) os << "\n警告：\n";
        anyWarn = true;
        os << "  " << i.path << ": " << i.message << "\n";
    }
    return os.str();
}

}  // namespace wa
