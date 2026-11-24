#pragma once

#include <QString>
#include <QWidget>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "RegionCaptureModel.h"
#include "RegionConfig.h"
#include "RegionSessionController.h"
#include "StockConfig.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimeEdit;

namespace stock_monitor {

class RegionEditor;
class RegionSelector;

// The region-recognition workspace: window binding, configuration file handling,
// region calibration, the scheduled capture session and the per-region results.
//
// The workspace never rescales anything. A configuration only works while the
// live client area is exactly the recorded physical size; any other size keeps
// the configuration contents but blocks capture, OCR and scheduling until the
// operator re-calibrates explicitly.
class RegionWorkspace final : public QWidget {
public:
    explicit RegionWorkspace(QWidget* parent = nullptr);
    ~RegionWorkspace() override;

    // Stops the capture session and waits for in-flight work; call before the
    // window closes so no worker outlives the widgets.
    void shutdown();

    // Returns false when the user cancels closing because unsaved region edits
    // are still pending; the owner must then ignore the close event.
    bool confirmClose();

private:
    enum class SizeCompatibility { Unknown, Compatible, Mismatch };

    void buildUi();

    void refreshWindows();
    void onWindowChanged();
    void captureSnapshot();
    void measureLiveSize();
    void updateSizeCompatibility();
    void recalibrateToLiveSize();

    void syncConfigIdentity();
    void onEditorRegionsChanged();
    void onActiveRegionChanged(const QString& regionId);
    void onRectEdited(const Rect& rect);
    void refreshOverlays();

    bool confirmDiscardChanges();
    void openConfig();
    void saveConfig();
    void saveConfigAs();
    bool writeConfigFile(const QString& path);
    void applyLoadedConfig(const RegionCaptureConfig& config, const QString& path);

    bool canStartSession(QString& reason) const;
    void startSession();
    void stopSession();
    void runSingleRegionTest();
    void updateControls();
    void setDirty(bool dirty);
    void setStatus(const QString& message, bool error);

    void onSnapshot(const RegionSessionSnapshot& snapshot);
    void onResult(const RegionCaptureResult& result);
    void showResult(const RegionCaptureResult& result, ResultAvailability availability);
    void showStaleSuccess(const RegionCaptureResult& stale);
    void clearResultView(const QString& reason);
    void refreshRuntimeTable(const RegionSessionSnapshot& snapshot);
    void refreshSessionLabel(const RegionSessionSnapshot& snapshot);

    int regionIndexById(const QString& regionId) const;
    const CaptureRegion* regionById(const QString& regionId) const;
    const RegionRuntimeStatus* runtimeStatusById(const QString& regionId) const;

    QComboBox* windowCombo_ = nullptr;
    QPushButton* refreshButton_ = nullptr;
    QPushButton* captureButton_ = nullptr;
    QLabel* liveSizeLabel_ = nullptr;

    QLineEdit* nameEdit_ = nullptr;
    QTimeEdit* startTimeEdit_ = nullptr;
    QTimeEdit* endTimeEdit_ = nullptr;
    QSpinBox* lowConfidenceSpin_ = nullptr;

    QPushButton* openButton_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* saveAsButton_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* testButton_ = nullptr;
    QPushButton* recalibrateButton_ = nullptr;
    QLabel* sessionLabel_ = nullptr;
    QLabel* compatibilityLabel_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* dirtyLabel_ = nullptr;

    RegionSelector* selector_ = nullptr;
    RegionEditor* editor_ = nullptr;

    QLabel* resultTitle_ = nullptr;
    QLabel* resultStatus_ = nullptr;
    QLabel* resultMeta_ = nullptr;
    QLabel* resultImage_ = nullptr;
    QTableWidget* fieldTable_ = nullptr;
    QPlainTextEdit* rawText_ = nullptr;
    QTableWidget* runtimeTable_ = nullptr;

    RegionCaptureConfig config_;
    FontColorConfig fontColor_;
    RegionSessionController controller_;
    std::map<std::string, RegionResultTracker> trackers_;
    RegionSessionSnapshot sessionSnapshot_;

    Image snapshot_;
    Size liveSize_;
    SizeCompatibility compatibility_ = SizeCompatibility::Unknown;
    std::uint64_t boundWindow_ = 0;
    QString boundTitle_;
    QString configPath_;
    QString displayedRegionId_;
    QString renderedRegionId_;
    ResultAvailability renderedAvailability_ = ResultAvailability::None;
    bool dirty_ = false;
    bool suppressWindowChange_ = false;
    bool shuttingDown_ = false;
    std::uint64_t singleTestToken_ = 0;
};

}  // namespace stock_monitor
