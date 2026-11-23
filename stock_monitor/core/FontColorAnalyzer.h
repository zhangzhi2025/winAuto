#pragma once

#include "StockConfig.h"
#include "StockModel.h"

#include <vector>

namespace stock_monitor {

FontColorResult analyzeFontColor(const Image& image, const Rect& bounds, const FontColorConfig& config);
std::vector<FontColorResult> analyzeFontColors(const Image& image,
                                               const std::vector<Rect>& bounds,
                                               const FontColorConfig& config);

}  // namespace stock_monitor
