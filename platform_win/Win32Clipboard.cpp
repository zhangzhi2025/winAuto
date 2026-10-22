#include "Win32Clipboard.h"

#include <windows.h>

#include <cstring>

#include "Win32Util.h"

namespace wa::platform {

namespace {

constexpr size_t kMaxBackupBytes = 64u << 20;  // 备份总量上限；超过的格式不备份（还原时报"部分格式未还原"）

HWND hwnd(void* p) { return static_cast<HWND>(p); }

bool isGdiHandleFormat(UINT f) {
    return f == CF_BITMAP || f == CF_PALETTE || f == CF_METAFILEPICT || f == CF_ENHMETAFILE ||
           (f >= CF_GDIOBJFIRST && f <= CF_GDIOBJLAST) || f == CF_OWNERDISPLAY ||
           (f >= CF_DSPTEXT && f <= CF_DSPMETAFILEPICT) || f == CF_DSPENHMETAFILE;
}

// 只在剪贴板已打开时调用。成功时 SetClipboardData 接管内存；失败时释放。
bool putBytes(UINT format, const void* data, size_t size) {
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, size ? size : 1);
    if (!mem) return false;
    void* dst = GlobalLock(mem);
    if (!dst) {
        GlobalFree(mem);
        return false;
    }
    if (size) std::memcpy(dst, data, size);
    GlobalUnlock(mem);
    if (!SetClipboardData(format, mem)) {
        GlobalFree(mem);
        return false;
    }
    return true;
}

std::wstring toClipboardText(const std::string& utf8) {
    std::wstring w = toWide(utf8);
    std::wstring out;
    out.reserve(w.size() + 8);
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] == L'\n' && (i == 0 || w[i - 1] != L'\r')) out += L'\r';
        out += w[i];
    }
    return out;
}

}  // namespace

Win32Clipboard::Win32Clipboard() {
    // 隐藏的仅消息窗口，只用来当剪贴板所有者：OpenClipboard(NULL) 之后 EmptyClipboard 会使 SetClipboardData 失败
    owner_ = CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
}

Win32Clipboard::~Win32Clipboard() {
    if (active_) restore();  // 尽力还原；析构时没有地方报告错误
    if (owner_) DestroyWindow(hwnd(owner_));
}

bool Win32Clipboard::open() {
    for (int i = 0; i < 20; ++i) {
        if (OpenClipboard(hwnd(owner_))) return true;
        Sleep(25);
    }
    return false;
}

Status Win32Clipboard::replaceText(const std::string& utf8) {
    if (!owner_) return Status::failure(ErrCode::Internal, "cannot create the clipboard owner window");
    if (active_) return Status::failure(ErrCode::Internal, "a clipboard replacement is already in progress");
    if (!open()) {
        return Status::failure(ErrCode::InputBlocked, "the clipboard is locked by another program");
    }

    // 1) 只读备份。此时还没改任何东西，失败可以直接返回。
    saved_.clear();
    lossy_ = false;
    size_t total = 0;
    for (UINT f = EnumClipboardFormats(0); f != 0; f = EnumClipboardFormats(f)) {
        if (isGdiHandleFormat(f)) {
            // 位图和元文件通常能由 DIB 自动合成；只有它们是"唯一格式"时才真正丢失，这里保守地标记
            if (f != CF_BITMAP && f != CF_ENHMETAFILE && f != CF_METAFILEPICT) lossy_ = true;
            else if (!IsClipboardFormatAvailable(CF_DIB)) lossy_ = true;
            continue;
        }
        HANDLE h = GetClipboardData(f);
        if (!h) {
            lossy_ = true;
            continue;
        }
        const SIZE_T size = GlobalSize(static_cast<HGLOBAL>(h));
        if (size == 0 || total + size > kMaxBackupBytes) {
            lossy_ = true;
            continue;
        }
        const void* src = GlobalLock(static_cast<HGLOBAL>(h));
        if (!src) {
            lossy_ = true;
            continue;
        }
        Saved s;
        s.format = f;
        s.bytes.assign(static_cast<const std::uint8_t*>(src), static_cast<const std::uint8_t*>(src) + size);
        GlobalUnlock(static_cast<HGLOBAL>(h));
        total += size;
        saved_.push_back(std::move(s));
    }

    // 2) 写入文本 + 不进历史/不上云 标记
    bool ok = EmptyClipboard() != FALSE;
    if (ok) {
        const std::wstring text = toClipboardText(utf8);
        ok = putBytes(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    }
    if (ok) {
        const DWORD one = 1, zero = 0;
        if (const UINT fmt = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing")) putBytes(fmt, &one, sizeof one);
        if (const UINT fmt = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory")) putBytes(fmt, &zero, sizeof zero);
        if (const UINT fmt = RegisterClipboardFormatW(L"CanUploadToCloudClipboard")) putBytes(fmt, &zero, sizeof zero);
    } else {
        // 清空了却没写成：把备份放回去，保证"失败时剪贴板不变"
        if (!saved_.empty()) {
            EmptyClipboard();
            for (const Saved& s : saved_) putBytes(s.format, s.bytes.data(), s.bytes.size());
        }
        CloseClipboard();
        saved_.clear();
        return Status::failure(ErrCode::InputBlocked, "could not write the text to the clipboard");
    }
    CloseClipboard();
    ourSequence_ = GetClipboardSequenceNumber();
    active_ = true;
    return okStatus();
}

Status Win32Clipboard::restore() {
    if (!active_) return okStatus();
    active_ = false;
    std::vector<Saved> saved = std::move(saved_);
    saved_.clear();
    const bool lossy = lossy_;

    // 粘贴期间剪贴板被别人改了：那是用户刚复制的新内容，不能用旧备份覆盖
    if (GetClipboardSequenceNumber() != ourSequence_) {
        Status st = okStatus();  // 不是错误，只是说明一下
        st.detail = "the clipboard changed during the paste; left as it is instead of restoring the old content";
        return st;
    }
    if (!open()) {
        return Status::failure(ErrCode::InputBlocked, "the clipboard is locked by another program");
    }
    EmptyClipboard();
    for (const Saved& s : saved) putBytes(s.format, s.bytes.data(), s.bytes.size());
    CloseClipboard();

    Status st = okStatus();
    if (lossy) st.detail = "clipboard restored, but some formats (bitmap/metafile handles) could not be backed up";
    return st;
}

}  // namespace wa::platform
