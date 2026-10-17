#include "WindowsDialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "Util.h"
#include "WindowPickerDialog.h"

WindowsDialog::WindowsDialog(wa::WorkflowEditor& editor, QWidget* parent) : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("目标窗口"));
    resize(720, 360);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("步骤通过别名引用窗口；同一动作可用于不同窗口。双击单元格编辑。\n"
                                                "窗口必须唯一匹配：同一进程有多个窗口时，请用标题关键字区分。"),
                                 this));
    table_ = new QTableWidget(0, 4, this);
    table_->setHorizontalHeaderLabels({QStringLiteral("别名"), QStringLiteral("进程名"),
                                       QStringLiteral("标题包含"), QStringLiteral("等待(ms)")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table_, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    auto* pick = buttons->addButton(QStringLiteral("从窗口列表添加…"), QDialogButtonBox::ActionRole);
    auto* add = buttons->addButton(QStringLiteral("添加空白"), QDialogButtonBox::ActionRole);
    auto* del = buttons->addButton(QStringLiteral("删除"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(pick, &QPushButton::clicked, this, &WindowsDialog::addFromPicker);
    connect(add, &QPushButton::clicked, this, &WindowsDialog::addEmpty);
    connect(del, &QPushButton::clicked, this, &WindowsDialog::removeSelected);
    connect(table_, &QTableWidget::cellChanged, this, &WindowsDialog::onCellChanged);
    reload();
}

void WindowsDialog::reload() {
    reloading_ = true;
    const auto& windows = editor_.workflow().windows;
    table_->setRowCount(static_cast<int>(windows.size()));
    int row = 0;
    for (const auto& [alias, spec] : windows) {
        table_->setItem(row, 0, new QTableWidgetItem(qs(alias)));
        table_->setItem(row, 1, new QTableWidgetItem(qs(spec.processName)));
        table_->setItem(row, 2, new QTableWidgetItem(qs(spec.titleContains)));
        table_->setItem(row, 3, new QTableWidgetItem(QString::number(spec.timeoutMs)));
        ++row;
    }
    reloading_ = false;
}

QString WindowsDialog::aliasAt(int row) const {
    // 别名在表格里可能正被编辑，所以按行序对应 map 的有序遍历
    int i = 0;
    for (const auto& kv : editor_.workflow().windows) {
        if (i++ == row) return qs(kv.first);
    }
    return QString();
}

void WindowsDialog::addFromPicker() {
    WindowPickerDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    const wa::WindowSpec spec = dlg.selected();
    // 别名取进程名去掉 .exe；重名时加序号
    QString base = qs(spec.processName);
    if (base.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) base.chop(4);
    if (base.isEmpty()) base = QStringLiteral("window");
    QString alias = base;
    for (int n = 2; editor_.workflow().windows.count(stdStr(alias)) != 0; ++n) {
        alias = base + QString::number(n);
    }
    editor_.setWindow(stdStr(alias), spec);
    reload();
}

void WindowsDialog::addEmpty() {
    bool ok = false;
    const QString alias = QInputDialog::getText(this, windowTitle(), QStringLiteral("别名："), QLineEdit::Normal,
                                                QString(), &ok)
                              .trimmed();
    if (!ok || alias.isEmpty()) return;
    if (editor_.workflow().windows.count(stdStr(alias)) != 0) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("别名已存在。"));
        return;
    }
    editor_.setWindow(stdStr(alias), wa::WindowSpec());
    reload();
}

void WindowsDialog::removeSelected() {
    const int row = table_->currentRow();
    if (row < 0) return;
    wa::Status st = editor_.removeWindow(stdStr(aliasAt(row)));
    if (!st.ok()) {
        QMessageBox::warning(this, windowTitle(), qs(st.detail));
        return;
    }
    reload();
}

void WindowsDialog::onCellChanged(int row, int column) {
    if (reloading_) return;
    const QString alias = aliasAt(row);
    if (alias.isEmpty()) return;
    const std::string key = stdStr(alias);
    const QString text = table_->item(row, column)->text();

    if (column == 0) {
        wa::Status st = editor_.renameWindow(key, stdStr(text.trimmed()));
        if (!st.ok()) QMessageBox::warning(this, windowTitle(), qs(st.detail));
        reload();  // 失败时恢复原值；成功时按新别名重新排序
        return;
    }
    wa::WindowSpec spec = editor_.workflow().windows.at(key);
    if (column == 1) spec.processName = stdStr(text.trimmed());
    else if (column == 2) spec.titleContains = stdStr(text);
    else {
        bool ok = false;
        const int v = text.toInt(&ok);
        if (ok && v > 0) spec.timeoutMs = v;
    }
    editor_.setWindow(key, spec);
    reload();
}
