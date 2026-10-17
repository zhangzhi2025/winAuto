#pragma once
// DPI 与坐标：整个程序统一使用"虚拟桌面物理像素"。
// 做法：进程声明 Per-Monitor V2 感知（清单或本函数），Win32 返回的所有坐标即为物理像素；
// Qt 一侧用 Qt::AA_DisableHighDpiScaling 关闭自动缩放，两边不再混用逻辑坐标。
#include <cstdint>

#include "core/Model.h"

namespace wa::platform {

// 必须在创建任何窗口之前调用。已通过清单声明过时返回 true（重复设置被系统拒绝是正常的）。
bool enablePerMonitorDpiAwareness();

// 当前线程是否处于 Per-Monitor V2 感知。坐标类输入（点击）依赖它：
// 感知方式不对，Win32 返回/接受的坐标会被系统缩放，点击会偏位。
bool isPerMonitorV2Aware();

// 窗口所在显示器的 DPI（96 = 100%）。系统过旧或失败时返回 96。
unsigned dpiForWindow(std::uint64_t hwnd);

// 虚拟桌面范围（所有显示器的外接矩形，x/y 可能为负）
Rect virtualScreenRect();

}  // namespace wa::platform
