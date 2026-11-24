#include "RegionWorkspace.h"

#include <QComboBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QTime>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <cstdint>
#include <utility>

#include "RegionConfigJson.h"
#include "RegionEditor.h"
#include "RegionGeometry.h"
#include "RegionScan.h"
#include "RegionSelector.h"
#include "StockWindowCapture.h"
#include "UiImageUtils.h"

namespace stock_monitor {
namespace {

QString utf8(const std::string& value) {
    return QString::fromStdString(value);
}

QString clockFromMs(qint64 milliseconds) {
    if(milliseconds <= 0) {
        return QStringLiteral("—");
    }
    return QDateTime::fromMSecsSinceEpoch(milliseconds).toString(QStringLiteral("HH:mm:ss"));
}

QString valueText(const TypedFieldValue& field) {
    switch(field.valueType) {
    case FieldValueType::Integer:
        return field.hasInteger ? QString::number(static_cast<qlonglong>(field.integerValue))
                                : QStringLiteral("—");
    case FieldValueType::Decimal:
        return field.hasNumber ? QString::number(field.numberValue, 'f', 4) : QStringLiteral("—");
    case FieldValueType::Percent:
        return field.hasNumber ? QString::number(field.numberValue, 'f', 2) + QStringLiteral("%")
                               : QStringLiteral("—");
    case FieldValueType::Text:
        break;
    }
    return field.hasText ? utf8(field.textValue) : QStringLiteral("—");
}

QString fieldStatusText(const TypedFieldValue& field) {
    QString status = utf8(toString(field.status));
    if(field.status != FieldStatus::Parsed && !field.message.empty()) {
        status += QStringLiteral("：") + utf8(field.message);
    }
    return status;
}

QString availabilityText(ResultAvailability availability) {
    switch(availability) {
    case ResultAvailability::CurrentSuccess:
        return QStringLiteral("本次成功");
    case ResultAvailability::CurrentFailure:
        return QStringLiteral("本次失败");
    case ResultAvailability::FailureWithStaleSuccess:
        return QStringLiteral("本次失败（下方为旧成功结果）");
    case ResultAvailability::None:
        break;
    }
    return QStringLiteral("暂无结果");
}

QString positionText(const Rect& rect) {
    return QStringLiteral("(%1, %2) %3 × %4 px")
        .arg(rect.x)
        .arg(rect.y)
        .arg(rect.width)
        .arg(rect.height);
}

const char* kNoRegionHint = "请先在左侧添加并选中一个区域。";

}  // namespace

RegionWorkspace::RegionWorkspace(QWidget* parent) : QWidget(parent) {
    buildUi();

    controller_.setResultHandler([this](const RegionCaptureResult& result) { onResult(result); });
    controller_.setSnapshotHandler(
        [this](const RegionSessionSnapshot& snapshot) { onSnapshot(snapshot); });
    controller_.setLogHandler([this](const std::string& message, bool error) {
        setStatus(utf8(message), error);
    });

    config_.configId = "region-config";
    nameEdit_->setText(utf8(config_.name));
    refreshWindows();
    updateControls();
}

RegionWorkspace::~RegionWorkspace() {
    shutdown();
}

void RegionWorkspace::shutdown() {
    if(shuttingDown_) {
        return;
    }
    shuttingDown_ = true;
    controller_.shutdown(5000);
}

bool RegionWorkspace::confirmClose() {
    if(controller_.running()) {
        controller_.stop();
    }
    return confirmDiscardChanges();
}

void RegionWorkspace::buildUi() {
    auto* root = new QVBoxLayout(this);

    auto* windowRow = new QHBoxLayout();
    windowRow->addWidget(new QLabel(QStringLiteral("目标窗口："), this));
    windowCombo_ = new QComboBox(this);
    windowCombo_->setMinimumWidth(280);
    windowRow->addWidget(windowCombo_, 1);
    refreshButton_ = new QPushButton(QStringLiteral("刷新窗口列表"), this);
    windowRow->addWidget(refreshButton_);
    captureButton_ = new QPushButton(QStringLiteral("截取客户区快照"), this);
    windowRow->addWidget(captureButton_);
    liveSizeLabel_ = new QLabel(QStringLiteral("客户区尺寸：未测量"), this);
    windowRow->addWidget(liveSizeLabel_);
    root->addLayout(windowRow);

    auto* configRow = new QHBoxLayout();
    configRow->addWidget(new QLabel(QStringLiteral("配置名称："), this));
    nameEdit_ = new QLineEdit(this);
    nameEdit_->setPlaceholderText(QStringLiteral("例如 分时看盘"));
    configRow->addWidget(nameEdit_, 1);
    configRow->addWidget(new QLabel(QStringLiteral("每日开始："), this));
    startTimeEdit_ = new QTimeEdit(QTime(9, 30), this);
    startTimeEdit_->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    configRow->addWidget(startTimeEdit_);
    configRow->addWidget(new QLabel(QStringLiteral("每日结束："), this));
    endTimeEdit_ = new QTimeEdit(QTime(15, 0), this);
    endTimeEdit_->setDisplayFormat(QStringLiteral("HH:mm:ss"));
    configRow->addWidget(endTimeEdit_);
    configRow->addWidget(new QLabel(QStringLiteral("低置信度阈值："), this));
    lowConfidenceSpin_ = new QSpinBox(this);
    lowConfidenceSpin_->setRange(0, 100);
    lowConfidenceSpin_->setValue(50);
    lowConfidenceSpin_->setSuffix(QStringLiteral(" %"));
    lowConfidenceSpin_->setMaximumWidth(90);
    configRow->addWidget(lowConfidenceSpin_);
    root->addLayout(configRow);

    auto* actionRow = new QHBoxLayout();
    openButton_ = new QPushButton(QStringLiteral("打开配置…"), this);
    actionRow->addWidget(openButton_);
    saveButton_ = new QPushButton(QStringLiteral("保存"), this);
    actionRow->addWidget(saveButton_);
    saveAsButton_ = new QPushButton(QStringLiteral("另存为…"), this);
    actionRow->addWidget(saveAsButton_);
    startButton_ = new QPushButton(QStringLiteral("开始采集"), this);
    actionRow->addWidget(startButton_);
    stopButton_ = new QPushButton(QStringLiteral("停止采集"), this);
    actionRow->addWidget(stopButton_);
    testButton_ = new QPushButton(QStringLiteral("测试选中区域"), this);
    actionRow->addWidget(testButton_);
    recalibrateButton_ = new QPushButton(QStringLiteral("以当前尺寸重校准"), this);
    actionRow->addWidget(recalibrateButton_);
    actionRow->addStretch(1);
    sessionLabel_ = new QLabel(QStringLiteral("采集：已停止"), this);
    actionRow->addWidget(sessionLabel_);
    dirtyLabel_ = new QLabel(QStringLiteral("未修改"), this);
    actionRow->addWidget(dirtyLabel_);
    root->addLayout(actionRow);

    compatibilityLabel_ = new QLabel(this);
    compatibilityLabel_->setWordWrap(true);
    compatibilityLabel_->setStyleSheet(
        QStringLiteral("color:#8a4b08; background:#fff3cd; padding:6px;"));
    compatibilityLabel_->hide();
    root->addWidget(compatibilityLabel_);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color:#333; background:#f2f2f2; padding:6px;"));
    root->addWidget(status_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    editor_ = new RegionEditor(this);
    auto* editorScroll = new QScrollArea(this);
    editorScroll->setWidgetResizable(true);
    editorScroll->setWidget(editor_);
    editorScroll->setMinimumWidth(420);
    splitter->addWidget(editorScroll);

    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    selector_ = new RegionSelector(right);
    selector_->setMinimumSize(560, 320);
    rightLayout->addWidget(selector_, 1);

    resultTitle_ = new QLabel(QStringLiteral("区域结果"), right);
    rightLayout->addWidget(resultTitle_);
    resultStatus_ = new QLabel(right);
    resultStatus_->setWordWrap(true);
    rightLayout->addWidget(resultStatus_);
    resultMeta_ = new QLabel(right);
    resultMeta_->setWordWrap(true);
    rightLayout->addWidget(resultMeta_);
    resultImage_ = new QLabel(right);
    resultImage_->setAlignment(Qt::AlignCenter);
    resultImage_->setMinimumHeight(120);
    resultImage_->setMaximumHeight(200);
    rightLayout->addWidget(resultImage_);
    fieldTable_ = new QTableWidget(0, 6, right);
    fieldTable_->setHorizontalHeaderLabels(
        {QStringLiteral("行"), QStringLiteral("字段"), QStringLiteral("原文"),
         QStringLiteral("类型化值"), QStringLiteral("置信度"), QStringLiteral("状态 / 颜色")});
    fieldTable_->horizontalHeader()->setStretchLastSection(true);
    fieldTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    fieldTable_->setSelectionMode(QAbstractItemView::NoSelection);
    rightLayout->addWidget(fieldTable_, 1);
    rawText_ = new QPlainTextEdit(right);
    rawText_->setReadOnly(true);
    rawText_->setMaximumHeight(90);
    rawText_->setPlaceholderText(QStringLiteral("OCR 原文"));
    rightLayout->addWidget(rawText_);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    runtimeTable_ = new QTableWidget(0, 7, this);
    runtimeTable_->setHorizontalHeaderLabels(
        {QStringLiteral("区域"), QStringLiteral("上次运行"), QStringLiteral("下次到期"),
         QStringLiteral("已完成"), QStringLiteral("失败"), QStringLiteral("跳过的节拍"),
         QStringLiteral("最近错误")});
    runtimeTable_->horizontalHeader()->setStretchLastSection(true);
    runtimeTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    runtimeTable_->setMaximumHeight(160);
    root->addWidget(runtimeTable_);

    connect(refreshButton_, &QPushButton::clicked, this, [this] { refreshWindows(); });
    connect(captureButton_, &QPushButton::clicked, this, [this] { captureSnapshot(); });
    connect(windowCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { onWindowChanged(); });
    connect(nameEdit_, &QLineEdit::textEdited, this, [this](const QString&) {
        syncConfigIdentity();
        setDirty(true);
        updateControls();
    });
    connect(startTimeEdit_, &QTimeEdit::timeChanged, this, [this](const QTime&) {
        syncConfigIdentity();
        setDirty(true);
        updateControls();
    });
    connect(endTimeEdit_, &QTimeEdit::timeChanged, this, [this](const QTime&) {
        syncConfigIdentity();
        setDirty(true);
        updateControls();
    });
    connect(openButton_, &QPushButton::clicked, this, [this] { openConfig(); });
    connect(saveButton_, &QPushButton::clicked, this, [this] { saveConfig(); });
    connect(saveAsButton_, &QPushButton::clicked, this, [this] { saveConfigAs(); });
    connect(startButton_, &QPushButton::clicked, this, [this] { startSession(); });
    connect(stopButton_, &QPushButton::clicked, this, [this] { stopSession(); });
    connect(testButton_, &QPushButton::clicked, this, [this] { runSingleRegionTest(); });
    connect(recalibrateButton_, &QPushButton::clicked, this, [this] { recalibrateToLiveSize(); });

    editor_->setRegionsChangedHandler([this] { onEditorRegionsChanged(); });
    editor_->setActiveRegionChangedHandler(
        [this](const QString& regionId) { onActiveRegionChanged(regionId); });
    selector_->setRectEditedHandler([this](const Rect& rect) { onRectEdited(rect); });
}

void RegionWorkspace::refreshWindows() {
    if(controller_.running()) {
        setStatus(QStringLiteral("采集进行中，请先停止采集再刷新窗口列表。"), true);
        return;
    }
    const std::vector<WindowEntry> windows = listCapturableWindows();

    suppressWindowChange_ = true;
    windowCombo_->clear();
    for(const WindowEntry& entry : windows) {
        windowCombo_->addItem(toQString(entry.title),
                               QVariant::fromValue<qulonglong>(entry.handle));
    }
    if(boundWindow_ != 0) {
        const int index = windowCombo_->findData(QVariant::fromValue<qulonglong>(boundWindow_));
        if(index >= 0) {
            windowCombo_->setCurrentIndex(index);
        }
    }
    suppressWindowChange_ = false;

    if(windows.empty()) {
        setStatus(QStringLiteral("没有找到可捕获的窗口。"), true);
    } else {
        setStatus(QStringLiteral("已列出 %1 个可捕获窗口。").arg(windows.size()), false);
    }
    onWindowChanged();
}

void RegionWorkspace::onWindowChanged() {
    if(suppressWindowChange_ || shuttingDown_) {
        return;
    }
    if(controller_.running()) {
        suppressWindowChange_ = true;
        const int boundIndex = windowCombo_->findData(QVariant::fromValue<qulonglong>(boundWindow_));
        windowCombo_->setCurrentIndex(boundIndex);
        suppressWindowChange_ = false;
        setStatus(QStringLiteral("采集进行中，必须先停止采集才能切换目标窗口。"), true);
        return;
    }

    if(windowCombo_->currentIndex() < 0) {
        boundWindow_ = 0;
        boundTitle_.clear();
        liveSize_ = Size{};
        snapshot_ = Image();
        selector_->clearSnapshot();
        config_.targetWindow = WindowIdentityHint{};
        updateSizeCompatibility();
        updateControls();
        clearResultView(QStringLiteral("尚未绑定目标窗口。"));
        return;
    }

    boundWindow_ = windowCombo_->currentData().toULongLong();
    boundTitle_ = windowCombo_->currentText();
    snapshot_ = Image();
    selector_->clearSnapshot();
    liveSize_ = Size{};
    trackers_.clear();
    displayedRegionId_ = editor_->selectedRegionId();
    renderedRegionId_.clear();
    renderedAvailability_ = ResultAvailability::None;

    measureLiveSize();
    updateSizeCompatibility();
    refreshOverlays();
    clearResultView(QStringLiteral("尚未抓取该窗口的客户区快照。"));
    setStatus(QStringLiteral("%1：请先截取客户区快照，再校准区域。").arg(boundTitle_), false);
    updateControls();
}

void RegionWorkspace::captureSnapshot() {
    if(boundWindow_ == 0) {
        setStatus(QStringLiteral("请先选择目标窗口。"), true);
        return;
    }
    ClientSnapshot captured = captureClientSnapshot(boundWindow_);
    if(!captured.success()) {
        setStatus(utf8(captured.message), true);
        if(captured.error == ClientCaptureError::ClientSizeChanged ||
           captured.error == ClientCaptureError::WindowInvalid ||
           captured.error == ClientCaptureError::WindowMinimized) {
            liveSize_ = captured.clientSize;
            updateSizeCompatibility();
        }
        updateControls();
        return;
    }

    liveSize_ = captured.clientSize;
    snapshot_ = captured.image;
    selector_->setSnapshot(snapshot_);

    const bool recorded =
        config_.referenceClientSize.width > 0 && config_.referenceClientSize.height > 0;
    if(!recorded) {
        config_.referenceClientSize = captured.clientSize;
        config_.referenceDpi = 0;
        if(config_.targetWindow.title.empty()) {
            config_.targetWindow.title = boundTitle_.toStdString();
        }
        editor_->setClientSize(config_.referenceClientSize);
        setDirty(true);
        setStatus(QStringLiteral("已记录客户区尺寸 %1 × %2 物理像素。")
                      .arg(captured.clientSize.width)
                      .arg(captured.clientSize.height),
                  false);
    } else if(!clientSizeMatches(config_.referenceClientSize, captured.clientSize)) {
        setStatus(QStringLiteral("当前客户区 %1 × %2 与配置记录的 %3 × %4 物理像素不一致；"
                                 "配置内容已保留，截图、识别与定时采集被阻断。"
                                 "请显式点击“以当前尺寸重校准”并重新核对区域，然后保存。")
                      .arg(captured.clientSize.width)
                      .arg(captured.clientSize.height)
                      .arg(config_.referenceClientSize.width)
                      .arg(config_.referenceClientSize.height),
                  true);
    } else {
        setStatus(QStringLiteral("客户区快照已更新：%1 × %2 物理像素。")
                      .arg(captured.clientSize.width)
                      .arg(captured.clientSize.height),
                  false);
    }

    updateSizeCompatibility();
    refreshOverlays();
    updateControls();
}

void RegionWorkspace::measureLiveSize() {
    if(boundWindow_ == 0) {
        liveSize_ = Size{};
        return;
    }
    Size measured;
    std::string message;
    const ClientCaptureError error =
        measureClientSize(boundWindow_, measured, message);
    if(error == ClientCaptureError::None) {
        liveSize_ = measured;
    } else {
        liveSize_ = Size{};
        setStatus(utf8(message), true);
    }
}

void RegionWorkspace::updateSizeCompatibility() {
    const bool hasReference = config_.referenceClientSize.width > 0 &&
                              config_.referenceClientSize.height > 0;
    const bool hasLive = liveSize_.width > 0 && liveSize_.height > 0;
    if(!hasReference || !hasLive) {
        compatibility_ = SizeCompatibility::Unknown;
    } else if(clientSizeMatches(config_.referenceClientSize, liveSize_)) {
        compatibility_ = SizeCompatibility::Compatible;
    } else {
        compatibility_ = SizeCompatibility::Mismatch;
    }

    if(hasLive) {
        liveSizeLabel_->setText(QStringLiteral("客户区尺寸：%1 × %2 物理像素")
                                    .arg(liveSize_.width)
                                    .arg(liveSize_.height));
    } else {
        liveSizeLabel_->setText(QStringLiteral("客户区尺寸：未测量"));
    }

    if(config_.regions.empty()) {
        config_.regions = editor_->regions();
    }

    switch(compatibility_) {
    case SizeCompatibility::Mismatch:
        compatibilityLabel_->setText(
            QStringLiteral("当前客户区 %1 × %2 与配置记录的 %3 × %4 物理像素不一致。"
                           "配置不会被缩放或裁剪，采集、识别与定时采集均不可用；"
                           "请显式点击“以当前尺寸重校准”，重新设定并保存区域。")
                .arg(liveSize_.width)
                .arg(liveSize_.height)
                .arg(config_.referenceClientSize.width)
                .arg(config_.referenceClientSize.height));
        compatibilityLabel_->show();
        break;
    case SizeCompatibility::Compatible:
        compatibilityLabel_->setText(
            QStringLiteral("配置与当前客户区尺寸一致（%1 × %2 物理像素）。")
                .arg(liveSize_.width)
                .arg(liveSize_.height));
        compatibilityLabel_->show();
        break;
    case SizeCompatibility::Unknown:
        compatibilityLabel_->setText(
            QStringLiteral("尚未确定客户区尺寸：选择窗口并截取一次客户区快照后再校准区域。"));
        compatibilityLabel_->show();
        break;
    }
}

void RegionWorkspace::recalibrateToLiveSize() {
    if(controller_.running()) {
        setStatus(QStringLiteral("采集进行中，请先停止采集再重校准。"), true);
        return;
    }
    if(liveSize_.width <= 0 || liveSize_.height <= 0) {
        measureLiveSize();
    }
    if(liveSize_.width <= 0 || liveSize_.height <= 0) {
        setStatus(QStringLiteral("无法确定当前客户区尺寸：请先选择目标窗口，或截取一次客户区快照。"),
                  true);
        return;
    }
    config_.referenceClientSize = liveSize_;
    config_.referenceDpi = 0;
    editor_->setClientSize(liveSize_);
    setDirty(true);
    updateSizeCompatibility();
    setStatus(QStringLiteral("已把记录尺寸改为当前客户区 %1 × %2 物理像素。"
                             "区域坐标与表格偏移不会被缩放，请逐个核对后再保存。")
                  .arg(liveSize_.width)
                  .arg(liveSize_.height),
              false);
    updateControls();
}

void RegionWorkspace::syncConfigIdentity() {
    config_.name = nameEdit_->text().trimmed().toStdString();
    config_.dailyWindow.start = startTimeEdit_->time().toString(QStringLiteral("HH:mm:ss")).toStdString();
    config_.dailyWindow.end = endTimeEdit_->time().toString(QStringLiteral("HH:mm:ss")).toStdString();
    config_.regions = editor_->regions();
}

void RegionWorkspace::onEditorRegionsChanged() {
    syncConfigIdentity();
    setDirty(true);
    refreshOverlays();
    updateControls();
}

void RegionWorkspace::onActiveRegionChanged(const QString& regionId) {
    displayedRegionId_ = regionId;
    refreshOverlays();
    updateControls();

    if(regionId.isEmpty()) {
        clearResultView(kNoRegionHint);
        return;
    }
    const auto found = trackers_.find(regionId.toStdString());
    if(found == trackers_.end() || !found->second.hasAttempt()) {
        clearResultView(QStringLiteral("选中区域尚未抓取。"));
        return;
    }
    showResult(found->second.lastAttempt(), found->second.availability());
}

void RegionWorkspace::onRectEdited(const Rect& rect) {
    if(controller_.running()) {
        setStatus(QStringLiteral("采集进行中不能修改区域矩形。"), true);
        return;
    }
    if(editor_->currentIndex() < 0) {
        setStatus(QStringLiteral("请先在左侧添加并选中一个区域，再拖拽设定矩形。"), true);
        return;
    }
    if(!editor_->applyRectFromPreview(rect)) {
        setStatus(QStringLiteral("拖出的矩形未通过几何校验（区域需落在客户区 %1 × %2 内且非空）。")
                      .arg(config_.referenceClientSize.width)
                      .arg(config_.referenceClientSize.height),
                  true);
        refreshOverlays();
        return;
    }
    syncConfigIdentity();
    setDirty(true);
    setStatus(QStringLiteral("区域矩形已更新为 %1。").arg(positionText(rect)), false);
    refreshOverlays();
    updateControls();
}

void RegionWorkspace::refreshOverlays() {
    if(selector_ == nullptr) {
        return;
    }
    config_.regions = editor_->regions();
    selector_->setRegions(config_.regions);
    if(displayedRegionId_.isEmpty()) {
        displayedRegionId_ = editor_->selectedRegionId();
    }
    selector_->setActiveRegion(displayedRegionId_);

    const CaptureRegion* region = regionById(displayedRegionId_);
    if(region != nullptr && region->mode == CaptureRegionMode::Table &&
       !region->table.fields.empty()) {
        selector_->setTableOverlay(region->rect, region->table, true);
    } else {
        selector_->clearTableOverlay();
    }
}

bool RegionWorkspace::confirmDiscardChanges() {
    if(!dirty_) {
        return true;
    }
    const QMessageBox::StandardButton answer =
        QMessageBox::warning(this, QStringLiteral("未保存的配置"),
                             QStringLiteral("当前配置有未保存的修改。要保存后再继续吗？"),
                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                             QMessageBox::Save);
    if(answer == QMessageBox::Cancel) {
        return false;
    }
    if(answer == QMessageBox::Save) {
        saveConfig();
        return !dirty_;
    }
    return true;
}

void RegionWorkspace::openConfig() {
    if(controller_.running()) {
        setStatus(QStringLiteral("采集进行中，请先停止采集再打开配置。"), true);
        return;
    }
    if(!confirmDiscardChanges()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开区域识别配置"), QString(),
        QStringLiteral("区域识别配置 (*.json);;所有文件 (*.*)"));
    if(path.isEmpty()) {
        return;
    }
    const RegionConfigParseResult parsed = readRegionConfigFile(path.toStdString());
    if(!parsed.ok) {
        setStatus(QStringLiteral("配置读取失败：%1：%2")
                      .arg(utf8(parsed.validation.path))
                      .arg(utf8(parsed.validation.message)),
                  true);
        return;
    }
    applyLoadedConfig(parsed.config, path);
    setStatus(QStringLiteral("已加载配置 %1。").arg(QFileInfo(path).fileName()), false);
}

void RegionWorkspace::saveConfig() {
    if(configPath_.isEmpty()) {
        saveConfigAs();
        return;
    }
    if(writeConfigFile(configPath_)) {
        setDirty(false);
        setStatus(QStringLiteral("已保存配置 %1。").arg(QFileInfo(configPath_).fileName()), false);
    }
}

void RegionWorkspace::saveConfigAs() {
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("保存区域识别配置"),
        configPath_.isEmpty() ? QStringLiteral("region-capture.json") : configPath_,
        QStringLiteral("区域识别配置 (*.json);;所有文件 (*.*)"));
    if(path.isEmpty()) {
        return;
    }
    if(QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".json");
    }
    if(writeConfigFile(path)) {
        configPath_ = path;
        setDirty(false);
        setStatus(QStringLiteral("已保存配置 %1。").arg(QFileInfo(path).fileName()), false);
    }
}

