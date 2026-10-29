#include "MainWindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QBrush>
#include <QColor>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include "ElementPickerDialog.h"
#include "RunController.h"
#include "StepPanel.h"
#include "TemplateCaptureDialog.h"
#include "Util.h"
#include "WindowsDialog.h"
#include "core/ExecutionState.h"
#include "core/Json.h"
#include "core/StepTree.h"
#include "core/Summary.h"

namespace {
const wa::StepType kStepTypes[] = {wa::StepType::ActivateWindow, wa::StepType::Click,
                                   wa::StepType::SetValue,       wa::StepType::InputText,
                                   wa::StepType::Hotkey,         wa::StepType::Wait,
                                   wa::StepType::WaitForElement, wa::StepType::AssertElement,
                                   wa::StepType::Script,         wa::StepType::LaunchProgram,
                                   wa::StepType::If,             wa::StepType::Repeat,
                                   wa::StepType::While};

QString typeLabel(wa::StepType t) {
    switch (t) {
        case wa::StepType::ActivateWindow: return QStringLiteral("激活窗口");
        case wa::StepType::Click: return QStringLiteral("点击");
        case wa::StepType::SetValue: return QStringLiteral("设置值");
        case wa::StepType::InputText: return QStringLiteral("输入文本");
        case wa::StepType::Hotkey: return QStringLiteral("组合键");
        case wa::StepType::Wait: return QStringLiteral("等待");
        case wa::StepType::WaitForElement: return QStringLiteral("等待控件");
        case wa::StepType::AssertElement: return QStringLiteral("断言控件");
        case wa::StepType::Script: return QStringLiteral("脚本");
        case wa::StepType::LaunchProgram: return QStringLiteral("启动程序");
        case wa::StepType::If: return QStringLiteral("如果");
        case wa::StepType::Repeat: return QStringLiteral("重复");
        case wa::StepType::While: return QStringLiteral("当…时循环");
        default: return QStringLiteral("未知类型");
    }
}
}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    run_ = new RunController(this);
    buildUi();
    buildMenus();

    connect(run_, &RunController::log, this, &MainWindow::appendLog);
    connect(run_, &RunController::stepStarted, this, &MainWindow::onStepStarted);
    connect(run_, &RunController::stepFinished, this, &MainWindow::onStepFinished);
    connect(run_, &RunController::waiting, this, &MainWindow::onWaiting);
    connect(run_, &RunController::finished, this, &MainWindow::onRunFinished);

    refreshAll(-1);
    resize(1100, 700);
}

