#pragma once

#include "core/Interfaces.h"

namespace wa::platform {

class ScriptWorker final : public IScriptRunner {
public:
    Result<ScriptResult> run(const ScriptRequest& request, const CancelToken& token,
                             const ScriptHostHandler& host) override;
};

}  // namespace wa::platform
