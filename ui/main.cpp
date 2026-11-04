// WinAuto 图形界面。逻辑都在 core（有单元测试），这里只做界面。
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QStringList>

#include "MainWindow.h"

int main(int argc, char** argv) {
    // 坐标统一使用 Win32 物理像素，由 DpiHelper 负责换算；必须在创建 QApplication 之前设置。
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("WinAuto"));
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));

    MainWindow w;
    const QStringList args = app.arguments();
    if (args.size() > 1) w.openFile(args.at(1));
    w.show();
    return app.exec();
}