void MainWindow::buildUi() {
    auto* split = new QSplitter(Qt::Horizontal, this);

    list_ = new QListWidget(split);
    list_->setMinimumWidth(280);
    split->addWidget(list_);

    panel_ = new StepPanel();
    auto* scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setWidget(panel_);
    split->addWidget(scroll);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    setCentralWidget(split);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    auto* dock = new QDockWidget(QStringLiteral("运行日志"), this);
    dock->setWidget(log_);
    addDockWidget(Qt::BottomDockWidgetArea, dock);

    statusLabel_ = new QLabel(this);
    statusBar()->addWidget(statusLabel_, 1);

    connect(list_, &QListWidget::currentRowChanged, this, [this] { onStepSelected(); });
    connect(panel_, &StepPanel::stepEdited, this, &MainWindow::onStepEdited);
    connect(panel_, &StepPanel::pickRequested, this, &MainWindow::onPickRequested);
    connect(panel_, &StepPanel::captureRequested, this, &MainWindow::onCaptureRequested);
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    actNew_ = file->addAction(QStringLiteral("新建"), this, &MainWindow::newFlow, QKeySequence::New);
    actOpen_ = file->addAction(QStringLiteral("打开…"), this, &MainWindow::openDialog, QKeySequence::Open);
    actSave_ = file->addAction(QStringLiteral("保存"), this, &MainWindow::save, QKeySequence::Save);
    actSaveAs_ = file->addAction(QStringLiteral("另存为…"), this, &MainWindow::saveAs, QKeySequence::SaveAs);
    file->addSeparator();
    file->addAction(QStringLiteral("退出"), this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("编辑(&E)"));
    actUndo_ = edit->addAction(QStringLiteral("撤销"), this, &MainWindow::undo, QKeySequence::Undo);
    actRedo_ = edit->addAction(QStringLiteral("重做"), this, &MainWindow::redo, QKeySequence::Redo);
    edit->addSeparator();
    auto* addMenu = edit->addMenu(QStringLiteral("添加步骤"));
    for (wa::StepType t : kStepTypes) {
        const int type = static_cast<int>(t);
        addMenu->addAction(typeLabel(t), this, [this, type] { addStep(type); });
    }
    // 往 if / repeat / while 里面添加：添加到所选块语句的末尾
    addIntoMenu_ = edit->addMenu(QStringLiteral("添加到所选块内（如果/重复/循环体）"));
    addElseMenu_ = edit->addMenu(QStringLiteral("添加到所选 if 的\"否则\"分支"));
    for (wa::StepType t : kStepTypes) {
        if (t == wa::StepType::LaunchProgram) continue;
        const int type = static_cast<int>(t);
        addIntoMenu_->addAction(typeLabel(t), this, [this, type] { addStepIntoBlock(type, false); });
        addElseMenu_->addAction(typeLabel(t), this, [this, type] { addStepIntoBlock(type, true); });
    }
    actDup_ = edit->addAction(QStringLiteral("复制步骤"), this, &MainWindow::duplicateStep, QKeySequence(Qt::CTRL | Qt::Key_D));
    actRemove_ = edit->addAction(QStringLiteral("删除步骤"), this, &MainWindow::removeStep, QKeySequence::Delete);
    actUp_ = edit->addAction(QStringLiteral("上移"), this, [this] { moveStep(-1); }, QKeySequence(Qt::ALT | Qt::Key_Up));
    actDown_ = edit->addAction(QStringLiteral("下移"), this, [this] { moveStep(1); }, QKeySequence(Qt::ALT | Qt::Key_Down));

    edit->addSeparator();
    actIndent_ = edit->addAction(QStringLiteral("缩进（移入上一个块）"), this, [this] { indentStep(false); },
                                 QKeySequence(Qt::ALT | Qt::Key_Right));
    actIndentElse_ = edit->addAction(QStringLiteral("缩进到上一个 if 的\"否则\""), this,
                                     [this] { indentStep(true); });
    actOutdent_ = edit->addAction(QStringLiteral("取消缩进（移出所在块）"), this, [this] { outdentStep(); },
                                  QKeySequence(Qt::ALT | Qt::Key_Left));

    auto* flow = menuBar()->addMenu(QStringLiteral("流程(&W)"));
    flow->addAction(QStringLiteral("目标窗口…"), this, &MainWindow::editWindows);
    flow->addAction(QStringLiteral("流程属性…"), this, &MainWindow::editSettings);

    auto* run = menuBar()->addMenu(QStringLiteral("运行(&R)"));
    actRunAll_ = run->addAction(QStringLiteral("运行全部"), this, &MainWindow::runAll, QKeySequence(Qt::Key_F5));
    actRunSel_ = run->addAction(QStringLiteral("只运行选中步骤"), this, &MainWindow::runSelected,
                                QKeySequence(Qt::CTRL | Qt::Key_F5));
    actDebug_ = run->addAction(QStringLiteral("单步调试（从头）"), this, &MainWindow::debugFromStart,
                               QKeySequence(Qt::Key_F11));
    run->addSeparator();
    actShots_ = run->addAction(QStringLiteral("打开失败截图文件夹"), this, &MainWindow::openScreenshotDir);
    run->addSeparator();
    actStep_ = run->addAction(QStringLiteral("执行当前步"), this, &MainWindow::stepNext, QKeySequence(Qt::Key_F10));
    actSkip_ = run->addAction(QStringLiteral("跳过当前步"), this, &MainWindow::skipStep, QKeySequence(Qt::Key_F9));
    actResume_ = run->addAction(QStringLiteral("继续运行"), this, &MainWindow::resumeRun, QKeySequence(Qt::Key_F8));
    actPause_ = run->addAction(QStringLiteral("暂停"), this, &MainWindow::pauseRun, QKeySequence(Qt::Key_F6));
    actStop_ = run->addAction(QStringLiteral("停止"), this, &MainWindow::stopRun,
                              QKeySequence(Qt::SHIFT | Qt::Key_F5));

    auto* bar = addToolBar(QStringLiteral("运行"));
    bar->addAction(actRunAll_);
    bar->addAction(actRunSel_);
    bar->addAction(actDebug_);
    bar->addSeparator();
    bar->addAction(actStep_);
    bar->addAction(actSkip_);
    bar->addAction(actResume_);
    bar->addAction(actPause_);
    bar->addAction(actStop_);
}

