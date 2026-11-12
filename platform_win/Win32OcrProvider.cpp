#include "Win32OcrProvider.h"

#include <windows.h>

#include <algorithm>

#include "WindowGrab.h"

namespace wa::platform {

Result<OcrRecognition> Win32OcrProvider::recognize(const OcrRequest& request, const CancelToken& token) {
    using R = Result<OcrRecognition>;
    if (token.isCancelled()) return R::failure(ErrCode::Cancelled, "OCR request cancelled");
    std::shared_ptr<OcrRuntime> runtime;
    std::string initializationError;
    {
        std::lock_guard<std::mutex> lock(initializationMutex_);
        if (!runtime_ && initializationError_.empty()) {
            Result<std::shared_ptr<OcrRuntime>> initialized = OcrRuntime::create(paths_);
            if (initialized.ok()) runtime_ = std::move(initialized.value);
            else initializationError_ = initialized.detail;
        }
        runtime = runtime_;
        initializationError = initializationError_;
    }
    if (!runtime) {
        return R::failure(ErrCode::Unsupported,
                          initializationError.empty() ? "OCR runtime is not configured" : initializationError);
    }

    Result<Bitmap> capture = grabWindowClient(request.windowHandle);
    if (!capture.ok()) return failAs<OcrRecognition>(capture);
    if (token.isCancelled()) return R::failure(ErrCode::Cancelled, "OCR request cancelled");

    const int width = capture.value.w;
    const int height = capture.value.h;
    Rect region{0, 0, width, height};
    if (request.searchRegion) {
        region = *request.searchRegion;
        if (region.x < 0 || region.y < 0 || region.w <= 0 || region.h <= 0 ||
            region.x > width - region.w || region.y > height - region.h) {
            return R::failure(ErrCode::InvalidWorkflow, "OCR searchRegion is outside the window client area");
        }
    }

    Bitmap image;
    image.w = region.w;
    image.h = region.h;
    image.bgra.resize(static_cast<size_t>(region.w) * static_cast<size_t>(region.h) * 4);
    for (int y = 0; y < region.h; ++y) {
        const size_t sourceOffset =
            (static_cast<size_t>(region.y + y) * static_cast<size_t>(width) +
             static_cast<size_t>(region.x)) * 4;
        const size_t destinationOffset = static_cast<size_t>(y) * static_cast<size_t>(region.w) * 4;
        std::copy_n(capture.value.bgra.data() + sourceOffset,
                    static_cast<size_t>(region.w) * 4, image.bgra.data() + destinationOffset);
    }

    Result<std::string> recognized = runtime->recognize(image);
    if (!recognized.ok()) return failAs<OcrRecognition>(recognized);
    if (token.isCancelled()) return R::failure(ErrCode::Cancelled, "OCR request cancelled");

    HWND window = reinterpret_cast<HWND>(static_cast<UINT_PTR>(request.windowHandle));
    RECT client{};
    POINT origin{0, 0};
    if (!IsWindow(window) || !GetClientRect(window, &client) || !ClientToScreen(window, &origin)) {
        return R::failure(ErrCode::WindowNotFound, "cannot read target window client coordinates");
    }

    OcrRecognition result;
    result.clientBounds = Rect{static_cast<int>(origin.x), static_cast<int>(origin.y),
                               static_cast<int>(client.right - client.left),
                               static_cast<int>(client.bottom - client.top)};
    result.recognizedText = std::move(recognized.value);
    result.hasRegionData = false;
    return R::success(std::move(result));
}

}  // namespace wa::platform
