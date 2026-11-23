#pragma once

#include "StockModel.h"

namespace stock_monitor {

StockClassification classifyColors(const FontColorResult& nameColor,
                                   const FontColorResult& codeColor,
                                   bool hasReliableCodeColor);

}  // namespace stock_monitor