// ------------------------------------------------------------------ 显示

std::vector<std::string> MainWindow::windowAliases() const {
    std::vector<std::string> out;
    for (const auto& kv : editor_.workflow().windows) out.push_back(kv.first);
    return out;
}

QString MainWindow::stepText(size_t row) const {
    const auto rows = editor_.rows();
    const wa::StepRow& r = rows[row];
    const wa::Step& s = *r.step;
    QString text = QStringLiteral("%1  %2").arg(qs(s.id), typeLabel(s.type));
    if (!s.window.empty()) text += QStringLiteral(" · ") + qs(s.window);
    if (wa::isBlockType(s.type)) text += QStringLiteral("  ▾");
    // 缩进显示层级；else 分支里的步骤带"否则"标记
    QString indent(r.depth * 4, QLatin1Char(' '));
    if (r.inElse) indent += QStringLiteral("否则 ▸ ");
    else if (r.depth > 0) indent += QStringLiteral("└ ");
    text = indent + text;
    const auto it = runMarks_.find(s.id);
    if (it != runMarks_.end()) text = it->second + QLatin1Char(' ') + text;
    return text;
}

int MainWindow::currentRow() const { return list_->currentRow(); }

void MainWindow::refreshList(int selectRow) {
    list_->blockSignals(true);
    list_->clear();
    const size_t count = editor_.rowCount();
    for (size_t i = 0; i < count; ++i) {
        auto* item = new QListWidgetItem(stepText(i));
        const auto issues = editor_.issuesForStep(i);
        bool hasError = false;
        for (const auto& is : issues) hasError = hasError || is.severity == wa::Severity::Error;
        if (hasError) {
            item->setForeground(QBrush(QColor(192, 57, 43)));
            item->setToolTip(qs(issues.front().message));
        } else if (!issues.empty()) {
            item->setForeground(QBrush(QColor(180, 120, 0)));
            item->setToolTip(qs(issues.front().message));
        }
        if (waiting_ && static_cast<int>(i) == waitingIndex_) item->setBackground(QBrush(QColor(255, 244, 179)));
        list_->addItem(item);
    }
    if (selectRow >= static_cast<int>(count)) selectRow = static_cast<int>(count) - 1;
    if (selectRow >= 0) list_->setCurrentRow(selectRow);
    list_->blockSignals(false);
}

void MainWindow::refreshAll(int selectRow) {
    if (selectRow == -2) selectRow = currentRow();
    refreshList(selectRow);
    onStepSelected();
    refreshTitle();
    updateActions();

    refreshStatus();
}

void MainWindow::refreshStatus() {
    const auto& rep = editor_.validation();
    statusLabel_->setText(QStringLiteral("%1 个步骤 · %2 个错误 · %3 个警告")
                              .arg(editor_.rowCount())
                              .arg(rep.errorCount())
                              .arg(rep.warningCount()));
}

void MainWindow::refreshTitle() {
    const QString name = filePath_.isEmpty() ? QStringLiteral("未命名") : filePath_;
    setWindowTitle(QStringLiteral("WinAuto - %1%2").arg(name, editor_.dirty() ? QStringLiteral(" *") : QString()));
}

