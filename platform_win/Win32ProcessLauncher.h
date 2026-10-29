#pragma once

#include "core/Interfaces.h"

namespace wa::platform {

class Win32ProcessLauncher : public IProcessLauncher {
public:
    Result<std::uint32_t> launch(const std::string& programPath,
                                 const std::string& arguments) override;
};

}  // namespace wa::platform
