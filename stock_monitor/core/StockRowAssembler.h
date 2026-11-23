#pragma once

#include <vector>

#include "StockConfig.h"
#include "StockModel.h"

namespace stock_monitor {

struct RowAssemblyResult {
    std::vector<StockRowCandidate> rows;
    std::vector<OcrTextBlock> unassignedBlocks;
};

RowAssemblyResult assembleRows(const std::vector<OcrTextBlock>& blocks,
                               int imageWidth,
                               const TableLayoutConfig& config);

}  // namespace stock_monitor
