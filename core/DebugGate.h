#pragma once
// 调试闸门：单步、暂停、继续、跳过、停止。纯逻辑，线程安全，有单元测试。
//   - 引擎线程调用 gate()，在需要时阻塞等待命令；UI 线程调用 pause/resume/step/skip/stop。
//   - 等待期间监听取消令牌（全局停止热键同样有效），取消时返回 Stop。
//   - 命令只对"正在等待的步骤"生效；没有人在等待时发出的 step/skip 会被忽略（不会积压到以后）。
#include <condition_variable>
#include <functional>
#include <mutex>

#include "core/CancelToken.h"
#include "core/Engine.h"

namespace wa {

class DebugGate {
public:
    explicit DebugGate(const CancelToken& token) : token_(token) {}

    // 运行前设置：true 表示在第一步之前就暂停（单步调试入口）
    void startPaused(bool paused);

    // 通知 UI：开始/结束等待某一步。回调在引擎线程、不持锁时调用。
    std::function<void(size_t index, bool waiting)> onWaiting;

    void pause();   // 下一步之前暂停
    void resume();  // 等待中的步骤执行，之后不再暂停
    void step();    // 等待中的步骤执行，执行后在下一步前再次暂停
    void skip();    // 等待中的步骤跳过，之后在下一步前再次暂停
    void stop();    // 结束运行

    bool isWaiting() const;

    // 交给 EngineDeps::beforeStep
    StepGate gate(const Step& step, size_t index);

private:
    enum class Cmd { None, Resume, Step, Skip, Stop };

    const CancelToken& token_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool paused_ = false;
    bool waiting_ = false;
    Cmd cmd_ = Cmd::None;
};

}  // namespace wa
