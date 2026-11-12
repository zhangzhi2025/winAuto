#include "OcrRuntime.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>
#include <iterator>

#include "OcrLiteCApi.h"
#include "ImageFile.h"

namespace wa::platform {
namespace {

Status checkFile(const std::string& path, const char* label) {
    std::error_code error;
    if (path.empty() || !std::filesystem::is_regular_file(std::filesystem::u8path(path), error)) {
        return Status::failure(ErrCode::Unsupported,
                               std::string("OCR ") + label + " file is missing: " + path);
    }
    return okStatus();
}

bool isAscii(const std::string& value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch < 0x80; });
}

class TemporaryPng {
public:
    static Result<TemporaryPng> create() {
        wchar_t tempDirectory[MAX_PATH]{};
        const DWORD length = GetTempPathW(MAX_PATH, tempDirectory);
        if (length == 0 || length >= MAX_PATH) {
            return Result<TemporaryPng>::failure(ErrCode::Internal, "cannot locate the Windows temporary directory");
        }

        wchar_t shortDirectory[MAX_PATH]{};
        const DWORD shortLength = GetShortPathNameW(tempDirectory, shortDirectory, MAX_PATH);
        if (shortLength == 0 || shortLength >= MAX_PATH) {
            return Result<TemporaryPng>::failure(ErrCode::Unsupported,
                                                 "RapidOcrOnnx requires an ASCII temporary directory path");
        }
        for (DWORD i = 0; i < shortLength; ++i) {
            if (shortDirectory[i] > 0x7f) {
                return Result<TemporaryPng>::failure(ErrCode::Unsupported,
                                                     "RapidOcrOnnx requires an ASCII temporary directory path");
            }
        }

        wchar_t tempFile[MAX_PATH]{};
        if (GetTempFileNameW(shortDirectory, L"ocr", 0, tempFile) == 0) {
            return Result<TemporaryPng>::failure(ErrCode::Internal, "cannot create OCR temporary image path");
        }
        DeleteFileW(tempFile);
        std::filesystem::path pngPath(tempFile);
        pngPath.replace_extension(L".png");

        TemporaryPng result;
        result.fullPath_ = pngPath.string();
        const std::filesystem::path filePath = pngPath.filename();
        result.directory_ = pngPath.parent_path().string();
        if (!result.directory_.empty() && result.directory_.back() != '\\') result.directory_.push_back('\\');
        result.fileName_ = filePath.string();
        if (!isAscii(result.fullPath_) || !isAscii(result.directory_) || !isAscii(result.fileName_)) {
            return Result<TemporaryPng>::failure(ErrCode::Unsupported,
                                                 "RapidOcrOnnx requires an ASCII temporary image path");
        }
        return Result<TemporaryPng>::success(std::move(result));
    }

    TemporaryPng() = default;
    TemporaryPng(const TemporaryPng&) = delete;
    TemporaryPng& operator=(const TemporaryPng&) = delete;
    TemporaryPng(TemporaryPng&& other) noexcept
        : fullPath_(std::move(other.fullPath_)),
          directory_(std::move(other.directory_)),
          fileName_(std::move(other.fileName_)) {
        other.fullPath_.clear();
    }
    TemporaryPng& operator=(TemporaryPng&& other) noexcept {
        if (this != &other) {
            if (!fullPath_.empty()) DeleteFileA(fullPath_.c_str());
            fullPath_ = std::move(other.fullPath_);
            directory_ = std::move(other.directory_);
            fileName_ = std::move(other.fileName_);
            other.fullPath_.clear();
        }
        return *this;
    }
    ~TemporaryPng() {
        if (!fullPath_.empty()) DeleteFileA(fullPath_.c_str());
    }

    const std::string& fullPath() const { return fullPath_; }
    const std::string& directory() const { return directory_; }
    const std::string& fileName() const { return fileName_; }

private:
    std::string fullPath_;
    std::string directory_;
    std::string fileName_;
};

}  // namespace

OcrRuntime::OcrRuntime(void* handle) : handle_(handle) {}

OcrRuntime::~OcrRuntime() {
    if (handle_) OcrDestroy(handle_);
}

