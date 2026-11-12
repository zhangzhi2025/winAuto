#include "platform_win/OcrRuntime.h"
#include "testing.h"

using namespace wa;

TEST(rapid_ocr_clib_loads_models_and_passes_verification_inference) {
    const Result<std::shared_ptr<wa::platform::OcrRuntime>> runtime =
        wa::platform::OcrRuntime::create(wa::platform::defaultOcrModelPaths());
    CHECK(runtime.ok());
    if (!runtime.ok()) CHECK(!runtime.detail.empty());
}

TEST(rapid_ocr_clib_reports_missing_model_files) {
    wa::platform::OcrModelPaths paths;
    paths.detector = "missing-detector.onnx";
    paths.classifier = "missing-classifier.onnx";
    paths.recognizer = "missing-recognizer.onnx";
    paths.dictionary = "missing-dictionary.txt";
    paths.verificationImage = "missing-image.jpg";
    const auto runtime = wa::platform::OcrRuntime::create(paths);
    CHECK(!runtime.ok());
    CHECK(runtime.code == ErrCode::Unsupported);
    CHECK_CONTAINS(runtime.detail, "detector");
}
