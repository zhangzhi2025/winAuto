#pragma once

#include <string>

#include "RegionConfig.h"
#include "StockConfig.h"

namespace stock_monitor {

struct RegionConfigParseResult {
    bool ok = false;
    RegionCaptureConfig config;
    // Populated when ok is false; carries the JSON path of the offending field.
    ConfigValidation validation;
};

// Deterministic, human diffable serialization. Field order and keys match the
// documented Version 1 schema.
std::string writeRegionConfigJson(const RegionCaptureConfig& config);

// Strict reader: the text must be a Version 1 region capture document whose
// values satisfy every semantic rule in validate(). Unknown keys are ignored,
// wrong types are reported with their JSON path.
RegionConfigParseResult readRegionConfigJson(const std::string& text);

// Convenience file wrappers used by the UI; the caller still chooses how to write
// atomically.
RegionConfigParseResult readRegionConfigFile(const std::string& path);
bool writeRegionConfigFile(const std::string& path,
                           const RegionCaptureConfig& config,
                           std::string& error);

}  // namespace stock_monitor
