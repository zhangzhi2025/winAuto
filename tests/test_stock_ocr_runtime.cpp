#include "testing.h"

#include <QImage>

#include <cstring>

#include "RapidOcrAdapter.h"

using namespace stock_monitor;

TEST(stock_monitor_rapidocr_initializes_models_and_returns_text_blocks) {
    RapidOcrAdapter provider;
    CHECK(provider.ready());
    if (!provider.ready()) {
        testing::reportFailure(__FILE__, __LINE__, provider.initializationError());
        return;
    }

    QImage source(QString::fromUtf8(WINAUTO_SOURCE_DIR) +
                  QStringLiteral("/third_party/RapidOcrOnnx/images/1.jpg"));
    CHECK(!source.isNull());
    if (source.isNull()) return;
    const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
    Image image;
    image.width = rgba.width();
    image.height = rgba.height();
    image.stride = static_cast<std::size_t>(image.width) * 4U;
    image.rgba.resize(image.stride * static_cast<std::size_t>(image.height));
    for (int y = 0; y < image.height; ++y) {
        std::memcpy(image.rgba.data() + static_cast<std::size_t>(y) * image.stride,
                    rgba.constScanLine(y), image.stride);
    }

    std::atomic_bool cancelled{false};
    const auto result = provider.recognize(image, cancelled);
    if (result.error != AnalysisError::None) {
        testing::reportFailure(__FILE__, __LINE__, result.message);
    }
    CHECK_EQ(result.error, AnalysisError::None);
    CHECK(!result.blocks.empty());
    for (const auto& block : result.blocks) {
        CHECK(!block.bounds.empty());
        CHECK(block.detectionConfidence >= 0.0F && block.detectionConfidence <= 1.0F);
        CHECK(block.recognitionConfidence >= 0.0F && block.recognitionConfidence <= 1.0F);
        CHECK(block.bounds.x >= 0 && block.bounds.y >= 0);
        CHECK(block.bounds.right() <= image.width && block.bounds.bottom() <= image.height);
    }
}