void MainWindow::updateActions() {
    const bool running = run_->running();
    const int row = currentRow();
    const bool hasRow = row >= 0;
    const int count = static_cast<int>(editor_.rowCount());
    const wa::Step* cur = hasRow ? editor_.stepAtRow(static_cast<size_t>(row)) : nullptr;
    const bool curBlock = cur && wa::isBlockType(cur->type);

    actNew_->setEnabled(!running);
    actOpen_->setEnabled(!running);
    actUndo_->setEnabled(!running && editor_.canUndo());
    actRedo_->setEnabled(!running && editor_.canRedo());
    actRemove_->setEnabled(!running && hasRow);
    actDup_->setEnabled(!running && hasRow);
    actUp_->setEnabled(!running && hasRow);
    actDown_->setEnabled(!running && hasRow);
    actIndent_->setEnabled(!running && hasRow);
    actIndentElse_->setEnabled(!running && hasRow);
    actOutdent_->setEnabled(!running && hasRow);
    addIntoMenu_->setEnabled(!running && curBlock);
    addElseMenu_->setEnabled(!running && cur && cur->type == wa::StepType::If);
    actShots_->setEnabled(!lastScreenshotDir_.empty());
    panel_->setEnabled(!running && hasRow);

    actRunAll_->setEnabled(!running && count > 0);
    actRunSel_->setEnabled(!running && hasRow);
    actDebug_->setEnabled(!running && count > 0);
    actStep_->setEnabled(running && waiting_);
    actSkip_->setEnabled(running && waiting_);
    actResume_->setEnabled(running && waiting_);
    actPause_->setEnabled(running && !waiting_);
    actStop_->setEnabled(running);
}

void MainWindow::appendLog(const QString& line) {
    log_->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + line);
}

// ------------------------------------------------------------------ 文件

bool MainWindow::maybeSave() {
    if (!editor_.dirty()) return true;
    const auto answer = QMessageBox::question(this, QStringLiteral("未保存的修改"),
                                              QStringLiteral("流程已修改，是否保存？"),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return save();
    return true;
}

void MainWindow::newFlow() {
    if (!maybeSave()) return;
    editor_ = wa::WorkflowEditor();
    filePath_.clear();
    runMarks_.clear();
    refreshAll(-1);
}

void MainWindow::openDialog() {
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开流程"), QString(),
                                                      QStringLiteral("流程文件 (*.json);;所有文件 (*)"));
    if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, QStringLiteral("打开失败"), f.errorString());
        return false;
    }
    QByteArray bytes = f.readAll();
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);  // 去掉 UTF-8 BOM
    auto wf = wa::readWorkflow(std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
    if (!wf.ok()) {
        QMessageBox::warning(this, QStringLiteral("流程文件无效"), qs(wf.detail));
        return false;
    }
    editor_.load(std::move(wf.value));
    filePath_ = path;
    runMarks_.clear();
    refreshAll(editor_.workflow().steps.empty() ? -1 : 0);
    return true;
}

bool MainWindow::writeTo(const QString& path) {
    const std::string text = wa::writeWorkflow(editor_.workflow());
    QSaveFile f(path);  // 先写临时文件，成功后原子替换，避免写一半损坏原文件
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), f.errorString());
        return false;
    }
    f.write(text.data(), static_cast<qint64>(text.size()));
    f.write("\n", 1);
    if (!f.commit()) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), f.errorString());
        return false;
    }
    filePath_ = path;
    editor_.markSaved();
    refreshTitle();
    return true;
}

bool MainWindow::save() {
    if (filePath_.isEmpty()) return saveAs();
    return writeTo(filePath_);
}

bool MainWindow::saveAs() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存流程"), filePath_,
                                                      QStringLiteral("流程文件 (*.json)"));
    if (path.isEmpty()) return false;
    return writeTo(path);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (run_->running()) {
        QMessageBox::information(this, QStringLiteral("正在运行"), QStringLiteral("请先停止运行再关闭。"));
        event->ignore();
        return;
    }
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    event->accept();
}

