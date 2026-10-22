#include "StepPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include "Util.h"
#include "core/Json.h"

namespace {
bool isBlock(wa::StepType t) { return wa::isBlockType(t); }

QString joinKeys(const std::vector<std::string>& keys) {
    QStringList parts;
    for (const auto& k : keys) parts << qs(k);
    return parts.join(QStringLiteral("+"));
}

std::vector<std::string> splitKeys(const QString& text) {
    std::vector<std::string> keys;
    for (const QString& part : text.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
        const QString k = part.trimmed();
        if (!k.isEmpty()) keys.push_back(stdStr(k));
    }
    return keys;
}
}  // namespace

StepPanel::StepPanel(QWidget* parent) : QWidget(parent) { buildUi(); }

void StepPanel::buildUi() {
    auto* root = new QVBoxLayout(this);
    form_ = new QFormLayout();
    root->addLayout(form_);

    id_ = new QLineEdit(this);
    type_ = new QLabel(this);
    window_ = new QComboBox(this);

    locator_ = new QPlainTextEdit(this);
    locator_->setPlaceholderText(QStringLiteral("定位器 JSON；可点\"拾取控件\"自动生成"));
    locator_->setFixedHeight(110);
    auto* pick = new QPushButton(QStringLiteral("拾取控件…"), this);
    auto* capture = new QPushButton(QStringLiteral("截取模板…"), this);
    capture->setToolTip(QStringLiteral("截下目标窗口当前画面，框选一块作为图像模板（image 定位器）"));
    locatorBox_ = new QWidget(this);
    auto* lb = new QVBoxLayout(locatorBox_);
    lb->setContentsMargins(0, 0, 0, 0);
    lb->addWidget(locator_);
    auto* lrow = new QHBoxLayout();
    lrow->addWidget(pick);
    lrow->addWidget(capture);
    lb->addLayout(lrow);

    clickMode_ = new QComboBox(this);
    clickMode_->addItem(QStringLiteral("自动（优先 Invoke）"), QStringLiteral("auto"));
    clickMode_->addItem(QStringLiteral("坐标点击"), QStringLiteral("point"));

    text_ = new QLineEdit(this);
    prompt_ = new QCheckBox(QStringLiteral("运行时询问（不保存到文件）"), this);
    sensitive_ = new QCheckBox(QStringLiteral("敏感内容（日志中隐藏）"), this);
    method_ = new QComboBox(this);
    method_->addItem(QStringLiteral("Unicode 逐字输入"), QStringLiteral("unicode"));
    method_->addItem(QStringLiteral("剪贴板粘贴（暂时占用剪贴板，输入后还原）"), QStringLiteral("clipboard"));

    keys_ = new QLineEdit(this);
    keys_->setPlaceholderText(QStringLiteral("如 CTRL+S"));

    duration_ = new QSpinBox(this);
    duration_->setRange(0, 600000);
    duration_->setSuffix(QStringLiteral(" ms"));

    timeout_ = new QSpinBox(this);
    timeout_->setRange(0, 600000);
    timeout_->setSpecialValueText(QStringLiteral("使用流程默认值"));
    timeout_->setSuffix(QStringLiteral(" ms"));

    expectOn_ = new QCheckBox(QStringLiteral("执行后校验元素存在（expect）"), this);
    expect_ = new QPlainTextEdit(this);
    expect_->setFixedHeight(90);
    auto* pickExpect = new QPushButton(QStringLiteral("拾取控件…"), this);
    auto* captureExpect = new QPushButton(QStringLiteral("截取模板…"), this);
    expectBox_ = new QWidget(this);
    auto* eb = new QVBoxLayout(expectBox_);
    eb->setContentsMargins(0, 0, 0, 0);
    eb->addWidget(expect_);
    auto* erow = new QHBoxLayout();
    erow->addWidget(pickExpect);
    erow->addWidget(captureExpect);
    eb->addLayout(erow);

    // if / while 的条件
    condType_ = new QComboBox(this);
    condType_->addItem(QStringLiteral("控件存在时成立"), QStringLiteral("element_exists"));
    condType_->addItem(QStringLiteral("控件不存在时成立"), QStringLiteral("element_missing"));
    cond_ = new QPlainTextEdit(this);
    cond_->setFixedHeight(110);
    cond_->setPlaceholderText(QStringLiteral("条件的定位器 JSON；可点\"拾取控件\"或\"截取模板\""));
    auto* pickCond = new QPushButton(QStringLiteral("拾取控件…"), this);
    auto* captureCond = new QPushButton(QStringLiteral("截取模板…"), this);
    condBox_ = new QWidget(this);
    auto* cb = new QVBoxLayout(condBox_);
    cb->setContentsMargins(0, 0, 0, 0);
    cb->addWidget(cond_);
    auto* crow = new QHBoxLayout();
    crow->addWidget(pickCond);
    crow->addWidget(captureCond);
    cb->addLayout(crow);

    times_ = new QSpinBox(this);
    times_->setRange(1, wa::kMaxLoopCount);
    times_->setSuffix(QStringLiteral(" 次"));
    maxIter_ = new QSpinBox(this);
    maxIter_->setRange(1, wa::kMaxLoopCount);
    maxIter_->setSuffix(QStringLiteral(" 次"));
    maxIter_->setToolTip(QStringLiteral("条件跑满这么多轮仍成立会判为失败，而不是无限循环"));
    interval_ = new QSpinBox(this);
    interval_->setRange(0, 600000);
    interval_->setSuffix(QStringLiteral(" ms"));
    interval_->setValue(wa::kDefaultLoopIntervalMs);

    retryTimes_ = new QSpinBox(this);
    retryTimes_->setRange(0, 20);
    retryTimes_->setSpecialValueText(QStringLiteral("不重试"));
    retryInterval_ = new QSpinBox(this);
    retryInterval_->setRange(0, 600000);
    retryInterval_->setSuffix(QStringLiteral(" ms"));
    retryInterval_->setValue(500);

    onFailure_ = new QComboBox(this);
    onFailure_->addItem(QStringLiteral("使用流程默认值"), QString());
    onFailure_->addItem(QStringLiteral("失败即停止"), QStringLiteral("stop"));
    onFailure_->addItem(QStringLiteral("忽略失败继续"), QStringLiteral("continue"));

    form_->addRow(QStringLiteral("编号"), id_);
    form_->addRow(QStringLiteral("类型"), type_);
    form_->addRow(QStringLiteral("窗口"), window_);
    form_->addRow(QStringLiteral("条件"), condType_);
    form_->addRow(QStringLiteral("条件定位器"), condBox_);
    form_->addRow(QStringLiteral("重复次数"), times_);
    form_->addRow(QStringLiteral("最多循环"), maxIter_);
    form_->addRow(QStringLiteral("每轮间隔"), interval_);
    form_->addRow(QStringLiteral("定位器"), locatorBox_);
    form_->addRow(QStringLiteral("点击方式"), clickMode_);
    form_->addRow(QStringLiteral("文本"), text_);
    form_->addRow(QString(), prompt_);
    form_->addRow(QString(), sensitive_);
    form_->addRow(QStringLiteral("输入方式"), method_);
    form_->addRow(QStringLiteral("按键"), keys_);
    form_->addRow(QStringLiteral("等待时长"), duration_);
    form_->addRow(QStringLiteral("超时"), timeout_);
    form_->addRow(QString(), expectOn_);
    form_->addRow(QStringLiteral("expect 定位器"), expectBox_);
    form_->addRow(QStringLiteral("失败重试"), retryTimes_);
    form_->addRow(QStringLiteral("重试间隔"), retryInterval_);
    form_->addRow(QStringLiteral("失败策略"), onFailure_);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    issues_ = new QLabel(this);
    issues_->setWordWrap(true);
    issues_->setTextFormat(Qt::PlainText);
    root->addWidget(status_);
    root->addWidget(issues_);
    root->addStretch(1);

    connect(id_, &QLineEdit::editingFinished, this, &StepPanel::onFormChanged);
    connect(window_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StepPanel::onFormChanged);
    connect(clickMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StepPanel::onFormChanged);
    connect(method_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StepPanel::onFormChanged);
    connect(onFailure_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StepPanel::onFormChanged);
    connect(text_, &QLineEdit::editingFinished, this, &StepPanel::onFormChanged);
    connect(keys_, &QLineEdit::editingFinished, this, &StepPanel::onFormChanged);
    connect(prompt_, &QCheckBox::toggled, this, &StepPanel::onFormChanged);
    connect(sensitive_, &QCheckBox::toggled, this, &StepPanel::onFormChanged);
    connect(expectOn_, &QCheckBox::toggled, this, &StepPanel::onFormChanged);
    connect(duration_, &QSpinBox::editingFinished, this, &StepPanel::onFormChanged);
    connect(timeout_, &QSpinBox::editingFinished, this, &StepPanel::onFormChanged);
    connect(times_, &QSpinBox::editingFinished, this, &StepPanel::onFormChanged);
    connect(maxIter_, &QSpinBox::editingFinished, this, &StepPanel::onFormChanged);
    connect(interval_, &QSpinBox::editingFinished, this, &StepPanel::onFormChanged);
    connect(condType_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &StepPanel::onFormChanged);
    connect(retryTimes_, &QSpinBox::editingFinished, this, &StepPanel::onFormChanged);
    connect(retryInterval_, &QSpinBox::editingFinished, this, &StepPanel::onFormChanged);
    // QPlainTextEdit 没有 editingFinished：用"失去焦点"之外的显式按钮会打断流程，
    // 所以在文本变化后用单次定时器合并提交（避免每敲一个字符就写入撤销历史）
    auto* locTimer = new QTimer(this);
    locTimer->setSingleShot(true);
    locTimer->setInterval(600);
    connect(locator_, &QPlainTextEdit::textChanged, locTimer, QOverload<>::of(&QTimer::start));
    connect(expect_, &QPlainTextEdit::textChanged, locTimer, QOverload<>::of(&QTimer::start));
    connect(cond_, &QPlainTextEdit::textChanged, locTimer, QOverload<>::of(&QTimer::start));
    connect(locTimer, &QTimer::timeout, this, &StepPanel::onFormChanged);
    connect(pick, &QPushButton::clicked, this, [this] { emit pickRequested(TargetLocator); });
    connect(pickExpect, &QPushButton::clicked, this, [this] { emit pickRequested(TargetExpect); });
    connect(pickCond, &QPushButton::clicked, this, [this] { emit pickRequested(TargetCondition); });
    connect(capture, &QPushButton::clicked, this, [this] { emit captureRequested(TargetLocator); });
    connect(captureExpect, &QPushButton::clicked, this, [this] { emit captureRequested(TargetExpect); });
    connect(captureCond, &QPushButton::clicked, this, [this] { emit captureRequested(TargetCondition); });

    clearStep();
}

