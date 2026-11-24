#pragma once

#include <string>

#include "RegionCaptureModel.h"
#include "RegionConfig.h"
#include "StockModel.h"

namespace stock_monitor {

enum class GeometryError {
    None,
    InvalidWindow,
    ClientSizeMismatch,
    InvalidImage,
    EmptyRegion,
    OutOfBounds
};

struct GeometryCheck {
    bool ok = false;
    GeometryError error = GeometryError::None;
    std::string message;
};

// A configuration is only usable when both client dimensions match exactly.
// Coordinates are never scaled, so no tolerance is applied.
GeometryCheck checkClientSize(const Size& reference, const Size& actual);

GeometryCheck checkCropRect(const Rect& rect, const Size& sourceSize);

// Copies the requested rectangle out of a full client-area image. The source
// pixels are preserved so later color analysis sees the original values.
GeometryCheck cropRegion(const Image& source, const Rect& rect, Image& output);

// Maps between a preview widget and the captured client image. The preview keeps
// the aspect ratio and is letterboxed, so the mapping is a single uniform scale
// plus a centering offset.
struct PreviewTransform {
    Size source;
    Size display;
    double scale = 0.0;
    int offsetX = 0;
    int offsetY = 0;

    bool valid() const { return scale > 0.0 && source.width > 0 && source.height > 0; }
    int scaledWidth() const;
    int scaledHeight() const;
};

PreviewTransform computePreviewTransform(const Size& source, const Size& display);

// Converts a preview widget point into source client pixels, clamping points
// that fall in the letterbox border onto the image edge.
bool displayToSourcePoint(const PreviewTransform& transform, int displayX, int displayY, Point& point);

// Converts a drag (any direction) into a normalized source rectangle. A drag that
// does not intersect the image yields an empty rectangle.
Rect dragToSourceRect(const PreviewTransform& transform,
                      int startX,
                      int startY,
                      int endX,
                      int endY);

bool sameImageSize(const Image& image, const Size& size);

}  // namespace stock_monitor