// ------------------------------------------------------------------ 编辑

void MainWindow::selectStepId(const std::string& id) {
    const auto rows = editor_.rows();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].step->id == id) {
            refreshAll(static_cast<int>(i));
            return;
        }
    }
    refreshAll(-2);
}

void MainWindow::addStep(int type) {
    const int row = currentRow();
    const size_t at = row < 0 ? editor_.addStep(editor_.rowCount(), static_cast<wa::StepType>(type))
                              : editor_.addStepAfter(static_cast<size_t>(row), static_cast<wa::StepType>(type));
    refreshAll(static_cast<int>(at));
}

void MainWindow::addStepIntoBlock(int type, bool elseBranch) {
    const int row = currentRow();
    if (row < 0) return;
    auto r = editor_.addStepInto(static_cast<size_t>(row), static_cast<wa::StepType>(type), elseBranch);
    if (!r.ok()) {
        QMessageBox::information(this, QStringLiteral("无法添加"), qs(r.detail));
        return;
    }
    refreshAll(static_cast<int>(r.value));
}

void MainWindow::removeStep() {
    const int row = currentRow();
    if (row < 0) return;
    const wa::Step* s = editor_.stepAtRow(static_cast<size_t>(row));
    if (s && (!s->body.empty() || !s->elseBody.empty())) {
        const auto answer = QMessageBox::question(this, QStringLiteral("删除块语句"),
                                                  QStringLiteral("它里面的 %1 个步骤会一起删除（可撤销）。继续？")
                                                      .arg(wa::countSteps(s->body) + wa::countSteps(s->elseBody)));
        if (answer != QMessageBox::Yes) return;
    }
    editor_.removeStep(static_cast<size_t>(row));
    refreshAll(row);
}

void MainWindow::duplicateStep() {
    const int row = currentRow();
    if (row < 0) return;
    auto r = editor_.duplicateStep(static_cast<size_t>(row));
    if (r.ok()) refreshAll(static_cast<int>(r.value));
}

void MainWindow::moveStep(int delta) {
    const int row = currentRow();
    if (row < 0) return;
    const std::string id = editor_.stepAtRow(static_cast<size_t>(row))->id;
    const wa::Status st = editor_.moveStep(static_cast<size_t>(row), delta);
    if (!st.ok()) {
        statusBar()->showMessage(qs(st.detail), 3000);
        return;
    }
    selectStepId(id);
}

void MainWindow::indentStep(bool intoElse) {
    const int row = currentRow();
    if (row < 0) return;
    auto r = editor_.indentStep(static_cast<size_t>(row), intoElse);
    if (!r.ok()) {
        statusBar()->showMessage(qs(r.detail), 4000);
        return;
    }
    refreshAll(static_cast<int>(r.value));
}

void MainWindow::outdentStep() {
    const int row = currentRow();
    if (row < 0) return;
    auto r = editor_.outdentStep(static_cast<size_t>(row));
    if (!r.ok()) {
        statusBar()->showMessage(qs(r.detail), 4000);
        return;
    }
    refreshAll(static_cast<int>(r.value));
}

void MainWindow::editWindows() {
    WindowsDialog dlg(editor_, this);
    dlg.exec();
    refreshAll();
}

