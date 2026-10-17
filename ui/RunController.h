#pragma once
// 在后台线程执行流程。引擎同步执行，所以放进 QThread；UI 线程通过线程安全的 DebugGate / CancelToken 控制它。
// 安全：没有紧急停止热键就拒绝运行（与命令行 run 一致）。
#include <QObject>
#include <QString>
#include <memory>

#include "core/CancelToken.h"
#include "core/DebugGate.h"
#include "core/Engine.h"

class QThread;
class RunController;

class RunWorker : public QObject {
    Q_OBJECT
public:
    RunWorker(wa::Workflow wf, wa::RunOptions options, bool startPaused, RunController* promptOwner);

    // 以下方法可从任意线程调用
    void pause() { gate_.pause(); }
    void resume() { gate_.resume(); }
    void step() { gate_.step(); }
    void skip() { gate_.skip(); }
    void stop();

public slots:
    void run();  // 在工作线程中执行

signals:
    void log(const QString& line);
    void stepStarted(const QString& stepId);
    void stepFinished(const QString& stepId, int code, const QString& detail, int durationMs, bool skipped);
    void waiting(int stepIndex, bool waiting);
    void finished(int state, int code, const QString& failedStepId, const QString& detail);

private:
    wa::Workflow wf_;
    wa::RunOptions options_;
    bool startPaused_;
    RunController* promptOwner_;
    wa::CancelToken token_;
    wa::DebugGate gate_;
};

class RunController : public QObject {
    Q_OBJECT
public:
    explicit RunController(QObject* parent = nullptr);
    ~RunController() override;

    bool running() const { return worker_ != nullptr; }

    // 开始运行。startPaused=true 为单步调试。已在运行时返回 false。
    bool start(const wa::Workflow& wf, const wa::RunOptions& options, bool startPaused);
    void pause();
    void resume();
    void step();
    void skip();
    void stop();

    // 必须在 UI 线程调用（工作线程通过 BlockingQueuedConnection 转到这里）：询问敏感文本
    void askSecret(const QString& prompt, QString& out, bool& ok);

signals:
    void log(const QString& line);
    void stepStarted(const QString& stepId);
    void stepFinished(const QString& stepId, int code, const QString& detail, int durationMs, bool skipped);
    void waiting(int stepIndex, bool waiting);
    void finished(int state, int code, const QString& failedStepId, const QString& detail);

private slots:
    void onFinished(int state, int code, const QString& failedStepId, const QString& detail);

private:
    QThread* thread_ = nullptr;
    RunWorker* worker_ = nullptr;
};
