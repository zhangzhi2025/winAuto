#include "StockMonitorWindow.h"

#include <QComboBox>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStringList>
#include <QStatusBar>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <cstring>

#include "StockWindowCapture.h"
#ifdef STOCK_MONITOR_HAS_RAPIDOCR
#include <QtConcurrent/QtConcurrentRun>

#include "RapidOcrAdapter.h"
#endif

namespace stock_monitor {
namespace {

QString toQString(const std::wstring& text) {
    return QString::fromWCharArray(text.data(), static_cast<int>(text.size()));
}

QImage toQImage(const Image& image) {
    QImage result(image.width, image.height, QImage::Format_RGBA8888);
    for (int y = 0; y < image.height; ++y) {
        std::memcpy(result.scanLine(y),
                    image.rgba.data() + static_cast<std::size_t>(y) * image.stride,
                    static_cast<std::size_t>(image.width) * 4U);
    }
    return result;
}

Image fromQImage(const QImage& source) {
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
    return image;
}

}  // namespace

StockMonitorWindow::StockMonitorWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("WinAuto Stock Monitor"));
    resize(1000, 720);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    auto* controls = new QHBoxLayout();
    importButton_ = new QPushButton(QStringLiteral("导入截图"), central);
    auto* refreshButton = new QPushButton(QStringLiteral("刷新窗口列表"), central);
    captureButton_ = new QPushButton(QStringLiteral("捕获可见客户区"), central);
    analyzeButton_ = new QPushButton(QStringLiteral("分析图像"), central);
    windows_ = new QComboBox(central);
    controls->addWidget(importButton_);
    controls->addWidget(windows_, 1);
    controls->addWidget(refreshButton);
    controls->addWidget(captureButton_);
    controls->addWidget(analyzeButton_);
    layout->addLayout(controls);

#ifdef STOCK_MONITOR_HAS_RAPIDOCR
    ocrRuntime_ = std::make_shared<OcrRuntimeState>();
    dependency_ = new QLabel(QStringLiteral("本地 RapidOcrOnnx 文字框识别已构建；导入或捕获图像后可手动分析。"), central);
#else
    dependency_ = new QLabel(
        QStringLiteral("已提供 RapidOcrOnnx Win32 CLIB DLL 和 OCR 模型，可用于普通文本识别；但其 C API 只返回拼接文本，"
                       "不提供逐段文字框坐标和置信度，无法支持看盘所需的股票行关联及字体颜色采样。"
                       "看盘目前尚未接入可返回 TextBlock 的接口；仓库中的 C++ 源码构建路径还缺少匹配的 Win32 "
                       "OpenCV / ONNX Runtime 开发包。不会用拼接文本猜测坐标。"),
        central);
#endif
    dependency_->setWordWrap(true);
    dependency_->setStyleSheet(QStringLiteral("QLabel { color: #8a4b08; background: #fff3cd; padding: 8px; }"));
    layout->addWidget(dependency_);

    status_ = new QLabel(QStringLiteral("请选择截图文件或窗口。"), central);
    layout->addWidget(status_);
    results_ = new QLabel(QStringLiteral("尚无分析结果。"), central);
    results_->setWordWrap(true);
    layout->addWidget(results_);
    auto* scroll = new QScrollArea(central);
    scroll->setWidgetResizable(true);
    preview_ = new QLabel(QStringLiteral("图像预览"), scroll);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(640, 360);
    scroll->setWidget(preview_);
    layout->addWidget(scroll, 1);
    setCentralWidget(central);

    connect(importButton_, &QPushButton::clicked, this, [this] { loadImage(); });
    connect(refreshButton, &QPushButton::clicked, this, [this] { refreshWindows(); });
    connect(captureButton_, &QPushButton::clicked, this, [this] { captureWindow(); });
    connect(analyzeButton_, &QPushButton::clicked, this, [this] { analyzeImage(); });
    refreshWindows();
}

void StockMonitorWindow::closeEvent(QCloseEvent* event) {
    if (cancellation_) cancellation_->store(true);
    ++analysisGeneration_;
    QMainWindow::closeEvent(event);
}

void StockMonitorWindow::refreshWindows() {
    windows_->clear();
    const auto entries = listCapturableWindows();
    for (const auto& entry : entries) {
        windows_->addItem(toQString(entry.title), QVariant::fromValue<qulonglong>(entry.handle));
    }
    if (entries.empty()) status_->setText(QStringLiteral("没有找到可捕获的可见窗口。"));
}