void MainWindow::editSettings() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("流程属性"));
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(qs(editor_.workflow().name), &dlg);
    auto* timeout = new QSpinBox(&dlg);
    timeout->setRange(100, 600000);
    timeout->setSuffix(QStringLiteral(" ms"));
    timeout->setValue(editor_.workflow().execution.stepTimeoutMs);
    auto* policy = new QComboBox(&dlg);
    policy->addItem(QStringLiteral("失败即停止"));
    policy->addItem(QStringLiteral("忽略失败继续"));
    policy->setCurrentIndex(editor_.workflow().execution.onFailure == wa::FailurePolicy::Stop ? 0 : 1);
    auto* shots = new QCheckBox(QStringLiteral("失败时自动截取目标窗口画面"), &dlg);
    shots->setChecked(editor_.workflow().execution.screenshotOnFailure);
    shots->setToolTip(QStringLiteral("保存到流程文件旁的 screenshots 文件夹；执行过敏感/运行时询问的步骤后不再截图"));
    auto* budget = new QSpinBox(&dlg);
    budget->setRange(1, wa::kMaxStepExecutionsLimit);
    budget->setValue(editor_.workflow().execution.maxStepExecutions);
    budget->setToolTip(QStringLiteral("整个运行最多执行多少次步骤（含循环里的每一次），超过即停止"));
    form->addRow(QStringLiteral("名称"), name);
    form->addRow(QStringLiteral("步骤默认超时"), timeout);
    form->addRow(QStringLiteral("默认失败策略"), policy);
    form->addRow(QString(), shots);
    form->addRow(QStringLiteral("步骤执行次数上限"), budget);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;

    editor_.setName(stdStr(name->text()));
    wa::ExecutionOptions ex = editor_.workflow().execution;
    ex.stepTimeoutMs = timeout->value();
    ex.onFailure = policy->currentIndex() == 0 ? wa::FailurePolicy::Stop : wa::FailurePolicy::Continue;
    ex.screenshotOnFailure = shots->isChecked();
    ex.maxStepExecutions = budget->value();
    editor_.setExecution(ex);
    refreshAll();
}

void MainWindow::undo() {
    editor_.undo();
    refreshAll();
}

void MainWindow::redo() {
    editor_.redo();
    refreshAll();
}

void MainWindow::onStepSelected() {
    const int row = currentRow();
    if (row < 0 || row >= static_cast<int>(editor_.rowCount())) {
        panel_->clearStep();
    } else {
        panel_->setStep(*editor_.stepAtRow(static_cast<size_t>(row)), windowAliases(),
                        editor_.issuesForStep(static_cast<size_t>(row)));
    }
    updateActions();
}

void MainWindow::onStepEdited(const wa::Step& step) {
    const int row = currentRow();
    if (row < 0) return;
    wa::Status st = editor_.updateStep(static_cast<size_t>(row), step);
    if (!st.ok()) {
        QMessageBox::warning(this, QStringLiteral("无法修改"), qs(st.detail));
        refreshAll(row);  // 把面板恢复成模型里的真实内容
        return;
    }
    // 轻量刷新：不重载面板，否则用户正在输入的文本框会被重置、光标跳走
    refreshList(row);
    refreshTitle();
    updateActions();
    refreshStatus();
    panel_->setIssues(editor_.issuesForStep(static_cast<size_t>(row)));
}

void MainWindow::onPickRequested(int target) {
    const int row = currentRow();
    if (row < 0) return;
    const wa::Step& s = *editor_.stepAtRow(static_cast<size_t>(row));
    auto it = editor_.workflow().windows.find(s.window);
    if (it == editor_.workflow().windows.end()) {
        QMessageBox::information(this, QStringLiteral("拾取控件"),
                                 QStringLiteral("请先给这一步选择目标窗口（流程 → 目标窗口…）。"));
        return;
    }
    ElementPickerDialog dlg(it->second, this);
    if (dlg.exec() != QDialog::Accepted) return;
    panel_->setLocatorText(dlg.locatorJson(), target);  // 文本变化会经面板的合并定时器提交
}

