// 阶段 0 的 UI 占位：只验证 Qt 5.15 + core 能链接并启动。真正的界面在阶段 4。
#include <QApplication>
#include <QLabel>
#include <QMainWindow>

#include "core/Model.h"

int main(int argc, char** argv) {
    // 坐标统一使用 Win32 物理像素，由 DpiHelper 负责换算；必须在创建 QApplication 之前设置。
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);

    QApplication app(argc, argv);
    QMainWindow w;
    w.setWindowTitle(QStringLiteral("WinAuto"));
    w.setCentralWidget(new QLabel(QStringLiteral("WinAuto stage 0 (schema %1)")
                                      .arg(QString::fromLatin1(wa::kSchemaVersion))));
    w.resize(480, 240);
    w.show();
    return app.exec();
}
