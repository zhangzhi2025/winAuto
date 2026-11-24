#include "testing.h"

#include <cstdint>
#include <vector>

#include "RegionGeometry.h"

using namespace stock_monitor;

namespace {

Image makeImage(int width, int height, std::size_t paddingBytes = 0) {
    Image image;
    image.width = width;
    image.height = height;
    image.stride = static_cast<std::size_t>(width) * 4U + paddingBytes;
    image.rgba.assign(image.stride * static_cast<std::size_t>(height), 0);
    for(int y = 0; y < height; ++y) {
        for(int x = 0; x < width; ++x) {
            const std::size_t offset =
                static_cast<std::size_t>(y) * image.stride + static_cast<std::size_t>(x) * 4U;
            const std::uint8_t marker = static_cast<std::uint8_t>(x * 10 + y);
            image.rgba[offset] = marker;
            image.rgba[offset + 1] = marker;
            image.rgba[offset + 2] = marker;
            image.rgba[offset + 3] = 255;
        }
    }
    return image;
}

std::uint8_t pixelMarker(const Image& image, int x, int y) {
    const std::size_t offset =
        static_cast<std::size_t>(y) * image.stride + static_cast<std::size_t>(x) * 4U;
    return image.rgba[offset];
}

}  // namespace

TEST(region_geometry_client_size_must_match_exactly) {
    const GeometryCheck ok = checkClientSize(Size{1024, 768}, Size{1024, 768});
    CHECK(ok.ok);
    CHECK(ok.error == GeometryError::None);
    CHECK(ok.message.empty());

    const GeometryCheck narrower = checkClientSize(Size{1024, 768}, Size{1023, 768});
    CHECK(!narrower.ok);
    CHECK(narrower.error == GeometryError::ClientSizeMismatch);
    CHECK_CONTAINS(narrower.message, std::string("never rescaled"));

    const GeometryCheck shorter = checkClientSize(Size{1024, 768}, Size{1024, 767});
    CHECK(!shorter.ok);
    CHECK(shorter.error == GeometryError::ClientSizeMismatch);
}

TEST(region_geometry_rejects_unmeasurable_sizes) {
    CHECK(checkClientSize(Size{0, 768}, Size{1024, 768}).error == GeometryError::InvalidWindow);
    CHECK(checkClientSize(Size{1024, 0}, Size{1024, 768}).error == GeometryError::InvalidWindow);
    CHECK(checkClientSize(Size{1024, 768}, Size{0, 768}).error == GeometryError::InvalidWindow);
    CHECK(checkClientSize(Size{1024, 768}, Size{1024, -5}).error == GeometryError::InvalidWindow);
}

TEST(region_geometry_crop_rect_validation) {
    const Size source{100, 50};
    CHECK(checkCropRect(Rect{0, 0, 100, 50}, source).ok);
    CHECK(checkCropRect(Rect{10, 5, 90, 45}, source).ok);

    CHECK(checkCropRect(Rect{0, 0, 0, 10}, source).error == GeometryError::EmptyRegion);
    CHECK(checkCropRect(Rect{0, 0, 10, -1}, source).error == GeometryError::EmptyRegion);
    CHECK(checkCropRect(Rect{-1, 0, 10, 10}, source).error == GeometryError::OutOfBounds);
    CHECK(checkCropRect(Rect{0, -1, 10, 10}, source).error == GeometryError::OutOfBounds);

    const GeometryCheck tooWide = checkCropRect(Rect{10, 0, 91, 10}, source);
    CHECK(tooWide.error == GeometryError::OutOfBounds);
    CHECK_CONTAINS(tooWide.message, std::string("100x50"));

    CHECK(checkCropRect(Rect{0, 40, 100, 11}, source).error == GeometryError::OutOfBounds);
    CHECK(checkCropRect(Rect{0, 0, 10, 10}, Size{0, 0}).error == GeometryError::InvalidImage);
}

TEST(region_geometry_crop_region_copies_source_pixels) {
    const Image source = makeImage(4, 3);
    Image cropped;
    const GeometryCheck check = cropRegion(source, Rect{1, 1, 2, 2}, cropped);
    CHECK(check.ok);
    CHECK_EQ(cropped.width, 2);
    CHECK_EQ(cropped.height, 2);
    CHECK_EQ(cropped.stride, static_cast<std::size_t>(8));
    CHECK_EQ(cropped.rgba.size(), static_cast<std::size_t>(16));
    CHECK_EQ(static_cast<int>(pixelMarker(cropped, 0, 0)), 11);
    CHECK_EQ(static_cast<int>(pixelMarker(cropped, 1, 0)), 21);
    CHECK_EQ(static_cast<int>(pixelMarker(cropped, 0, 1)), 12);
    CHECK_EQ(static_cast<int>(pixelMarker(cropped, 1, 1)), 22);
    CHECK_EQ(static_cast<int>(cropped.rgba[3]), 255);
}

TEST(region_geometry_crop_region_honours_source_stride) {
    const Image source = makeImage(4, 3, 4);
    Image cropped;
    CHECK(cropRegion(source, Rect{2, 0, 2, 3}, cropped).ok);
    CHECK_EQ(cropped.width, 2);
    CHECK_EQ(cropped.height, 3);
    CHECK_EQ(cropped.stride, static_cast<std::size_t>(8));
    CHECK_EQ(static_cast<int>(pixelMarker(cropped, 0, 0)), 20);
    CHECK_EQ(static_cast<int>(pixelMarker(cropped, 1, 2)), 32);
    CHECK_EQ(static_cast<int>(pixelMarker(source, 3, 2)), 32);
}