void StepPanel::clearStep() {
    loading_ = true;
    setEnabled(false);
    id_->clear();
    type_->clear();
    status_->clear();
    issues_->clear();
    loading_ = false;
    step_ = wa::Step();
}

void StepPanel::setStep(const wa::Step& step, const std::vector<std::string>& aliases,
                        const std::vector<wa::Issue>& issues) {
    loading_ = true;
    setEnabled(true);
    step_ = step;

    id_->setText(qs(step.id));
    type_->setText(qs(step.rawType.empty() ? wa::toString(step.type) : step.rawType));

    window_->clear();
    window_->addItem(QStringLiteral("（无）"), QString());
    for (const auto& a : aliases) window_->addItem(qs(a), qs(a));
    int wi = window_->findData(qs(step.window));
    if (wi < 0 && !step.window.empty()) {
        window_->addItem(qs(step.window) + QStringLiteral("（未定义）"), qs(step.window));
        wi = window_->count() - 1;
    }
    window_->setCurrentIndex(wi < 0 ? 0 : wi);

    locator_->setPlainText(step.locator ? qs(wa::locatorToJson(*step.locator)) : QString());
    clickMode_->setCurrentIndex(step.clickMode == wa::ClickMode::Point ? 1 : 0);
    text_->setText(step.text ? qs(*step.text) : QString());
    prompt_->setChecked(step.textFromPrompt);
    sensitive_->setChecked(step.sensitive);
    method_->setCurrentIndex(step.method == wa::InputMethod::Clipboard ? 1 : 0);
    keys_->setText(joinKeys(step.keys));
    duration_->setValue(step.durationMs.value_or(0));
    timeout_->setValue(step.timeoutMs.value_or(0));

    expectOn_->setChecked(step.expect.has_value());
    expect_->setPlainText(step.expect ? qs(wa::locatorToJson(step.expect->locator)) : QString());

    condType_->setCurrentIndex(step.condition && step.condition->type == "element_missing" ? 1 : 0);
    cond_->setPlainText(step.condition ? qs(wa::locatorToJson(step.condition->locator)) : QString());
    times_->setValue(step.times.value_or(1));
    maxIter_->setValue(step.maxIterations.value_or(10));
    interval_->setValue(step.intervalMs.value_or(wa::kDefaultLoopIntervalMs));

    retryTimes_->setValue(step.retry ? step.retry->times : 0);
    retryInterval_->setValue(step.retry ? step.retry->intervalMs : 500);
    int fi = 0;
    if (step.onFailure) fi = *step.onFailure == wa::FailurePolicy::Stop ? 1 : 2;
    onFailure_->setCurrentIndex(fi);

    setIssues(issues);
    status_->clear();
    updateVisibility();
    loading_ = false;
}

