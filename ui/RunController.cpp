#include "RunController.h"

#include <QApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QThread>

#include "Util.h"
#include "core/Clock.h"
#include "core/StopHotkey.h"
#include "core/VirtualKey.h"

#ifdef WINAUTO_HAVE_PLATFORM
#include "UiaSession.h"
#include "Win32Clipboard.h"
#include "Win32ImageLocator.h"
#include "Win32InputController.h"
#include "Win32Screenshot.h"
#include "ScriptWorker.h"
#include "Win32StopHotkey.h"
#include "Win32UiaLocator.h"
#include "Win32WindowProvider.h"
#include "Win32ProcessLauncher.h"
#endif

RunWorker::RunWorker(wa::Workflow wf, wa::RunOptions options, bool startPaused, RunController* promptOwner,
                     std::string baseDir)
    : wf_(std::move(wf)),
      options_(options),
      startPaused_(startPaused),
      promptOwner_(promptOwner),
      baseDir_(std::move(baseDir)),
      gate_(token_) {}

void RunWorker::stop() {
    gate_.stop();
    token_.cancel();
}

void RunWorker::run() {
#ifndef WINAUTO_HAVE_PLATFORM
    emit finished(static_cast<int>(wa::ExecutionState::Failed), static_cast<int>(wa::ErrCode::Unsupported),
                  QString(), QStringLiteral("运行流程仅在 Windows 上可用"));
    return;
#else
    // 紧急停止热键：没有它就不运行
    auto spec = wa::parseHotkey(wa::kDefaultStopHotkey);
    wa::platform::Win32StopHotkey stopHotkey;
    wa::StopHotkeyGuard guard(stopHotkey, token_);
    wa::Status armed = guard.arm(spec.value);
    if (!armed.ok()) {
        emit finished(static_cast<int>(wa::ExecutionState::Failed), static_cast<int>(armed.code), QString(),
                      QStringLiteral("无法注册紧急停止热键 %1（%2），为安全起见拒绝运行。")
                          .arg(qs(spec.value.text), qs(armed.detail)));
        return;
    }
    emit log(QStringLiteral("紧急停止：随时按 %1").arg(qs(spec.value.text)));

    // COM 对象必须在使用它们的线程里创建
    wa::platform::Win32WindowProvider windows;
    wa::platform::UiaSession uia;
    wa::platform::Win32ImageLocator images(baseDir_);
    wa::platform::Win32UiaLocator locator(uia, &images);
    wa::platform::Win32InputController input(&uia);
    wa::platform::Win32Clipboard clipboard;  // 剪贴板输入；和 COM 对象一样在工作线程里创建
    wa::platform::Win32Screenshot screenshot;
    wa::platform::ScriptWorker scripts;
    wa::platform::Win32ProcessLauncher processes;
    wa::SteadyClock clock;

    gate_.startPaused(startPaused_);
    gate_.onWaiting = [this](size_t index, bool w) { emit waiting(static_cast<int>(index), w); };

    wa::EngineDeps deps;
    deps.windows = &windows;
    deps.locator = &locator;
    deps.input = &input;
    deps.clock = &clock;
    deps.clipboard = &clipboard;
    deps.screenshot = &screenshot;
    deps.scripts = &scripts;
    deps.processes = &processes;
    deps.beforeStep = [this](const wa::Step& s, size_t i) { return gate_.gate(s, i); };
    deps.prompt = [this](const wa::Step& s) -> wa::Result<std::string> {
        QString text;
        bool ok = false;
        const QString prompt = QStringLiteral("请输入步骤 %1 的内容：").arg(qs(s.id));
        // 用仿函数形式：参数不经过元对象系统拷贝，不需要注册指针类型
        QMetaObject::invokeMethod(
            promptOwner_, [&] { promptOwner_->askSecret(prompt, text, ok); }, Qt::BlockingQueuedConnection);
        if (!ok) return wa::Result<std::string>::failure(wa::ErrCode::Cancelled, "input cancelled by user");
        return wa::Result<std::string>::success(stdStr(text));
    };
    deps.observer.onLog = [this](const wa::LogEntry& e) {
        emit log(qs(e.stepId.empty() ? e.message : e.stepId + ": " + e.message));
    };
    deps.observer.onStepStarted = [this](const std::string& id) { emit stepStarted(qs(id)); };
    deps.observer.onStepFinished = [this](const wa::StepRecord& r) {
        emit stepFinished(qs(r.stepId), static_cast<int>(r.code), qs(r.detail), static_cast<int>(r.durationMs),
                          r.skipped);
    };

    wa::WorkflowEngine engine(deps);
    const wa::RunReport report = engine.run(wf_, token_, options_);
    guard.disarm();
    emit finished(static_cast<int>(report.state), static_cast<int>(report.code), qs(report.failedStepId),
                  qs(report.detail));
#endif
}

RunController::RunController(QObject* parent) : QObject(parent) {}

RunController::~RunController() {
    if (worker_) worker_->stop();
    if (thread_) {
        thread_->quit();
        thread_->wait(5000);
    }
}

bool RunController::start(const wa::Workflow& wf, const wa::RunOptions& options, bool startPaused,
                          const std::string& baseDir) {
    if (worker_) return false;
    thread_ = new QThread(this);
    worker_ = new RunWorker(wf, options, startPaused, this, baseDir);
    worker_->moveToThread(thread_);
    connect(thread_, &QThread::started, worker_, &RunWorker::run);
    connect(worker_, &RunWorker::log, this, &RunController::log);
    connect(worker_, &RunWorker::stepStarted, this, &RunController::stepStarted);
    connect(worker_, &RunWorker::stepFinished, this, &RunController::stepFinished);
    connect(worker_, &RunWorker::waiting, this, &RunController::waiting);
    connect(worker_, &RunWorker::finished, this, &RunController::onFinished);
    thread_->start();
    return true;
}

void RunController::onFinished(int state, int code, const QString& failedStepId, const QString& detail) {
    // 先收尾线程再通知界面，这样界面收到 finished 时 running() 已为 false
    thread_->quit();
    thread_->wait(5000);
    delete worker_;
    worker_ = nullptr;
    delete thread_;
    thread_ = nullptr;
    emit finished(state, code, failedStepId, detail);
}

void RunController::pause() {
    if (worker_) worker_->pause();
}
void RunController::resume() {
    if (worker_) worker_->resume();
}
void RunController::step() {
    if (worker_) worker_->step();
}
void RunController::skip() {
    if (worker_) worker_->skip();
}
void RunController::stop() {
    if (worker_) worker_->stop();
}

void RunController::askSecret(const QString& prompt, QString& out, bool& ok) {
    bool accepted = false;
    out = QInputDialog::getText(QApplication::activeWindow(), QStringLiteral("需要输入"), prompt, QLineEdit::Password,
                                QString(), &accepted);
    ok = accepted;
}