TEST(region_geometry_crop_region_rejects_bad_input) {
    Image output;
    Image invalid;
    invalid.width = 4;
    invalid.height = 4;
    CHECK(cropRegion(invalid, Rect{0, 0, 1, 1}, output).error == GeometryError::InvalidImage);

    const Image source = makeImage(4, 3);
    CHECK(cropRegion(source, Rect{0, 0, 0, 0}, output).error == GeometryError::EmptyRegion);
    CHECK(cropRegion(source, Rect{3, 0, 2, 1}, output).error == GeometryError::OutOfBounds);

    Image truncated = makeImage(4, 3);
    truncated.rgba.resize(truncated.stride * 2U);
    const GeometryCheck check = cropRegion(truncated, Rect{0, 0, 4, 3}, output);
    CHECK(!check.ok);
    CHECK(check.error == GeometryError::InvalidImage);
}

TEST(region_geometry_preview_transform_is_letterboxed) {
    const PreviewTransform wide = computePreviewTransform(Size{100, 50}, Size{200, 200});
    CHECK(wide.valid());
    CHECK_EQ(wide.scaledWidth(), 200);
    CHECK_EQ(wide.scaledHeight(), 100);
    CHECK_EQ(wide.offsetX, 0);
    CHECK_EQ(wide.offsetY, 50);

    const PreviewTransform tall = computePreviewTransform(Size{50, 100}, Size{200, 200});
    CHECK_EQ(tall.scaledWidth(), 100);
    CHECK_EQ(tall.scaledHeight(), 200);
    CHECK_EQ(tall.offsetX, 50);
    CHECK_EQ(tall.offsetY, 0);

    const PreviewTransform shrunk = computePreviewTransform(Size{8, 8}, Size{4, 4});
    CHECK_EQ(shrunk.scaledWidth(), 4);
    CHECK_EQ(shrunk.scaledHeight(), 4);
    CHECK_EQ(shrunk.offsetX, 0);
    CHECK_EQ(shrunk.offsetY, 0);
}

TEST(region_geometry_preview_transform_rejects_empty_sizes) {
    CHECK(!computePreviewTransform(Size{0, 10}, Size{10, 10}).valid());
    CHECK(!computePreviewTransform(Size{10, 10}, Size{0, 10}).valid());
    CHECK(!computePreviewTransform(Size{10, 10}, Size{-1, 10}).valid());
    CHECK(!computePreviewTransform(Size{10, 10}, Size{10, 0}).valid());
}

TEST(region_geometry_maps_display_points_to_source_pixels) {
    const PreviewTransform transform = computePreviewTransform(Size{100, 50}, Size{200, 200});
    Point point;
    CHECK(displayToSourcePoint(transform, 0, 50, point));
    CHECK_EQ(point.x, 0);
    CHECK_EQ(point.y, 0);

    CHECK(displayToSourcePoint(transform, 198, 148, point));
    CHECK_EQ(point.x, 99);
    CHECK_EQ(point.y, 49);

    // Points inside the letterbox border collapse onto the image edge.
    CHECK(displayToSourcePoint(transform, 0, 0, point));
    CHECK_EQ(point.x, 0);
    CHECK_EQ(point.y, 0);

    CHECK(displayToSourcePoint(transform, 500, 500, point));
    CHECK_EQ(point.x, 99);
    CHECK_EQ(point.y, 49);

    CHECK(!displayToSourcePoint(PreviewTransform{}, 0, 0, point));
}

TEST(region_geometry_drag_rect_is_normalized_and_clamped) {
    const PreviewTransform transform = computePreviewTransform(Size{10, 10}, Size{10, 10});
    const Rect forward = dragToSourceRect(transform, 2, 3, 5, 7);
    CHECK_EQ(forward.x, 2);
    CHECK_EQ(forward.y, 3);
    CHECK_EQ(forward.width, 4);
    CHECK_EQ(forward.height, 5);

    const Rect reverse = dragToSourceRect(transform, 5, 7, 2, 3);
    CHECK_EQ(reverse.x, 2);
    CHECK_EQ(reverse.y, 3);
    CHECK_EQ(reverse.width, 4);
    CHECK_EQ(reverse.height, 5);

    const Rect clamped = dragToSourceRect(transform, -20, -20, 400, 400);
    CHECK_EQ(clamped.x, 0);
    CHECK_EQ(clamped.y, 0);
    CHECK_EQ(clamped.width, 10);
    CHECK_EQ(clamped.height, 10);

    const Rect single = dragToSourceRect(transform, 4, 4, 4, 4);
    CHECK_EQ(single.x, 4);
    CHECK_EQ(single.y, 4);
    CHECK_EQ(single.width, 1);
    CHECK_EQ(single.height, 1);

    const Rect empty = dragToSourceRect(PreviewTransform{}, 0, 0, 5, 5);
    CHECK(empty.empty());
}

TEST(region_geometry_same_image_size) {
    const Image image = makeImage(4, 3);
    CHECK(sameImageSize(image, Size{4, 3}));
    CHECK(!sameImageSize(image, Size{3, 4}));
    CHECK(!sameImageSize(image, Size{4, 4}));
}
