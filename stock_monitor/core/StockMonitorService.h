#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "FontColorAnalyzer.h"
#include "StockClassifier.h"
#include "StockModel.h"
#include "StockRowAssembler.h"

namespace stock_monitor {

struct OcrProviderResult {
    AnalysisError error = AnalysisError::None;
    std::string message;
    std::vector<OcrTextBlock> blocks;
};

class OcrProvider {
public:
    virtual ~OcrProvider() = default;
    virtual OcrProviderResult recognize(const Image& image, const std::atomic_bool& cancelled) = 0;
};

AnalysisResult analyze(const Image& image,
                       const std::vector<OcrTextBlock>& blocks,
                       const StockMonitorConfig& config);
AnalysisResult analyzeWithProvider(const Image& image,
                                  const StockMonitorConfig& config,
                                  OcrProvider& provider,
                                  const std::atomic_bool& cancelled);

}  // namespace stock_monitor
