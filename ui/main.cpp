// WinAuto 图形界面。逻辑都在 core（有单元测试），这里只做界面。
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QStringList>

#include "MainWindow.h"

int main(int argc, char** argv) {
    // 坐标统一使用 Win32 物理像素，由 DpiHelper 负责换算；必须在创建 QApplication 之前设置。
    QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("WinAuto"));
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));

    QString workflowPath;
    QString logFilePath;
    bool logFileSpecified = false;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QStringLiteral("--log-file")) {
            if (logFileSpecified || i + 1 >= args.size() || args.at(i + 1).isEmpty() ||
                args.at(i + 1).startsWith(QStringLiteral("--"))) {
                QMessageBox::critical(nullptr, QStringLiteral("启动参数错误"),
                                      QStringLiteral("用法：winauto-ui [--log-file <日志文件>] [流程.json]"));
                return 2;
            }
            logFileSpecified = true;
            logFilePath = args.at(++i);
        } else if (args.at(i).startsWith(QStringLiteral("--")) || !workflowPath.isEmpty()) {
            QMessageBox::critical(nullptr, QStringLiteral("启动参数错误"),
                                  QStringLiteral("用法：winauto-ui [--log-file <日志文件>] [流程.json]"));
            return 2;
        } else {
            workflowPath = args.at(i);
        }
    }
    if (!logFilePath.isEmpty() && !workflowPath.isEmpty() &&
        QDir::cleanPath(QFileInfo(logFilePath).absoluteFilePath())
                .compare(QDir::cleanPath(QFileInfo(workflowPath).absoluteFilePath()), Qt::CaseInsensitive) == 0) {
        QMessageBox::critical(nullptr, QStringLiteral("启动参数错误"),
                              QStringLiteral("日志文件不能与流程文件相同。"));
        return 2;
    }

    MainWindow w;
    if (!logFilePath.isEmpty()) {
        QString error;
        if (!w.openLogFile(logFilePath, &error)) {
            QMessageBox::critical(nullptr, QStringLiteral("无法打开日志文件"),
                                  QStringLiteral("%1\n%2").arg(logFilePath, error));
            return 1;
        }
    }
    if (!workflowPath.isEmpty()) w.openFile(workflowPath);
    w.show();
    return app.exec();
}