void StepPanel::setIssues(const std::vector<wa::Issue>& issues) {
    QStringList lines;
    for (const auto& i : issues) {
        lines << QStringLiteral("%1 %2: %3")
                     .arg(i.severity == wa::Severity::Error ? QStringLiteral("✖") : QStringLiteral("⚠"),
                          qs(i.path), qs(i.message));
    }
    issues_->setText(lines.join(QLatin1Char('\n')));
}

// QFormLayout::setRowVisible 是 Qt 6.4 才有（Qt 5.15 没有）；这里用控件自身和它的标签控制显隐。
void StepPanel::setRowVisible(QWidget* field, bool visible) {
    if (QWidget* label = form_->labelForField(field)) label->setVisible(visible);
    field->setVisible(visible);
}

void StepPanel::updateVisibility() {
    using wa::StepType;
    const StepType t = step_.type;
    const bool locatorStep = t == StepType::Click || t == StepType::SetValue ||
                             t == StepType::WaitForElement || t == StepType::AssertElement;
    const bool textStep = t == StepType::InputText || t == StepType::SetValue;
    const bool waits = t == StepType::Wait;
    const bool isIf = t == StepType::If;
    const bool isWhile = t == StepType::While;
    const bool isRepeat = t == StepType::Repeat;
    const bool cond = isIf || isWhile;
    const bool block = cond || isRepeat;

    setRowVisible(window_, !waits && !isRepeat);
    setRowVisible(condType_, cond);
    setRowVisible(condBox_, cond);
    setRowVisible(times_, isRepeat);
    setRowVisible(maxIter_, isWhile);
    setRowVisible(interval_, isRepeat || isWhile);
    setRowVisible(locatorBox_, locatorStep);
    setRowVisible(clickMode_, t == StepType::Click);
    setRowVisible(text_, textStep);
    setRowVisible(prompt_, textStep);
    setRowVisible(sensitive_, textStep);
    setRowVisible(method_, t == StepType::InputText);
    setRowVisible(keys_, t == StepType::Hotkey);
    setRowVisible(duration_, waits);
    // if / while 的"超时"是等条件成立的最长时间；0 表示只检查一次
    timeout_->setSpecialValueText(cond ? QStringLiteral("只检查一次") : QStringLiteral("使用流程默认值"));
    if (QWidget* label = form_->labelForField(timeout_)) {
        static_cast<QLabel*>(label)->setText(cond ? QStringLiteral("最多等条件成立") : QStringLiteral("超时"));
    }
    setRowVisible(timeout_, locatorStep || cond);
    // expect / retry 只属于叶子步骤；块语句把它们放在块内的步骤上
    setRowVisible(expectOn_, !waits && !block);
    setRowVisible(expectBox_, !waits && !block && expectOn_->isChecked());
    setRowVisible(retryTimes_, !block);
    setRowVisible(retryInterval_, !block);
    text_->setEnabled(!prompt_->isChecked());
}

