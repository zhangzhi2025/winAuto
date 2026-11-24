#include "RegionGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace stock_monitor {
namespace {

GeometryCheck success() {
    return GeometryCheck{true, GeometryError::None, {}};
}

GeometryCheck failure(GeometryError error, std::string message) {
    return GeometryCheck{false, error, std::move(message)};
}

std::string describe(const Size& size) {
    return std::to_string(size.width) + "x" + std::to_string(size.height);
}

int clampToRange(int value, int low, int high) {
    if(high < low) {
        return low;
    }
    return std::clamp(value, low, high);
}

}  // namespace

GeometryCheck checkClientSize(const Size& reference, const Size& actual) {
    if(reference.width <= 0 || reference.height <= 0) {
        return failure(GeometryError::InvalidWindow,
                       "the saved reference client size is not valid: " + describe(reference));
    }
    if(actual.width <= 0 || actual.height <= 0) {
        return failure(GeometryError::InvalidWindow,
                       "the window client area has no measurable size: " + describe(actual));
    }
    if(!clientSizeMatches(reference, actual)) {
        return failure(GeometryError::ClientSizeMismatch,
                       "client size changed: configuration expects " + describe(reference) +
                           " but the window is " + describe(actual) +
                           "; region coordinates are never rescaled");
    }
    return success();
}

GeometryCheck checkCropRect(const Rect& rect, const Size& sourceSize) {
    if(sourceSize.width <= 0 || sourceSize.height <= 0) {
        return failure(GeometryError::InvalidImage,
                       "source image has no measurable size: " + describe(sourceSize));
    }
    if(rect.empty()) {
        return failure(GeometryError::EmptyRegion, "region rectangle must have a positive size");
    }
    if(rect.x < 0 || rect.y < 0) {
        return failure(GeometryError::OutOfBounds, "region rectangle starts outside the source image");
    }
    const long long right = static_cast<long long>(rect.x) + rect.width;
    const long long bottom = static_cast<long long>(rect.y) + rect.height;
    if(right > sourceSize.width || bottom > sourceSize.height) {
        return failure(GeometryError::OutOfBounds,
                       "region rectangle exceeds the source image " + describe(sourceSize));
    }
    return success();
}

GeometryCheck cropRegion(const Image& source, const Rect& rect, Image& output) {
    if(!source.valid()) {
        return failure(GeometryError::InvalidImage,
                       "image dimensions, stride, or pixel buffer are invalid");
    }
    const Size sourceSize{source.width, source.height};
    const GeometryCheck rectCheck = checkCropRect(rect, sourceSize);
    if(!rectCheck.ok) {
        return rectCheck;
    }
    if(static_cast<std::size_t>(rect.width) > std::numeric_limits<std::size_t>::max() / 4U) {
        return failure(GeometryError::OutOfBounds, "region width overflows the pixel buffer size");
    }
    const std::size_t destinationStride = static_cast<std::size_t>(rect.width) * 4U;
    if(static_cast<std::size_t>(rect.height) >
       std::numeric_limits<std::size_t>::max() / destinationStride) {
        return failure(GeometryError::OutOfBounds, "region height overflows the pixel buffer size");
    }
    const std::size_t destinationBytes = destinationStride * static_cast<std::size_t>(rect.height);
    const std::size_t sourceOffset =
        static_cast<std::size_t>(rect.y) * static_cast<std::size_t>(source.stride) +
        static_cast<std::size_t>(rect.x) * 4U;
    if(sourceOffset > source.rgba.size() || source.rgba.size() - sourceOffset < destinationBytes) {
        return failure(GeometryError::InvalidImage, "source pixel buffer is smaller than its geometry");
    }

    Image cropped;
    cropped.width = rect.width;
    cropped.height = rect.height;
    cropped.stride = static_cast<int>(destinationStride);
    cropped.rgba.assign(destinationBytes, 0);
    const int sourceStride = source.stride;
    const std::size_t rowBytes = destinationStride;
    for(int row = 0; row < rect.height; ++row) {
        const std::size_t sourceRow =
            sourceOffset + static_cast<std::size_t>(row) * static_cast<std::size_t>(sourceStride);
        std::memcpy(cropped.rgba.data() + static_cast<std::size_t>(row) * rowBytes,
                    source.rgba.data() + sourceRow,
                    rowBytes);
    }
    output = std::move(cropped);
    return success();
}

int PreviewTransform::scaledWidth() const {
    if(!valid()) {
        return 0;
    }
    const int width = static_cast<int>(std::lround(static_cast<double>(source.width) * scale));
    return std::min(width, std::max(display.width, 0));
}

int PreviewTransform::scaledHeight() const {
    if(!valid()) {
        return 0;
    }
    const int height = static_cast<int>(std::lround(static_cast<double>(source.height) * scale));
    return std::min(height, std::max(display.height, 0));
}

PreviewTransform computePreviewTransform(const Size& source, const Size& display) {
    PreviewTransform transform;
    transform.source = source;
    transform.display = display;
    if(source.width <= 0 || source.height <= 0 || display.width <= 0 || display.height <= 0) {
        return transform;
    }
    const double horizontal = static_cast<double>(display.width) / static_cast<double>(source.width);
    const double vertical = static_cast<double>(display.height) / static_cast<double>(source.height);
    transform.scale = std::min(horizontal, vertical);
    if(!(transform.scale > 0.0) || !std::isfinite(transform.scale)) {
        transform.scale = 0.0;
        return transform;
    }
    transform.offsetX = (display.width - transform.scaledWidth()) / 2;
    transform.offsetY = (display.height - transform.scaledHeight()) / 2;
    return transform;
}

bool displayToSourcePoint(const PreviewTransform& transform, int displayX, int displayY, Point& point) {
    if(!transform.valid()) {
        return false;
    }
    const int width = transform.scaledWidth();
    const int height = transform.scaledHeight();
    if(width <= 0 || height <= 0) {
        return false;
    }
    const int clampedX = clampToRange(displayX, transform.offsetX, transform.offsetX + width - 1);
    const int clampedY = clampToRange(displayY, transform.offsetY, transform.offsetY + height - 1);
    const int sourceX = static_cast<int>(
        std::floor(static_cast<double>(clampedX - transform.offsetX) / transform.scale));
    const int sourceY = static_cast<int>(
        std::floor(static_cast<double>(clampedY - transform.offsetY) / transform.scale));
    point.x = clampToRange(sourceX, 0, transform.source.width - 1);
    point.y = clampToRange(sourceY, 0, transform.source.height - 1);
    return true;
}

Rect dragToSourceRect(const PreviewTransform& transform,
                      int startX,
                      int startY,
                      int endX,
                      int endY) {
    Point start;
    Point end;
    if(!displayToSourcePoint(transform, startX, startY, start) ||
       !displayToSourcePoint(transform, endX, endY, end)) {
        return Rect{};
    }
    const int left = std::min(start.x, end.x);
    const int top = std::min(start.y, end.y);
    const int right = std::min(std::max(start.x, end.x) + 1, transform.source.width);
    const int bottom = std::min(std::max(start.y, end.y) + 1, transform.source.height);
    if(right <= left || bottom <= top) {
        return Rect{};
    }
    Rect rect;
    rect.x = left;
    rect.y = top;
    rect.width = right - left;
    rect.height = bottom - top;
    return rect;
}

bool sameImageSize(const Image& image, const Size& size) {
    return image.width == size.width && image.height == size.height;
}

}  // namespace stock_monitor