bool RegionWorkspace::writeConfigFile(const QString& path) {
    syncConfigIdentity();
    const ConfigValidation validation = validate(config_);
    if(!validation.valid) {
        setStatus(QStringLiteral("配置校验失败：%1：%2")
                      .arg(utf8(validation.path))
                      .arg(utf8(validation.message)),
                  true);
        return false;
    }
    if(compatibility_ == SizeCompatibility::Mismatch) {
        setStatus(QStringLiteral("当前客户区尺寸与配置记录不一致，保存前请先重校准并核对区域。"),
                  true);
        return false;
    }

    const std::string text = writeRegionConfigJson(config_);
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatus(QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()), true);
        return false;
    }
    if(file.write(text.c_str(), static_cast<qint64>(text.size())) != static_cast<qint64>(text.size())) {
        setStatus(QStringLiteral("写入 %1 不完整：%2").arg(path, file.errorString()), true);
        file.cancelWriting();
        return false;
    }
    if(!file.commit()) {
        setStatus(QStringLiteral("提交 %1 失败：%2").arg(path, file.errorString()), true);
        return false;
    }
    return true;
}

void RegionWorkspace::applyLoadedConfig(const RegionCaptureConfig& config, const QString& path) {
    config_ = config;
    configPath_ = path;

    suppressWindowChange_ = true;
    nameEdit_->setText(utf8(config_.name));
    startTimeEdit_->setTime(QTime::fromString(utf8(config_.dailyWindow.start),
                                              QStringLiteral("HH:mm:ss")));
    endTimeEdit_->setTime(QTime::fromString(utf8(config_.dailyWindow.end),
                                            QStringLiteral("HH:mm:ss")));
    suppressWindowChange_ = false;

    editor_->setClientSize(config_.referenceClientSize);
    editor_->setRegions(config_.regions);
    trackers_.clear();
    displayedRegionId_ = editor_->selectedRegionId();

    if(!config_.targetWindow.title.empty()) {
        const int index = windowCombo_->findText(utf8(config_.targetWindow.title));
        if(index >= 0) {
            suppressWindowChange_ = true;
            windowCombo_->setCurrentIndex(index);
            suppressWindowChange_ = false;
            boundWindow_ = windowCombo_->currentData().toULongLong();
            boundTitle_ = windowCombo_->currentText();
        }
    }
    if(liveSize_.width <= 0 || liveSize_.height <= 0) {
        measureLiveSize();
    }

    updateSizeCompatibility();
    refreshOverlays();
    clearResultView(QStringLiteral("配置已加载，尚未抓取。"));
    setDirty(false);
    updateControls();
}

