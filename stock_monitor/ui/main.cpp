#include <windows.h>

#include <QApplication>

#include "StockMonitorWindow.h"

namespace {
void setPerMonitorDpiAwareness() {
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const HMODULE user32 = GetModuleHandleW(L"user32.dll");
    const auto setContext = user32 ? reinterpret_cast<SetContextFn>(
                                        GetProcAddress(user32, "SetProcessDpiAwarenessContext"))
                                   : nullptr;
    if (setContext) {
        setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
}
}  // namespace

int main(int argc, char* argv[]) {
    setPerMonitorDpiAwareness();
    QApplication application(argc, argv);
    application.setOrganizationName(QStringLiteral("WinAuto"));
    application.setApplicationName(QStringLiteral("StockMonitor"));
    stock_monitor::StockMonitorWindow window;
    window.show();
    return application.exec();
}