void StockMonitorWindow::loadImage() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入截图"), QString(),
        QStringLiteral("图像文件 (*.png *.jpg *.jpeg *.bmp *.tif *.tiff)"));
    if (path.isEmpty()) return;

    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QImage loaded = reader.read();
    if (loaded.isNull()) {
        status_->setText(QStringLiteral("截图加载失败：%1").arg(reader.errorString()));
        return;
    }
    const Image image = fromQImage(loaded);
    if (!image.valid()) {
        status_->setText(QStringLiteral("截图无效或尺寸超出支持范围。"));
        return;
    }
    currentImage_ = image;
    displayImage(currentImage_, QStringLiteral("文件：%1").arg(path));
    results_->setText(QStringLiteral("图像已载入，尚未分析。"));
    analyzeButton_->setEnabled(true);
}

void StockMonitorWindow::captureWindow() {
    if (windows_->currentIndex() < 0) {
        status_->setText(QStringLiteral("请先刷新并选择一个窗口。"));
        return;
    }
    const auto handle = windows_->currentData().toULongLong();
    const auto captured = captureVisibleClientArea(handle);
    if (!captured.success) {
        status_->setText(QStringLiteral("窗口截图失败：%1").arg(QString::fromStdString(captured.error)));
        return;
    }
    currentImage_ = captured.image;
    displayImage(currentImage_, QStringLiteral("窗口：%1").arg(windows_->currentText()));
    results_->setText(QStringLiteral("图像已捕获，尚未分析。"));
    analyzeButton_->setEnabled(true);
}

void StockMonitorWindow::analyzeImage() {
    if (!currentImage_.valid()) {
        status_->setText(QStringLiteral("请先导入截图或捕获窗口。"));
        return;
    }
    if (analysisWatcher_) return;
#ifndef STOCK_MONITOR_HAS_RAPIDOCR
    status_->setText(QStringLiteral("分析未启动：当前构建没有逐文字框 OCR 运行库。"));
    return;
#else
    const Image image = currentImage_;
    const StockMonitorConfig config = config_;
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    cancellation_ = cancel;
    const auto runtime = ocrRuntime_;
    const std::uint64_t generation = ++analysisGeneration_;
    auto* watcher = new QFutureWatcher<AnalysisResult>(this);
    analysisWatcher_ = watcher;
    analyzeButton_->setEnabled(false);
    importButton_->setEnabled(false);
    captureButton_->setEnabled(false);
    status_->setText(QStringLiteral("正在本地执行 OCR 和颜色分析…"));
    results_->setText(QStringLiteral("分析中。"));

    connect(watcher, &QFutureWatcher<AnalysisResult>::finished, this, [this, watcher, generation, cancel] {
        const AnalysisResult result = watcher->result();
        watcher->deleteLater();
        if (analysisWatcher_ == watcher) analysisWatcher_ = nullptr;
        if (generation != analysisGeneration_ || cancel->load()) return;
        cancellation_.reset();
        analyzeButton_->setEnabled(true);
        importButton_->setEnabled(true);
        captureButton_->setEnabled(true);
        if (result.error != AnalysisError::None) {
            status_->setText(QStringLiteral("分析失败 [%1]：%2")
                                 .arg(QString::fromLatin1(toString(result.error)),
                                      QString::fromStdString(result.message)));
            return;
        }
        status_->setText(QStringLiteral("分析完成：OCR 文字框 %1 个，股票行 %2 行。")
                             .arg(result.blocks.size()).arg(result.rows.size()));
        QStringList lines;
        for (const auto& row : result.rows) {
            const QString code = row.codeBlocks.empty()
                                     ? QStringLiteral("（无代码）")
                                     : QString::fromUtf8(row.codeBlocks.front().text.c_str());
            const QString name = row.nameBlocks.empty()
                                     ? QStringLiteral("（无名称）")
                                     : QString::fromUtf8(row.nameBlocks.front().text.c_str());
            lines.push_back(QStringLiteral("%1  %2 | 颜色：%3 | %4")
                                .arg(code, name,
                                     QString::fromLatin1(toString(row.nameColor.color)),
                                     QString::fromLatin1(toString(row.status))));
        }
        results_->setText(lines.isEmpty() ? QStringLiteral("识别成功，但没有可显示的股票行。")
                                          : lines.join(QLatin1Char('\n')));
    });

    watcher->setFuture(QtConcurrent::run([image, config, cancel, runtime] {
        std::shared_ptr<RapidOcrAdapter> provider;
        {
            std::lock_guard<std::mutex> lock(runtime->mutex);
            if (!runtime->provider) runtime->provider = std::make_shared<RapidOcrAdapter>();
            provider = runtime->provider;
        }
        return analyzeWithProvider(image, config, *provider, *cancel);
    }));
#endif
}

void StockMonitorWindow::displayImage(const Image& image, const QString& source) {
    if (!image.valid()) return;
    preview_->setPixmap(QPixmap::fromImage(toQImage(image)).scaled(
        preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    status_->setText(QStringLiteral("%1 — %2 × %3 px").arg(source).arg(image.width).arg(image.height));
    statusBar()->showMessage(QStringLiteral("图像已在内存中载入；未自动保存。"));
}

}  // namespace stock_monitor