bool RegionWorkspace::canStartSession(QString& reason) const {
    if(controller_.running()) {
        reason = QStringLiteral("采集已在进行中。");
        return false;
    }
    if(boundWindow_ == 0) {
        reason = QStringLiteral("请先选择并绑定目标窗口。");
        return false;
    }
    if(config_.regions.empty()) {
        reason = QStringLiteral("请至少配置一个区域。");
        return false;
    }
    const ConfigValidation validation = validate(config_);
    if(!validation.valid) {
        reason = QStringLiteral("配置校验失败：%1：%2")
                     .arg(utf8(validation.path))
                     .arg(utf8(validation.message));
        return false;
    }
    if(compatibility_ != SizeCompatibility::Compatible) {
        reason = QStringLiteral("客户区尺寸与配置记录不一致，请重新捕获并重校准后再开始。");
        return false;
    }
    return true;
}

void RegionWorkspace::startSession() {
    syncConfigIdentity();
    config_.regions = editor_->regions();

    QString reason;
    if(!canStartSession(reason)) {
        setStatus(reason, true);
        updateControls();
        return;
    }
    if(dirty_) {
        setStatus(QStringLiteral("提示：当前配置尚未保存，采集将使用内存中的配置。"), false);
    }

    int tickIntervalMs = 0;
    for(const CaptureRegion& region : config_.regions) {
        if(!region.enabled) {
            continue;
        }
        if(tickIntervalMs == 0 || region.intervalMs < tickIntervalMs) {
            tickIntervalMs = region.intervalMs;
        }
    }
    tickIntervalMs = std::max(tickIntervalMs, kMinRegionIntervalMs);

    const float lowConfidenceThreshold =
        static_cast<float>(lowConfidenceSpin_->value()) / 100.0F;

    trackers_.clear();
    clearResultView(QStringLiteral("采集已开始，等待首个节拍。"));
    if(!controller_.start(config_, boundWindow_, fontColor_, lowConfidenceThreshold,
                          tickIntervalMs)) {
        setStatus(QStringLiteral("无法启动采集，请检查目标窗口是否仍然有效。"), true);
        updateControls();
        return;
    }
    setStatus(QStringLiteral("采集已开始，共 %1 个区域。").arg(config_.regions.size()), false);
    updateControls();
}

