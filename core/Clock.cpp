#include "core/Clock.h"

#include <chrono>

namespace wa {

std::int64_t SteadyClock::nowMs() const {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool SteadyClock::sleepFor(int ms, const CancelToken& token) {
    return token.waitFor(std::chrono::milliseconds(ms));
}

}  // namespace wa
