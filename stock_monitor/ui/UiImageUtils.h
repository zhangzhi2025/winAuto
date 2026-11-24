#pragma once

#include <QImage>
#include <QString>

#include <string>

#include "StockModel.h"

namespace stock_monitor {

// Conversion helpers shared by the stock monitor widgets. The RGBA byte layout of
// Image matches QImage::Format_RGBA8888, so no pixel swizzling is required.
QImage toQImage(const Image& image);
Image fromQImage(const QImage& source);
QString toQString(const std::wstring& text);
QString toQString(const std::string& text);

}  // namespace stock_monitor