void MainWindow::onCaptureRequested(int target) {
    const int row = currentRow();
    if (row < 0) return;
    const wa::Step& s = *editor_.stepAtRow(static_cast<size_t>(row));
    auto it = editor_.workflow().windows.find(s.window);
    if (it == editor_.workflow().windows.end()) {
        QMessageBox::information(this, QStringLiteral("截取模板"),
                                 QStringLiteral("请先给这一步选择目标窗口（流程 → 目标窗口…）。"));
        return;
    }
    // 模板文件按相对路径存在流程文件旁边，所以流程必须先有保存位置
    if (filePath_.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("截取模板"),
            QStringLiteral("模板图片保存在流程文件旁边的 templates 文件夹，需要先保存流程。现在保存吗？"));
        if (answer != QMessageBox::Yes || !save()) return;
    }
    QImage image;
    QString error;
    if (!TemplateCaptureDialog::grabTarget(it->second, this, image, error)) {
        QMessageBox::warning(this, QStringLiteral("截取模板"), error);
        return;
    }
    TemplateCaptureDialog dlg(image, QFileInfo(filePath_).absolutePath(), qs(s.id), this);
    if (dlg.exec() != QDialog::Accepted) return;
    panel_->setLocatorText(dlg.locatorJson(), target);
}

// ------------------------------------------------------------------ 运行

bool MainWindow::confirmRun(const wa::Workflow& wf, bool& confirmedRisks) {
    const wa::ValidationReport report = wa::validateWorkflow(wf);
    if (!report.ok()) {
        QString text = QStringLiteral("流程有 %1 个错误，不能运行：\n\n").arg(report.errorCount());
        int shown = 0;
        for (const auto& i : report.issues) {
            if (i.severity != wa::Severity::Error) continue;
            text += QStringLiteral("• %1：%2\n").arg(qs(i.path), qs(i.message));
            if (++shown >= 8) break;
        }
        QMessageBox::warning(this, QStringLiteral("无法运行"), text);
        return false;
    }

    // 信任摘要：直接显示（不折叠），让用户在运行前看清"会操作哪些程序、输入什么、按什么键"
    QDialog box(this);
    box.setWindowTitle(QStringLiteral("运行前确认"));
    box.resize(640, 480);
    auto* layout = new QVBoxLayout(&box);
    auto* head = new QLabel(QStringLiteral("该流程将接管键盘和鼠标。运行期间随时按 Ctrl+Alt+F10 紧急停止。\n"
                                           "请确认下面的内容与你的预期一致："),
                            &box);
    head->setWordWrap(true);
    layout->addWidget(head);
    auto* summary = new QPlainTextEdit(&box);
    summary->setReadOnly(true);
    summary->setPlainText(qs(wa::summarizeWorkflow(wf, report)));
    layout->addWidget(summary, 1);
    auto* buttons = new QDialogButtonBox(&box);
    auto* run = buttons->addButton(QStringLiteral("运行"), QDialogButtonBox::AcceptRole);
    auto* cancel = buttons->addButton(QStringLiteral("取消"), QDialogButtonBox::RejectRole);
    cancel->setDefault(true);  // 默认是取消：回车不会误启动
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &box, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &box, &QDialog::reject);
    (void)run;
    if (box.exec() != QDialog::Accepted) return false;

    confirmedRisks = false;
    if (report.needsConfirmation()) {
        bool ok = false;
        const QString typed = QInputDialog::getText(
            this, QStringLiteral("高风险操作"),
            QStringLiteral("流程包含高风险操作（如 Alt+F4、Delete）。\n确认要运行请输入 RISK："), QLineEdit::Normal,
            QString(), &ok);
        if (!ok || typed.trimmed() != QLatin1String("RISK")) return false;
        confirmedRisks = true;
    }
    return true;
}

// 每次运行一个时间戳子目录：流程文件旁的 screenshots/；流程还没保存则放到系统临时目录
std::string MainWindow::startScreenshotDir() const {
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString base = filePath_.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/winauto")
                             : QFileInfo(filePath_).absolutePath();
    return stdStr(QDir::cleanPath(base + QStringLiteral("/screenshots/") + stamp));
}