Result<std::shared_ptr<OcrRuntime>> OcrRuntime::create(const OcrModelPaths& paths) {
    const std::pair<const std::string*, const char*> requiredFiles[] = {
        {&paths.detector, "detector"}, {&paths.classifier, "classifier"},
        {&paths.recognizer, "recognizer"}, {&paths.dictionary, "dictionary"},
        {&paths.verificationImage, "verification image"}};
    for (const auto& file : requiredFiles) {
        const Status status = checkFile(*file.first, file.second);
        if (!status.ok()) return Result<std::shared_ptr<OcrRuntime>>::failure(status.code, status.detail);
        if (!isAscii(*file.first)) {
            return Result<std::shared_ptr<OcrRuntime>>::failure(
                ErrCode::Unsupported, std::string("RapidOcrOnnx C API requires an ASCII ") + file.second + " path");
        }
    }
    if (paths.threads < 1 || paths.threads > 32) {
        return Result<std::shared_ptr<OcrRuntime>>::failure(ErrCode::Unsupported,
                                                            "OCR thread count must be between 1 and 32");
    }

    try {
        void* handle = OcrInit(paths.detector.c_str(), paths.classifier.c_str(),
                               paths.recognizer.c_str(), paths.dictionary.c_str(), paths.threads);
        if (!handle) {
            return Result<std::shared_ptr<OcrRuntime>>::failure(ErrCode::Unsupported,
                                                                "RapidOcrOnnx OcrInit returned a null handle");
        }
        auto runtime = std::shared_ptr<OcrRuntime>(new OcrRuntime(handle));
        Result<Bitmap> verification = loadImage(paths.verificationImage);
        if (!verification.ok()) {
            return Result<std::shared_ptr<OcrRuntime>>::failure(verification.code,
                                                                verification.detail);
        }
        Result<std::string> verified = runtime->recognize(verification.value);
        if (!verified.ok() || verified.value.empty()) {
            const std::string detail = verified.ok()
                                           ? "verification image produced no recognized text"
                                           : verified.detail;
            return Result<std::shared_ptr<OcrRuntime>>::failure(
                verified.ok() ? ErrCode::Unsupported : verified.code,
                "RapidOcrOnnx verification inference failed: " + detail);
        }
        return Result<std::shared_ptr<OcrRuntime>>::success(std::move(runtime));
    } catch (const std::exception& e) {
        return Result<std::shared_ptr<OcrRuntime>>::failure(
            ErrCode::Unsupported, std::string("RapidOcrOnnx initialization failed: ") + e.what());
    }
}

Result<std::string> OcrRuntime::recognize(const Bitmap& image) {
    if (image.w <= 0 || image.h <= 0 ||
        image.bgra.size() != static_cast<size_t>(image.w) * static_cast<size_t>(image.h) * 4) {
        return Result<std::string>::failure(ErrCode::InvalidWorkflow, "invalid OCR image bitmap");
    }
    Result<TemporaryPng> temp = TemporaryPng::create();
    if (!temp.ok()) return failAs<std::string>(temp);
    const Status saved = savePng(temp.value.fullPath(), image);
    if (!saved.ok()) return failAs<std::string>(saved);

    std::lock_guard<std::mutex> lock(mutex_);
    try {
        OCR_PARAM params{};
        params.padding = 50;
        params.maxSideLen = 1024;
        params.boxScoreThresh = 0.6f;
        params.boxThresh = 0.3f;
        params.unClipRatio = 2.0f;
        params.doAngle = 1;
        params.mostAngle = 1;

        if (!OcrDetect(handle_, temp.value.directory().c_str(), temp.value.fileName().c_str(), &params)) {
            return Result<std::string>::success({});
        }
        const int length = OcrGetLen(handle_);
        if (length < 1 || length > (1 << 20)) {
            return Result<std::string>::failure(ErrCode::LimitExceeded, "RapidOcrOnnx returned an invalid result length");
        }
        std::vector<char> buffer(static_cast<size_t>(length), '\0');
        (void)OcrGetResult(handle_, buffer.data(), length);
        buffer.back() = '\0';
        return Result<std::string>::success(std::string(buffer.data()));
    } catch (const std::exception& e) {
        return Result<std::string>::failure(ErrCode::Unsupported,
                                            std::string("RapidOcrOnnx inference failed: ") + e.what());
    }
}

OcrModelPaths defaultOcrModelPaths() {
    wchar_t executablePath[32768]{};
    const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath, static_cast<DWORD>(std::size(executablePath)));
    std::filesystem::path modelDirectory;
    if (pathLength > 0 && pathLength < std::size(executablePath)) {
        modelDirectory = std::filesystem::path(executablePath).parent_path() / L"models";
    }
    std::error_code error;
    if (modelDirectory.empty() || !std::filesystem::is_directory(modelDirectory, error)) {
#ifdef WINAUTO_OCR_MODEL_DIR
        modelDirectory = std::filesystem::u8path(WINAUTO_OCR_MODEL_DIR);
#else
        modelDirectory = std::filesystem::u8path("models");
#endif
    }
    OcrModelPaths paths;
    paths.detector = (modelDirectory / "ch_PP-OCRv3_det_infer.onnx").u8string();
    paths.classifier = (modelDirectory / "ch_ppocr_mobile_v2.0_cls_train.onnx").u8string();
    paths.recognizer = (modelDirectory / "ch_PP-OCRv3_rec_infer.onnx").u8string();
    paths.dictionary = (modelDirectory / "ppocr_keys_v1.txt").u8string();
    paths.verificationImage = (modelDirectory / "ocr-smoke-test.jpg").u8string();
    return paths;
}

}  // namespace wa::platform
