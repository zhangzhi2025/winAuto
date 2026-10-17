#include <vector>

#include "core/StateMachine.h"
#include "tests/TestFramework.h"

using namespace winauto;
using S = ExecutionState;

TEST(StateMachine, startsIdle) {
    ExecutionStateMachine m;
    CHECK(m.state() == S::Idle);
}

TEST(StateMachine, happyPath) {
    ExecutionStateMachine m;
    CHECK(m.transitionTo(S::Validating));
    CHECK(m.transitionTo(S::Running));
    CHECK(m.transitionTo(S::Succeeded));
    CHECK(m.transitionTo(S::Idle));
}

TEST(StateMachine, illegalTransitionsAreRejected) {
    ExecutionStateMachine m;
    std::string err;
    CHECK(!m.transitionTo(S::Paused, &err));  // Idle -> Paused
    CHECK(err.find("Idle -> Paused") != std::string::npos);
    CHECK(m.state() == S::Idle);
    CHECK(!m.transitionTo(S::Running));
    CHECK(!m.transitionTo(S::Succeeded));
    CHECK(!m.transitionTo(S::Cancelling));
    CHECK(m.state() == S::Idle);

    CHECK(m.transitionTo(S::Validating));
    CHECK(m.transitionTo(S::Running));
    CHECK(m.transitionTo(S::Succeeded));
    CHECK(!m.transitionTo(S::Running));   // 终态不能直接回到 Running
    CHECK(!m.transitionTo(S::Failed));
    CHECK(m.state() == S::Succeeded);
}

TEST(StateMachine, cancelFromRunningAndPaused) {
    ExecutionStateMachine a;
    a.transitionTo(S::Validating);
    a.transitionTo(S::Running);
    CHECK(a.transitionTo(S::Cancelling));
    CHECK(a.transitionTo(S::Cancelled));
    CHECK(a.transitionTo(S::Idle));

    ExecutionStateMachine b;
    b.transitionTo(S::Validating);
    b.transitionTo(S::Running);
    CHECK(b.transitionTo(S::Paused));
    CHECK(b.transitionTo(S::Cancelling));
    CHECK(b.transitionTo(S::Cancelled));
}

TEST(StateMachine, pauseAndResume) {
    ExecutionStateMachine m;
    m.transitionTo(S::Validating);
    m.transitionTo(S::Running);
    CHECK(m.transitionTo(S::Paused));
    CHECK(m.transitionTo(S::Running));
}

TEST(StateMachine, cancellingOnlyEndsCancelled) {
    ExecutionStateMachine m;
    m.transitionTo(S::Validating);
    m.transitionTo(S::Running);
    m.transitionTo(S::Cancelling);
    CHECK(!m.transitionTo(S::Running));
    CHECK(!m.transitionTo(S::Failed));
    CHECK(!m.transitionTo(S::Succeeded));
    CHECK(m.transitionTo(S::Cancelled));
}

TEST(StateMachine, validationFailure) {
    ExecutionStateMachine m;
    m.transitionTo(S::Validating);
    CHECK(m.transitionTo(S::Failed));
}

TEST(StateMachine, fullTransitionTable) {
    const S all[] = {S::Idle, S::Validating, S::Running, S::Paused, S::Cancelling, S::Succeeded, S::Failed, S::Cancelled};
    int legal = 0;
    for (S from : all)
        for (S to : all)
            if (canTransition(from, to)) ++legal;
    CHECK_EQ(legal, 14);
    for (S s : all) CHECK(!canTransition(s, s));  // 没有自环
}

TEST(StateMachine, listenerSeesEveryChangeButNotRejected) {
    ExecutionStateMachine m;
    std::vector<std::string> seen;
    m.setListener([&](S from, S to) { seen.push_back(std::string(toString(from)) + ">" + toString(to)); });
    m.transitionTo(S::Paused);  // 非法
    m.transitionTo(S::Validating);
    m.transitionTo(S::Running);
    CHECK_EQ(seen.size(), static_cast<std::size_t>(2));
    CHECK_EQ(seen[0], std::string("Idle>Validating"));
    CHECK_EQ(seen[1], std::string("Validating>Running"));
}
