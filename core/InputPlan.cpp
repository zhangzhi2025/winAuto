#include "core/InputPlan.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Validator.h"
#include "core/VirtualKey.h"

namespace wa {

Result<std::vector<KeyEvent>> buildKeySequence(const std::vector<std::string>& keys) {
    using R = Result<std::vector<KeyEvent>>;
    if (keys.empty()) return R::failure(ErrCode::InvalidWorkflow, "hotkey has no keys");

    std::vector<unsigned> mods, mains;
    for (const std::string& k : keys) {
        const auto vk = virtualKeyFor(k);
        if (!vk) return R::failure(ErrCode::InvalidWorkflow, "unsupported key \"" + k + "\"");
        std::vector<unsigned>& target = isModifierKey(k) ? mods : mains;
        if (std::find(target.begin(), target.end(), *vk) == target.end()) target.push_back(*vk);
    }
    if (mains.empty()) return R::failure(ErrCode::InvalidWorkflow, "hotkey needs a non-modifier key");

    std::vector<unsigned> order = mods;
    order.insert(order.end(), mains.begin(), mains.end());

    std::vector<KeyEvent> events;
    for (unsigned vk : order) events.push_back(KeyEvent{vk, true, isExtendedKey(vk)});
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        events.push_back(KeyEvent{*it, false, isExtendedKey(*it)});
    }
    return R::success(std::move(events));
}

namespace {

// 解码下一个码点。成功返回 true 并推进 i；失败返回 false。
bool nextCodePoint(const std::string& s, size_t& i, char32_t* out) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int extra = 0;
    char32_t cp = 0;
    char32_t minValue = 0;
    if (c < 0x80) {
        *out = c;
        ++i;
        return true;
    } else if ((c & 0xE0) == 0xC0) {
        extra = 1; cp = c & 0x1F; minValue = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2; cp = c & 0x0F; minValue = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3; cp = c & 0x07; minValue = 0x10000;
    } else {
        return false;  // 孤立的续字节或 0xF8+
    }
    if (i + static_cast<size_t>(extra) >= s.size()) return false;  // 截断
    for (int k = 1; k <= extra; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
        if ((cc & 0xC0) != 0x80) return false;
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < minValue) return false;                   // 过长编码
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;    // UTF-16 代理区不是合法码点
    if (cp > 0x10FFFF) return false;
    i += static_cast<size_t>(extra) + 1;
    *out = cp;
    return true;
}

void appendUtf16(char32_t cp, std::u16string* out) {
    if (cp < 0x10000) {
        out->push_back(static_cast<char16_t>(cp));
    } else {
        const char32_t v = cp - 0x10000;
        out->push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
        out->push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
    }
}

}  // namespace

Result<std::u16string> utf8ToUtf16(const std::string& utf8) {
    using R = Result<std::u16string>;
    std::u16string out;
    size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = 0;
        const size_t at = i;
        if (!nextCodePoint(utf8, i, &cp)) {
            return R::failure(ErrCode::InvalidWorkflow,
                              "invalid UTF-8 at byte offset " + std::to_string(at));
        }
        appendUtf16(cp, &out);
    }
    return R::success(std::move(out));
}

Result<std::vector<TextEvent>> buildTextEvents(const std::string& utf8) {
    using R = Result<std::vector<TextEvent>>;
    std::vector<TextEvent> events;
    size_t i = 0;
    while (i < utf8.size()) {
        char32_t cp = 0;
        const size_t at = i;
        if (!nextCodePoint(utf8, i, &cp)) {
            return R::failure(ErrCode::InvalidWorkflow, "invalid UTF-8 at byte offset " + std::to_string(at));
        }
        if (cp == U'\r') {
            if (i < utf8.size() && utf8[i] == '\n') ++i;  // \r\n 只算一次换行
            events.push_back(TextEvent{TextEvent::Kind::Key, 0x0D});
        } else if (cp == U'\n') {
            events.push_back(TextEvent{TextEvent::Kind::Key, 0x0D});
        } else if (cp == U'\t') {
            events.push_back(TextEvent{TextEvent::Kind::Key, 0x09});
        } else if (cp < 0x20 || cp == 0x7F) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "U+%04X", static_cast<unsigned>(cp));
            return R::failure(ErrCode::InvalidWorkflow,
                              std::string("control character ") + buf + " is not allowed in typed text");
        } else {
            std::u16string units;
            appendUtf16(cp, &units);
            for (char16_t u : units) events.push_back(TextEvent{TextEvent::Kind::Unicode, u});
        }
    }
    return R::success(std::move(events));
}

Result<Point> resolveWindowPoint(const Rect& c, const WindowPointLocator& l) {
    using R = Result<Point>;
    if (c.w <= 0 || c.h <= 0) {
        return R::failure(ErrCode::ElementNotFound, "window client area is empty (minimized?)");
    }
    long px = 0, py = 0;
    switch (l.anchor) {
        case Anchor::TopLeft:
            px = std::lround(l.x);
            py = std::lround(l.y);
            break;
        case Anchor::BottomRight:
            px = static_cast<long>(c.w) - 1 - std::lround(l.x);
            py = static_cast<long>(c.h) - 1 - std::lround(l.y);
            break;
        case Anchor::Ratio:
            px = std::lround(l.x * (c.w - 1));
            py = std::lround(l.y * (c.h - 1));
            break;
    }
    if (px < 0 || py < 0 || px >= c.w || py >= c.h) {
        return R::failure(ErrCode::ElementNotFound,
                          "point (" + std::to_string(px) + "," + std::to_string(py) +
                              ") is outside the window client area " + std::to_string(c.w) + "x" +
                              std::to_string(c.h) + " (window resized?)");
    }
    return R::success(Point{c.x + static_cast<int>(px), c.y + static_cast<int>(py)});
}

AbsoluteMouse toAbsoluteMouse(Point p, const Rect& v) {
    // 把 [origin, origin+size-1] 线性映射到 [0, 65535]；越界先夹到桌面内。
    // 只有 1 个像素（或无效尺寸）的桌面只有坐标 0 是合法的。
    auto map = [](long long value, long long origin, long long size) -> int {
        if (size <= 1) return 0;
        long long rel = value - origin;
        rel = std::max<long long>(0, std::min<long long>(rel, size - 1));
        return static_cast<int>((rel * 65535 + (size - 1) / 2) / (size - 1));
    };
    AbsoluteMouse m;
    m.x = map(p.x, v.x, v.w);
    m.y = map(p.y, v.y, v.h);
    return m;
}

}  // namespace wa
