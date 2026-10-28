#pragma once
// 步骤属性面板。只负责"表单 <-> Step"，不做任何规则判断（规则在 core 的 WorkflowValidator / WorkflowEditor）。
#include <QWidget>
#include <string>
#include <vector>

#include "core/Model.h"
#include "core/Validator.h"

class QCheckBox;
class QComboBox;
class QFormLayout;
class QWidget;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;

class StepPanel : public QWidget {
    Q_OBJECT
public:
    explicit StepPanel(QWidget* parent = nullptr);

    // 显示某一步。不会触发 stepEdited。
    void setStep(const wa::Step& step, const std::vector<std::string>& windowAliases,
                 const std::vector<wa::Issue>& issues);
    void clearStep();
    // 只更新问题列表，不动表单（用户可能正在输入）
    void setIssues(const std::vector<wa::Issue>& issues);

    // 定位器编辑框有三个：步骤自己的 locator、执行后校验 expect、if/while 的条件 condition
    enum Target { TargetLocator = 0, TargetExpect = 1, TargetCondition = 2 };
    // 用于"拾取控件 / 截取模板"把结果写回
    void setLocatorText(const QString& json, int target);

signals:
    // 表单内容整体合法时发出，携带新的 Step
    void stepEdited(const wa::Step& step);
    // 请求拾取控件 / 截取模板；target 见 Target
    void pickRequested(int target);
    void captureRequested(int target);

private slots:
    void onFormChanged();

private:
    void buildUi();
    void updateVisibility();
    void setRowVisible(QWidget* field, bool visible);
    void showStatus(const QString& text, bool error);
    bool collect(wa::Step& out);

    bool loading_ = false;
    wa::Step step_;

    QFormLayout* form_ = nullptr;
    QLineEdit* id_ = nullptr;
    QLabel* type_ = nullptr;
    QComboBox* window_ = nullptr;
    QPlainTextEdit* locator_ = nullptr;
    QWidget* locatorBox_ = nullptr;
    QComboBox* clickMode_ = nullptr;
    QComboBox* scriptLanguage_ = nullptr;
    QPlainTextEdit* scriptCode_ = nullptr;
    QLineEdit* text_ = nullptr;
    QCheckBox* prompt_ = nullptr;
    QCheckBox* sensitive_ = nullptr;
    QComboBox* method_ = nullptr;
    QLineEdit* keys_ = nullptr;
    QSpinBox* duration_ = nullptr;
    QSpinBox* timeout_ = nullptr;
    QCheckBox* expectOn_ = nullptr;
    QPlainTextEdit* expect_ = nullptr;
    QWidget* expectBox_ = nullptr;
    QComboBox* condType_ = nullptr;
    QPlainTextEdit* cond_ = nullptr;
    QWidget* condBox_ = nullptr;
    QSpinBox* times_ = nullptr;
    QSpinBox* maxIter_ = nullptr;
    QSpinBox* interval_ = nullptr;
    QSpinBox* retryTimes_ = nullptr;
    QSpinBox* retryInterval_ = nullptr;
    QComboBox* onFailure_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* issues_ = nullptr;
};
