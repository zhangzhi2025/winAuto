#include "UiImageUtils.h"

#include <cstring>

namespace stock_monitor {

QImage toQImage(const Image& image) {
    if (!image.valid()) return QImage();
    QImage result(image.width, image.height, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height; ++y) {
        std::memcpy(result.scanLine(y),
                    image.rgba.data() + static_cast<std::size_t>(y) * image.stride,
                    static_cast<std::size_t>(image.width) * 4U);
    }
    return result;
}

Image fromQImage(const QImage& source) {
    Image image;
    if (source.isNull()) return image;
    const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
    image.width = rgba.width();
    image.height = rgba.height();
    image.stride = static_cast<std::size_t>(image.width) * 4U;
    image.rgba.resize(image.stride * static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        std::memcpy(image.rgba.data() + static_cast<std::size_t>(y) * image.stride,
                    rgba.constScanLine(y), image.stride);
    }
    return image;
}

QString toQString(const std::wstring& text) {
    return QString::fromWCharArray(text.data(), static_cast<int>(text.size()));
}

QString toQString(const std::string& text) {
    return QString::fromUtf8(text.c_str());
}

}  // namespace stock_monitor