void MainWindow::openScreenshotDir() {
    if (lastScreenshotDir_.empty()) return;
    const QString dir = qs(lastScreenshotDir_);
    if (!QFileInfo::exists(dir)) {
        QMessageBox::information(this, QStringLiteral("失败截图"), QStringLiteral("这次运行没有产生截图。"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

bool MainWindow::startRun(bool paused, bool selectedOnly) {
    if (run_->running()) return false;
    bool confirmed = false;
    if (!confirmRun(editor_.workflow(), confirmed)) return false;

    wa::RunOptions options;
    options.confirmedRisks = confirmed;
    if (selectedOnly) {
        const int row = currentRow();
        if (row < 0) return false;
        options.firstStep = static_cast<size_t>(row);
        options.lastStep = static_cast<size_t>(row);
    }
    if (editor_.workflow().execution.screenshotOnFailure) {
        options.screenshotDir = startScreenshotDir();
        lastScreenshotDir_ = options.screenshotDir;
    }
    runMarks_.clear();
    waiting_ = false;
    waitingIndex_ = -1;
    log_->clear();
    appendLog(paused ? QStringLiteral("开始单步调试") : QStringLiteral("开始运行"));
    const std::string baseDir =
        filePath_.isEmpty() ? std::string() : stdStr(QFileInfo(filePath_).absolutePath());
    if (!run_->start(editor_.workflow(), options, paused, baseDir)) return false;
    refreshAll();
    return true;
}

void MainWindow::runAll() { startRun(false, false); }
void MainWindow::runSelected() { startRun(false, true); }
void MainWindow::debugFromStart() { startRun(true, false); }

void MainWindow::stepNext() { run_->step(); }
void MainWindow::skipStep() { run_->skip(); }
void MainWindow::pauseRun() {
    run_->pause();
    appendLog(QStringLiteral("将在下一步之前暂停"));
}
void MainWindow::resumeRun() { run_->resume(); }
void MainWindow::stopRun() { run_->stop(); }

void MainWindow::onStepStarted(const QString& stepId) {
    runMarks_[stdStr(stepId)] = QStringLiteral("▶");
    refreshList(currentRow());
}

void MainWindow::onStepFinished(const QString& stepId, int code, const QString& detail, int durationMs,
                                bool skipped) {
    QString mark;
    if (skipped) {
        mark = QStringLiteral("↷");
        appendLog(QStringLiteral("↷ %1 已跳过").arg(stepId));
    } else if (code == static_cast<int>(wa::ErrCode::Ok)) {
        mark = QStringLiteral("✔");
        appendLog(QStringLiteral("✔ %1 (%2 ms)").arg(stepId).arg(durationMs));
    } else {
        mark = QStringLiteral("✖");
        appendLog(QStringLiteral("✖ %1 失败 %2：%3")
                      .arg(stepId, qs(wa::errName(static_cast<wa::ErrCode>(code))), detail));
    }
    runMarks_[stdStr(stepId)] = mark;
    refreshList(currentRow());
}

void MainWindow::onWaiting(int stepIndex, bool waiting) {
    waiting_ = waiting;
    waitingIndex_ = waiting ? stepIndex : -1;
    if (waiting) {
        appendLog(QStringLiteral("停在步骤 %1 之前：F10 执行 / F9 跳过 / F8 继续").arg(stepIndex + 1));
        if (stepIndex >= 0 && stepIndex < list_->count()) list_->setCurrentRow(stepIndex);
    }
    refreshList(currentRow());
    updateActions();
}

void MainWindow::onRunFinished(int state, int code, const QString& failedStepId, const QString& detail) {
    waiting_ = false;
    waitingIndex_ = -1;
    QString text = QStringLiteral("结束：%1").arg(qs(wa::toString(static_cast<wa::ExecutionState>(state))));
    if (code != static_cast<int>(wa::ErrCode::Ok)) {
        text += QStringLiteral("（%1").arg(qs(wa::errName(static_cast<wa::ErrCode>(code))));
        if (!failedStepId.isEmpty()) text += QStringLiteral("，步骤 ") + failedStepId;
        text += QStringLiteral("）");
    }
    if (!detail.isEmpty()) text += QStringLiteral("：") + detail;
    appendLog(text);
    statusLabel_->setText(text);
    refreshList(currentRow());
    updateActions();
}
