#pragma once
// 控件拾取：读取目标窗口的 UI Automation 控件树，选中后生成最稳的定位器。
// 也支持"鼠标拾取"：倒计时后取鼠标下的控件。
// 非 Windows 构建中只显示一个说明。
#include <QDialog>

#include "core/Model.h"
#include "core/UiaSnapshot.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace wa::platform {
class UiaSession;
}

class ElementPickerDialog : public QDialog {
    Q_OBJECT
public:
    ElementPickerDialog(const wa::WindowSpec& window, QWidget* parent = nullptr);
    ~ElementPickerDialog() override;

    // 选中并确认后的定位器 JSON
    QString locatorJson() const { return locatorJson_; }

public slots:
    void accept() override;

private slots:
    void loadTree();
    void startHoverPick();
    void onHoverTick();
    void onSelectionChanged();
    void applyFilter(const QString& text);

private:
    void selectNode(int nodeIndex);

    wa::WindowSpec window_;
    wa::UiaSnapshot snapshot_;
    std::vector<QTreeWidgetItem*> items_;  // 与 snapshot_.nodes 同序
    QString locatorJson_;
    bool haveSuggestion_ = false;

    QTreeWidget* tree_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QLabel* info_ = nullptr;
    QPlainTextEdit* suggestion_ = nullptr;
    QPushButton* okButton_ = nullptr;
    QPushButton* hoverButton_ = nullptr;
    QTimer* timer_ = nullptr;
    int countdown_ = 0;
    std::uint64_t windowHandle_ = 0;
    wa::platform::UiaSession* session_ = nullptr;
};
