#pragma once
// 从当前打开的窗口里选一个，生成 WindowSpec（进程名 + 标题关键字）。
#include <QDialog>

#include "core/Interfaces.h"
#include "core/Model.h"

class QTableWidget;

class WindowPickerDialog : public QDialog {
    Q_OBJECT
public:
    explicit WindowPickerDialog(QWidget* parent = nullptr);
    wa::WindowSpec selected() const { return selected_; }

public slots:
    void accept() override;

private slots:
    void refresh();

private:
    QTableWidget* table_ = nullptr;
    std::vector<wa::WindowInfo> windows_;
    wa::WindowSpec selected_;
};