void RegionWorkspace::stopSession() {
    controller_.stop();
    setStatus(QStringLiteral("已请求停止采集，正在等待进行中的任务结束。"), false);
    updateControls();
}

void RegionWorkspace::runSingleRegionTest() {
    if(controller_.running()) {
        setStatus(QStringLiteral("采集进行中，单区域测试请先停止采集，避免与调度相互干扰。"), true);
        return;
    }
    const CaptureRegion* region = regionById(displayedRegionId_);
    if(region == nullptr) {
        setStatus(QStringLiteral("请先在左侧选择一个区域再做单区域测试。"), true);
        return;
    }
    if(boundWindow_ == 0) {
        setStatus(QStringLiteral("请先选择并绑定目标窗口。"), true);
        return;
    }
    if(compatibility_ != SizeCompatibility::Compatible) {
        setStatus(QStringLiteral("客户区尺寸与配置记录不一致，单区域测试已阻断。"), true);
        return;
    }

    RegionScanRequest request;
    request.windowHandle = boundWindow_;
    ++singleTestToken_;
    request.sessionId = "single-" + region->id + "-" + std::to_string(singleTestToken_);
    request.region = *region;
    request.expectedClientSize = config_.referenceClientSize;
    request.fontColorConfig = fontColor_;
    request.lowConfidenceThreshold =
        static_cast<float>(lowConfidenceSpin_->value()) / 100.0F;

    setStatus(QStringLiteral("正在对区域“%1”执行单次抓取识别。").arg(utf8(region->name)), false);
    controller_.runSingle(request, [this](const RegionCaptureResult& result) {
        onResult(result);
        if(result.succeeded()) {
            setStatus(QStringLiteral("单区域测试完成：%1").arg(utf8(result.regionId)), false);
        } else {
            setStatus(QStringLiteral("单区域测试失败：%1：%2")
                          .arg(utf8(toString(result.captureStatus)))
                          .arg(utf8(result.message)),
                      true);
        }
    });
    updateControls();
}

