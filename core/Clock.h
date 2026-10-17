#pragma once
#include "core/Interfaces.h"

namespace wa {

// 真实时钟：steady_clock + 可取消等待
class SteadyClock : public IClock {
public:
    std::int64_t nowMs() const override;
    bool sleepFor(int ms, const CancelToken& token) override;
};

}  // namespace wa
