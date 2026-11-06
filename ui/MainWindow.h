#pragma once
// 主窗口：步骤列表 + 属性面板 + 运行/调试 + 日志。
// 所有编辑规则都在 core 的 WorkflowEditor 里；本类只做界面转发和显示。
#include <QMainWindow>
#include <map>
#include <string>

#include "core/WorkflowEditor.h"

class QAction;
class QLabel;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class StepPanel;
class RunController;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    bool openFile(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void newFlow();
    void openDialog();
    bool save();
    bool saveAs();
    void addStep(int type);
    void addStepIntoBlock(int type, bool elseBranch);
    void indentStep(bool intoElse);
    void outdentStep();
    void openScreenshotDir();
    void removeStep();
    void duplicateStep();
    void moveStep(int delta);
    void editWindows();
    void editSettings();
    void generateWorkflow();
    void editModelSettings();
    void openAgentSession();
    void undo();
    void redo();
    void onStepSelected();
    void onStepEdited(const wa::Step& step);
    void onPickRequested(int target);
    void onCaptureRequested(int target);

    void runAll();
    void runSelected();
    void debugFromStart();
    void stepNext();
    void skipStep();
    void pauseRun();
    void resumeRun();
    void stopRun();

    void onStepStarted(const QString& stepId);
    void onStepFinished(const QString& stepId, int code, const QString& detail, int durationMs, bool skipped);
    void onWaiting(int stepIndex, bool waiting);
    void onRunFinished(int state, int code, const QString& failedStepId, const QString& detail);

private:
    void buildUi();
    void buildMenus();
    void refreshAll(int selectRow = -2);  // -2：保持当前选中
    void refreshList(int selectRow);
    void refreshTitle();
    void refreshStatus();
    void updateActions();
    int currentRow() const;
    bool maybeSave();
    bool writeTo(const QString& path);
    void appendLog(const QString& line);
    bool startRun(bool paused, bool selectedOnly);
    bool confirmRun(const wa::Workflow& wf, bool& confirmedRisks);
    std::vector<std::string> windowAliases() const;
    QString stepText(size_t row) const;
    void selectStepId(const std::string& id);   // 结构编辑后按步骤 id 找回选中行
    std::string startScreenshotDir() const;

    wa::WorkflowEditor editor_;
    QString filePath_;

    QListWidget* list_ = nullptr;
    StepPanel* panel_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    RunController* run_ = nullptr;
    bool waiting_ = false;          // 调试中，正停在某一步之前
    int waitingIndex_ = -1;
    std::map<std::string, QString> runMarks_;  // 步骤 id -> 本次运行的状态标记

    QAction *actNew_ = nullptr, *actOpen_ = nullptr, *actSave_ = nullptr, *actSaveAs_ = nullptr;
    QAction *actUndo_ = nullptr, *actRedo_ = nullptr;
    QAction* actGenerate_ = nullptr;
    QAction* actModelSettings_ = nullptr;
    QAction* actAgentSession_ = nullptr;
    QAction *actRemove_ = nullptr, *actDup_ = nullptr, *actUp_ = nullptr, *actDown_ = nullptr;
    QAction *actIndent_ = nullptr, *actIndentElse_ = nullptr, *actOutdent_ = nullptr, *actShots_ = nullptr;
    QMenu* addIntoMenu_ = nullptr;
    QMenu* addElseMenu_ = nullptr;
    std::string lastScreenshotDir_;  // 最近一次运行的失败截图目录
    QAction *actRunAll_ = nullptr, *actRunSel_ = nullptr, *actDebug_ = nullptr;
    QAction *actStep_ = nullptr, *actSkip_ = nullptr, *actPause_ = nullptr, *actResume_ = nullptr, *actStop_ = nullptr;
};