void RegionWorkspace::onSnapshot(const RegionSessionSnapshot& snapshot) {
    sessionSnapshot_ = snapshot;
    refreshSessionLabel(sessionSnapshot_);
    refreshRuntimeTable(sessionSnapshot_);
    updateControls();
}

void RegionWorkspace::onResult(const RegionCaptureResult& result) {
    RegionResultTracker& tracker = trackers_[result.regionId];
    tracker.record(result);

    const QString regionId = utf8(result.regionId);
    if(displayedRegionId_.isEmpty() || displayedRegionId_ == regionId) {
        if(result.succeeded()) {
            showResult(result, ResultAvailability::CurrentSuccess);
        } else if(tracker.hasSuccess()) {
            showResult(result, ResultAvailability::FailureWithStaleSuccess);
        } else {
            showResult(result, ResultAvailability::CurrentFailure);
        }
    }
    refreshRuntimeTable(sessionSnapshot_);
}

void RegionWorkspace::showResult(const RegionCaptureResult& result, ResultAvailability availability) {
    renderedRegionId_ = utf8(result.regionId);
    renderedAvailability_ = availability;

    const CaptureRegion* region = regionById(utf8(result.regionId));
    resultTitle_->setText(region != nullptr
                              ? QStringLiteral("区域结果：%1").arg(utf8(region->name))
                              : QStringLiteral("区域结果：%1").arg(utf8(result.regionId)));

    QString statusText = availabilityText(availability);
    if(!result.succeeded()) {
        statusText += QStringLiteral("　抓取：%1　识别：%2")
                          .arg(utf8(toString(result.captureStatus)))
                          .arg(utf8(toString(result.ocrStatus)));
        if(!result.message.empty()) {
            statusText += QStringLiteral("　原因：") + utf8(result.message);
        }
    }
    resultStatus_->setText(statusText);
    resultStatus_->setStyleSheet(result.succeeded()
                                     ? QStringLiteral("color:#1b5e20;font-weight:bold;")
                                     : QStringLiteral("color:#b00020;font-weight:bold;"));

    const RegionCaptureResult* shown = &result;
    if(availability == ResultAvailability::FailureWithStaleSuccess) {
        const auto found = trackers_.find(result.regionId);
        if(found != trackers_.end() && found->second.hasSuccess()) {
            shown = &found->second.lastSuccess();
        }
    }

    QString metaText = QStringLiteral("采集时间：%1　客户区：%2 × %3 px")
                           .arg(utf8(shown->capturedAt))
                           .arg(shown->clientSize.width)
                           .arg(shown->clientSize.height);
    if(shown->imageSize.width > 0 && shown->imageSize.height > 0) {
        metaText += QStringLiteral("　截图：%1 × %2 px")
                        .arg(shown->imageSize.width)
                        .arg(shown->imageSize.height);
    }
    if(shown->skippedTicks > 0) {
        metaText += QStringLiteral("　跳过节拍：%1")
                        .arg(static_cast<qulonglong>(shown->skippedTicks));
    }
    resultMeta_->setText(metaText);

    if(shown->image.valid()) {
        QSize target = resultImage_->size();
        if(target.width() < 32 || target.height() < 32) {
            target = QSize(320, 120);
        }
        const QImage converted = toQImage(shown->image);
        resultImage_->setPixmap(
            QPixmap::fromImage(converted).scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        resultImage_->setPixmap(QPixmap());
        resultImage_->setText(QStringLiteral("无预览图像（运行期不保存截图）"));
    }

    const auto fieldName = [region](const std::string& fieldId) -> QString {
        if(region != nullptr) {
            for(const FieldDefinition& definition : region->table.fields) {
                if(definition.id == fieldId) {
                    return utf8(definition.name);
                }
            }
        }
        return utf8(fieldId);
    };

    fieldTable_->clearContents();
    if(!shown->rows.empty()) {
        std::size_t total = 0;
        for(const TableRowResult& tableRow : shown->rows) {
            total += tableRow.fields.size();
        }
        fieldTable_->setRowCount(static_cast<int>(total));
        int row = 0;
        for(const TableRowResult& tableRow : shown->rows) {
            for(const TypedFieldValue& field : tableRow.fields) {
                fieldTable_->setItem(
                    row, 0, new QTableWidgetItem(QString::number(tableRow.rowIndex + 1)));
                fieldTable_->setItem(row, 1, new QTableWidgetItem(fieldName(field.fieldId)));
                fieldTable_->setItem(row, 2, new QTableWidgetItem(utf8(field.rawText)));
                fieldTable_->setItem(row, 3, new QTableWidgetItem(valueText(field)));
                fieldTable_->setItem(
                    row, 4,
                    new QTableWidgetItem(field.hasConfidence
                                             ? QString::number(field.ocrConfidence, 'f', 3)
                                             : QStringLiteral("—")));
                fieldTable_->setItem(row, 5, new QTableWidgetItem(fieldStatusText(field)));
                ++row;
            }
        }
    } else if(!shown->blocks.empty()) {
        fieldTable_->setRowCount(static_cast<int>(shown->blocks.size()));
        int row = 0;
        for(const OcrTextBlock& block : shown->blocks) {
            fieldTable_->setItem(
                row, 0,
                new QTableWidgetItem(QString::number(static_cast<qulonglong>(block.order) + 1)));
            fieldTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("识别文本")));
            fieldTable_->setItem(row, 2, new QTableWidgetItem(utf8(block.text)));
            fieldTable_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("—")));
            fieldTable_->setItem(row, 4,
                                 new QTableWidgetItem(QString::number(block.recognitionConfidence, 'f', 3)));
            fieldTable_->setItem(row, 5, new QTableWidgetItem(QStringLiteral("—")));
            ++row;
        }
    } else {
        fieldTable_->setRowCount(0);
    }

    QStringList rawLines;
    if(availability == ResultAvailability::FailureWithStaleSuccess) {
        rawLines << QStringLiteral("（本次失败，以下为上一次成功结果）");
    }
    for(const OcrTextBlock& block : shown->blocks) {
        rawLines << QStringLiteral("[%1] %2")
                        .arg(static_cast<qulonglong>(block.order) + 1)
                        .arg(utf8(block.text));
    }
    rawText_->setPlainText(rawLines.join(QLatin1Char('\n')));

    updateControls();
}

