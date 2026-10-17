#pragma once
// 窗口别名管理。所有修改直接走 WorkflowEditor（可撤销），表格只是它的投影。
#include <QDialog>

#include "core/WorkflowEditor.h"

class QTableWidget;

class WindowsDialog : public QDialog {
    Q_OBJECT
public:
    WindowsDialog(wa::WorkflowEditor& editor, QWidget* parent = nullptr);

private slots:
    void addFromPicker();
    void addEmpty();
    void removeSelected();
    void onCellChanged(int row, int column);

private:
    void reload();
    QString aliasAt(int row) const;

    wa::WorkflowEditor& editor_;
    QTableWidget* table_ = nullptr;
    bool reloading_ = false;
};