void StepPanel::showStatus(const QString& text, bool error) {
    status_->setStyleSheet(error ? QStringLiteral("color:#c0392b;") : QString());
    status_->setText(text);
}

bool StepPanel::collect(wa::Step& out) {
    using wa::StepType;
    out = step_;
    out.id = stdStr(id_->text().trimmed());
    out.window = stdStr(window_->currentData().toString());

    const StepType t = step_.type;
    if (t == StepType::Click || t == StepType::SetValue || t == StepType::WaitForElement ||
        t == StepType::AssertElement) {
        const QString text = locator_->toPlainText().trimmed();
        if (text.isEmpty()) {
            out.locator.reset();
        } else {
            auto r = wa::readLocatorJson(stdStr(text));
            if (!r.ok()) {
                showStatus(QStringLiteral("定位器 JSON 有误：") + qs(r.detail), true);
                return false;
            }
            out.locator = r.value;
        }
    }
    out.clickMode = clickMode_->currentData().toString() == QLatin1String("point") ? wa::ClickMode::Point
                                                                                  : wa::ClickMode::Auto;
    out.textFromPrompt = prompt_->isChecked();
    out.sensitive = sensitive_->isChecked();
    if (t == StepType::InputText || t == StepType::SetValue) {
        if (out.textFromPrompt) out.text.reset();
        else out.text = stdStr(text_->text());
    }
    out.method = method_->currentData().toString() == QLatin1String("clipboard") ? wa::InputMethod::Clipboard
                                                                                 : wa::InputMethod::Unicode;
    if (t == StepType::Hotkey) out.keys = splitKeys(keys_->text());
    if (t == StepType::Wait) out.durationMs = duration_->value();
    if (timeout_->value() > 0) out.timeoutMs = timeout_->value();
    else out.timeoutMs.reset();

    if (t == StepType::If || t == StepType::While) {
        const QString text = cond_->toPlainText().trimmed();
        wa::Expect c = step_.condition.value_or(wa::Expect());
        c.type = stdStr(condType_->currentData().toString());
        if (text.isEmpty()) {
            out.condition.reset();  // 校验器会报"缺少条件"
        } else {
            auto r = wa::readLocatorJson(stdStr(text));
            if (!r.ok()) {
                showStatus(QStringLiteral("条件定位器 JSON 有误：") + qs(r.detail), true);
                return false;
            }
            c.locator = r.value;
            out.condition = c;
        }
    }
    if (t == StepType::Repeat) out.times = times_->value();
    if (t == StepType::While) out.maxIterations = maxIter_->value();
    if (t == StepType::Repeat || t == StepType::While) out.intervalMs = interval_->value();

    if (isBlock(t)) {
        out.expect.reset();
        out.retry.reset();
    } else if (expectOn_->isChecked() && t != StepType::Wait) {
        auto r = wa::readLocatorJson(stdStr(expect_->toPlainText().trimmed()));
        if (!r.ok()) {
            showStatus(QStringLiteral("expect 定位器 JSON 有误：") + qs(r.detail), true);
            return false;
        }
        wa::Expect e = step_.expect.value_or(wa::Expect());
        e.locator = r.value;
        out.expect = e;
    } else {
        out.expect.reset();
    }

    if (!isBlock(t) && retryTimes_->value() > 0) {
        wa::RetryPolicy rp;
        rp.times = retryTimes_->value();
        rp.intervalMs = retryInterval_->value();
        out.retry = rp;
    } else {
        out.retry.reset();
    }
    const QString f = onFailure_->currentData().toString();
    if (f == QLatin1String("stop")) out.onFailure = wa::FailurePolicy::Stop;
    else if (f == QLatin1String("continue")) out.onFailure = wa::FailurePolicy::Continue;
    else out.onFailure.reset();
    return true;
}

void StepPanel::onFormChanged() {
    if (loading_) return;
    wa::Step s;
    if (!collect(s)) return;  // 状态栏已显示原因，不提交半成品
    showStatus(QString(), false);
    updateVisibility();
    emit stepEdited(s);
}

void StepPanel::setLocatorText(const QString& json, int target) {
    if (target == TargetExpect) {
        expectOn_->setChecked(true);
        expect_->setPlainText(json);
    } else if (target == TargetCondition) {
        cond_->setPlainText(json);
    } else {
        locator_->setPlainText(json);
    }
}