void RegionWorkspace::showStaleSuccess(const RegionCaptureResult& result) {
    showResult(result, ResultAvailability::FailureWithStaleSuccess);
}

void RegionWorkspace::clearResultView(const QString& message) {
    renderedRegionId_.clear();
    renderedAvailability_ = ResultAvailability::None;
    resultTitle_->setText(QStringLiteral("区域结果"));
    resultStatus_->setText(message);
    resultStatus_->setStyleSheet(QStringLiteral("color:#555;"));
    resultMeta_->clear();
    resultImage_->setPixmap(QPixmap());
    resultImage_->setText(QStringLiteral("暂无预览"));
    fieldTable_->clearContents();
    fieldTable_->setRowCount(0);
    rawText_->clear();
}

void RegionWorkspace::refreshRuntimeTable(const RegionSessionSnapshot& snapshot) {
    runtimeTable_->clearContents();
    runtimeTable_->setRowCount(static_cast<int>(config_.regions.size()));
    int row = 0;
    for(const CaptureRegion& region : config_.regions) {
        const RegionRuntimeStatus* status = nullptr;
        for(const RegionRuntimeStatus& candidate : snapshot.statuses) {
            if(candidate.regionId == region.id) {
                status = &candidate;
                break;
            }
        }
        runtimeTable_->setItem(row, 0, new QTableWidgetItem(utf8(region.name)));
        runtimeTable_->setItem(
            row, 1,
            new QTableWidgetItem(status != nullptr && status->hasAttempt
                                     ? clockFromMs(status->lastAttemptMs)
                                     : QStringLiteral("—")));
        runtimeTable_->setItem(
            row, 2,
            new QTableWidgetItem(status != nullptr ? clockFromMs(status->nextDueMs)
                                                   : QStringLiteral("—")));
        runtimeTable_->setItem(
            row, 3,
            new QTableWidgetItem(status != nullptr
                                     ? QString::number(static_cast<qulonglong>(status->completedRuns))
                                     : QStringLiteral("0")));
        runtimeTable_->setItem(
            row, 4,
            new QTableWidgetItem(status != nullptr
                                     ? QString::number(static_cast<qulonglong>(status->failedRuns))
                                     : QStringLiteral("0")));
        runtimeTable_->setItem(
            row, 5,
            new QTableWidgetItem(status != nullptr
                                     ? QString::number(static_cast<qulonglong>(status->skippedTicks))
                                     : QStringLiteral("0")));
        runtimeTable_->setItem(row, 6,
                               new QTableWidgetItem(status != nullptr ? utf8(status->lastError)
                                                                      : QString()));
        ++row;
    }
}

