#pragma once
// IClipboard 的实现：剪贴板输入用。
//
// 流程：备份当前剪贴板 → 写入文本 → （引擎发 Ctrl+V）→ 还原。要点：
//  - 写入时附带 "ExcludeClipboardContentFromMonitorProcessing / CanIncludeInClipboardHistory=0 /
//    CanUploadToCloudClipboard=0"：Windows 剪贴板历史（Win+V）和云同步不会记录这段文本。
//    这只能挡住遵守约定的程序；其他剪贴板监听软件仍可能读到，所以校验器禁止敏感/运行时询问的文本走剪贴板。
//  - 还原前检查剪贴板序号：粘贴期间用户或别的程序复制了新内容，就不覆盖它。
//  - 备份只覆盖"内存块"类格式（文本、HDROP、DIB、注册格式……）；位图/元文件这类 GDI 句柄格式不备份，
//    若剪贴板里有这类格式，还原时会在 detail 里说明"部分格式未还原"。
//  - 文本里单独的 LF 会换成 CRLF（Windows 剪贴板文本的标准换行）。
//  - 对象必须在使用它的线程里创建（持有一个仅用于剪贴板所有权的隐藏窗口）。
#include <cstdint>
#include <string>
#include <vector>

#include "core/Interfaces.h"

namespace wa::platform {

class Win32Clipboard : public IClipboard {
public:
    Win32Clipboard();
    ~Win32Clipboard() override;
    Win32Clipboard(const Win32Clipboard&) = delete;
    Win32Clipboard& operator=(const Win32Clipboard&) = delete;

    Status replaceText(const std::string& utf8) override;
    Status restore() override;

private:
    struct Saved {
        unsigned format = 0;
        std::vector<std::uint8_t> bytes;
    };
    bool open();  // 带重试：剪贴板常被别的程序短暂占用

    void* owner_ = nullptr;  // HWND
    bool active_ = false;
    bool lossy_ = false;
    std::uint32_t ourSequence_ = 0;
    std::vector<Saved> saved_;
};

}  // namespace wa::platform
