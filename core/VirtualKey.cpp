#include "core/VirtualKey.h"

#include <cctype>
#include <vector>

#include "core/Validator.h"

namespace wa {

std::optional<unsigned> virtualKeyFor(const std::string& key) {
    if (!isSupportedKey(key)) return std::nullopt;
    const std::string n = normalizeKey(key);

    if (n.size() == 1) {
        const char c = n[0];
        if (c >= 'A' && c <= 'Z') return static_cast<unsigned>(c);  // VK_A.. == ASCII
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c);
        return std::nullopt;
    }
    if (n[0] == 'F' && n.size() <= 3 && std::isdigit(static_cast<unsigned char>(n[1]))) {
        const int num = std::stoi(n.substr(1));
        if (num >= 1 && num <= 24) return 0x70u + static_cast<unsigned>(num - 1);  // VK_F1 = 0x70
    }

    struct Entry {
        const char* name;
        unsigned vk;
    };
    static const Entry table[] = {
        {"SHIFT", 0x10},  {"CTRL", 0x11},     {"ALT", 0x12},   {"WIN", 0x5B},
        {"ENTER", 0x0D},  {"TAB", 0x09},      {"ESC", 0x1B},   {"SPACE", 0x20},
        {"BACKSPACE", 0x08}, {"DELETE", 0x2E}, {"INSERT", 0x2D}, {"HOME", 0x24},
        {"END", 0x23},    {"PAGEUP", 0x21},   {"PAGEDOWN", 0x22},
        {"LEFT", 0x25},   {"UP", 0x26},       {"RIGHT", 0x27}, {"DOWN", 0x28},
    };
    for (const Entry& e : table) {
        if (n == e.name) return e.vk;
    }
    return std::nullopt;
}

bool isExtendedKey(unsigned vk) {
    switch (vk) {
        case 0x21:  // PAGEUP
        case 0x22:  // PAGEDOWN
        case 0x23:  // END
        case 0x24:  // HOME
        case 0x25:  // LEFT
        case 0x26:  // UP
        case 0x27:  // RIGHT
        case 0x28:  // DOWN
        case 0x2D:  // INSERT
        case 0x2E:  // DELETE
        case 0x5B:  // LWIN
            return true;
        default:
            return false;
    }
}

Result<HotkeySpec> parseHotkey(const std::string& text) {
    using R = Result<HotkeySpec>;
    std::vector<std::string> parts;
    std::string cur;
    for (char c : text) {
        if (c == '+') {
            parts.push_back(cur);
            cur.clear();
        } else if (c != ' ') {
            cur += c;
        }
    }
    parts.push_back(cur);

    HotkeySpec spec;
    std::string mainKey;
    std::string canonical;
    // 规范化顺序：CTRL, ALT, SHIFT, WIN, 主键
    bool ctrl = false, alt = false, shift = false, win = false;
    for (const std::string& p : parts) {
        if (p.empty()) return R::failure(ErrCode::InvalidWorkflow, "empty key in hotkey \"" + text + "\"");
        if (!isSupportedKey(p)) {
            return R::failure(ErrCode::InvalidWorkflow, "unsupported key \"" + p + "\" in hotkey \"" + text + "\"");
        }
        const std::string n = normalizeKey(p);
        if (n == "CTRL") ctrl = true;
        else if (n == "ALT") alt = true;
        else if (n == "SHIFT") shift = true;
        else if (n == "WIN") win = true;
        else {
            if (!mainKey.empty()) {
                return R::failure(ErrCode::InvalidWorkflow, "hotkey \"" + text + "\" has more than one non-modifier key");
            }
            mainKey = n;
        }
    }
    if (mainKey.empty()) {
        return R::failure(ErrCode::InvalidWorkflow, "hotkey \"" + text + "\" needs a non-modifier key");
    }
    if (!(ctrl || alt || shift || win)) {
        return R::failure(ErrCode::InvalidWorkflow,
                          "hotkey \"" + text + "\" needs at least one modifier (a bare global key would hijack typing)");
    }
    const auto vk = virtualKeyFor(mainKey);
    if (!vk) return R::failure(ErrCode::InvalidWorkflow, "no virtual key for \"" + mainKey + "\"");

    if (ctrl) { spec.modifiers |= kModControl; canonical += "CTRL+"; }
    if (alt) { spec.modifiers |= kModAlt; canonical += "ALT+"; }
    if (shift) { spec.modifiers |= kModShift; canonical += "SHIFT+"; }
    if (win) { spec.modifiers |= kModWin; canonical += "WIN+"; }
    spec.vk = *vk;
    spec.text = canonical + mainKey;
    return R::success(spec);
}

}  // namespace wa