void RegionWorkspace::refreshSessionLabel(const RegionSessionSnapshot& snapshot) {
    QString stateText;
    switch(snapshot.state) {
    case SessionState::WaitingForDailyStart:
        stateText = QStringLiteral("等待每日开始");
        break;
    case SessionState::Running:
        stateText = QStringLiteral("运行中");
        break;
    case SessionState::Ended:
        stateText = QStringLiteral("已结束");
        break;
    case SessionState::Failed:
        stateText = QStringLiteral("失败");
        break;
    case SessionState::Stopped:
        stateText = QStringLiteral("已停止");
        break;
    }
    QString text = QStringLiteral("采集：%1").arg(stateText);
    if(snapshot.inFlight > 0) {
        text += QStringLiteral("　进行中：%1").arg(snapshot.inFlight);
    }
    if(snapshot.skippedTicks > 0) {
        text += QStringLiteral("　跳过节拍：%1").arg(snapshot.skippedTicks);
    }
    sessionLabel_->setText(text);
}

void RegionWorkspace::updateControls() {
    const bool running = controller_.running();
    const bool hasWindow = boundWindow_ != 0;
    const bool compatible = compatibility_ == SizeCompatibility::Compatible;
    const bool hasRegions = !editor_->regions().empty();
    const bool hasSelection = !editor_->selectedRegionId().isEmpty();

    refreshButton_->setEnabled(!running);
    captureButton_->setEnabled(hasWindow && !running);
    windowCombo_->setEnabled(!running);
    openButton_->setEnabled(!running);
    saveButton_->setEnabled(!running);
    saveAsButton_->setEnabled(!running);
    startButton_->setEnabled(!running && hasWindow && compatible && hasRegions);
    stopButton_->setEnabled(running);
    testButton_->setEnabled(!running && hasWindow && compatible && hasSelection);
    recalibrateButton_->setEnabled(hasWindow && !running);
    nameEdit_->setEnabled(!running);
    startTimeEdit_->setEnabled(!running);
    endTimeEdit_->setEnabled(!running);
    lowConfidenceSpin_->setEnabled(!running);
    editor_->setEditingEnabled(!running);
    selector_->setEditingEnabled(!running);
}

