#pragma once

#include <QMainWindow>
#include <QFutureWatcher>

#include <atomic>
#include <memory>
#include <mutex>

#include "StockConfig.h"
#include "StockModel.h"
#include "StockMonitorService.h"

class QLabel;
class QComboBox;
class QPushButton;
class QCloseEvent;

namespace stock_monitor {

#ifdef STOCK_MONITOR_HAS_RAPIDOCR
class RapidOcrAdapter;
#endif

class StockMonitorWindow final : public QMainWindow {
public:
    explicit StockMonitorWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void refreshWindows();
    void loadImage();
    void captureWindow();
    void analyzeImage();
    void displayImage(const Image& image, const QString& source);

    QComboBox* windows_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* dependency_ = nullptr;
    QLabel* results_ = nullptr;
    QPushButton* importButton_ = nullptr;
    QPushButton* captureButton_ = nullptr;
    QPushButton* analyzeButton_ = nullptr;
    Image currentImage_;
    StockMonitorConfig config_;
    QFutureWatcher<AnalysisResult>* analysisWatcher_ = nullptr;
    std::shared_ptr<std::atomic_bool> cancellation_;
    std::uint64_t analysisGeneration_ = 0;
#ifdef STOCK_MONITOR_HAS_RAPIDOCR
    struct OcrRuntimeState {
        std::mutex mutex;
        std::shared_ptr<RapidOcrAdapter> provider;
    };
    std::shared_ptr<OcrRuntimeState> ocrRuntime_;
#endif
};

}  // namespace stock_monitor
