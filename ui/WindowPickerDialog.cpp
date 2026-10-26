#include "WindowPickerDialog.h"

#include <QBrush>
#include <QColor>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "Util.h"

#ifdef WINAUTO_HAVE_PLATFORM
#include "Win32WindowProvider.h"
#endif

WindowPickerDialog::WindowPickerDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("选择目标窗口"));
    resize(860, 420);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("选中一行后确定。标题关键字默认取当前标题，可在窗口列表里再改。\n"
                                                "红色（管理员）窗口无法被本工具操作。"),
                                 this));
    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({QStringLiteral("进程"), QStringLiteral("标题"), QStringLiteral("PID"),
                                       QStringLiteral("窗口大小"), QStringLiteral("权限")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 160);
    table_->setColumnWidth(1, 380);
    table_->setColumnWidth(2, 60);
    table_->setColumnWidth(3, 100);
    layout->addWidget(table_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* refreshBtn = buttons->addButton(QStringLiteral("刷新"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &WindowPickerDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(refreshBtn, &QPushButton::clicked, this, &WindowPickerDialog::refresh);
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this] { accept(); });
    refresh();
}

void WindowPickerDialog::refresh() {
    windows_.clear();
    table_->setRowCount(0);
#ifdef WINAUTO_HAVE_PLATFORM
    wa::platform::Win32WindowProvider provider;
    windows_ = provider.enumerateWindows();
#endif
    table_->setRowCount(static_cast<int>(windows_.size()));
    for (int row = 0; row < static_cast<int>(windows_.size()); ++row) {
        const wa::WindowInfo& w = windows_[static_cast<size_t>(row)];
        const QString cells[5] = {qs(w.processName.empty() ? "(无法读取)" : w.processName), qs(w.title),
                                  QString::number(w.processId),
                                  QStringLiteral("%1*%2").arg(w.bounds.w).arg(w.bounds.h),
                                  w.elevated ? QStringLiteral("管理员") : QStringLiteral("普通")};
        for (int col = 0; col < 5; ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            if (w.elevated) item->setForeground(QBrush(QColor(192, 57, 43)));
            table_->setItem(row, col, item);
        }
    }
#ifndef WINAUTO_HAVE_PLATFORM
    table_->setRowCount(1);
    table_->setItem(0, 0, new QTableWidgetItem(QStringLiteral("窗口枚举仅在 Windows 上可用")));
#endif
}

void WindowPickerDialog::accept() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(windows_.size())) {
        QMessageBox::information(this, windowTitle(), QStringLiteral("请先选中一个窗口。"));
        return;
    }
    const wa::WindowInfo& w = windows_[static_cast<size_t>(row)];
    if (w.elevated) {
        QMessageBox::warning(this, windowTitle(),
                             QStringLiteral("该窗口以管理员权限运行，本工具不会操作它（系统会静默丢弃输入）。"));
        return;
    }
    selected_.processName = w.processName;
    selected_.titleContains = w.title;
    selected_.timeoutMs = 5000;
    QDialog::accept();
}