void RegionWorkspace::setDirty(bool dirty) {
    dirty_ = dirty;
    dirtyLabel_->setText(dirty ? QStringLiteral("有未保存的修改") : QStringLiteral("未修改"));
    dirtyLabel_->setStyleSheet(dirty ? QStringLiteral("color:#b00020;font-weight:bold;")
                                     : QStringLiteral("color:#555;"));
}

void RegionWorkspace::setStatus(const QString& message, bool error) {
    status_->setText(message);
    status_->setStyleSheet(error ? QStringLiteral("color:#b00020; background:#fdecea; padding:6px;")
                                 : QStringLiteral("color:#333; background:#f2f2f2; padding:6px;"));
}

int RegionWorkspace::regionIndexById(const QString& regionId) const {
    if(regionId.isEmpty()) {
        return -1;
    }
    for(std::size_t index = 0; index < config_.regions.size(); ++index) {
        if(utf8(config_.regions[index].id) == regionId) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

const CaptureRegion* RegionWorkspace::regionById(const QString& regionId) const {
    if(regionId.isEmpty()) {
        return nullptr;
    }
    for(const CaptureRegion& region : config_.regions) {
        if(utf8(region.id) == regionId) {
            return &region;
        }
    }
    return nullptr;
}

const RegionRuntimeStatus* RegionWorkspace::runtimeStatusById(const QString& regionId) const {
    if(regionId.isEmpty()) {
        return nullptr;
    }
    for(const RegionRuntimeStatus& status : sessionSnapshot_.statuses) {
        if(utf8(status.regionId) == regionId) {
            return &status;
        }
    }
    return nullptr;
}

}  // namespace stock_monitor
