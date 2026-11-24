#include "RegionSelector.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <utility>

#include "UiImageUtils.h"

namespace stock_monitor {

RegionSelector::RegionSelector(QWidget* parent) : QWidget(parent) {
    setMinimumSize(320, 200);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(true);
}

void RegionSelector::setSnapshot(const Image& clientImage) {
    snapshot_ = toQImage(clientImage);
    sourceSize_ = clientImage.valid() ? Size {clientImage.width, clientImage.height} : Size {};
    update();
}

void RegionSelector::clearSnapshot() {
    snapshot_ = QImage();
    sourceSize_ = Size {};
    regions_.clear();
    activeRegionId_.clear();
    clearTableOverlay();
    update();
}

void RegionSelector::setRegions(const std::vector<CaptureRegion>& regions) {
    regions_.clear();
    regions_.reserve(regions.size());
    for (const auto& region : regions) {
        OverlayRegion overlay;
        overlay.id = toQString(region.id);
        overlay.name = toQString(region.name);
        overlay.rect = region.rect;
        regions_.push_back(std::move(overlay));
    }
    update();
}

void RegionSelector::setActiveRegion(const QString& regionId) {
    activeRegionId_ = regionId;
    update();
}

void RegionSelector::setTableOverlay(const Rect& regionRect,
                                     const TableTemplate& table,
                                     bool enabled) {
    overlayRegionRect_ = regionRect;
    overlayTable_ = table;
    hasTableOverlay_ = enabled;
    update();
}

void RegionSelector::clearTableOverlay() {
    hasTableOverlay_ = false;
    overlayTable_ = TableTemplate {};
    update();
}

void RegionSelector::setEditingEnabled(bool enabled) {
    editingEnabled_ = enabled;
    if (!enabled) {
        dragging_ = false;
        hasDragArea_ = false;
    }
    update();
}

void RegionSelector::setRectEditedHandler(std::function<void(const Rect&)> handler) {
    rectEditedHandler_ = std::move(handler);
}

QSize RegionSelector::sizeHint() const {
    if (snapshot_.isNull()) return QSize(640, 400);
    return snapshot_.size();
}

QSize RegionSelector::minimumSizeHint() const {
    return QSize(320, 200);
}

PreviewTransform RegionSelector::transform() const {
    return computePreviewTransform(sourceSize_, Size {width(), height()});
}

QPoint RegionSelector::toDisplay(const PreviewTransform& current, const Point& source) const {
    return QPoint(
        current.offsetX + static_cast<int>(std::lround(static_cast<double>(source.x) * current.scale)),
        current.offsetY + static_cast<int>(std::lround(static_cast<double>(source.y) * current.scale)));
}

void RegionSelector::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), QColor(32, 32, 32));
    const PreviewTransform current = transform();
    if (!current.valid()) {
        painter.setPen(QColor(224, 224, 224));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("先捕获目标窗口客户区，然后在预览上拖动鼠标选择区域。"));
        return;
    }
    const QRect imageRect(current.offsetX, current.offsetY, current.scaledWidth(),
                          current.scaledHeight());
    painter.drawImage(imageRect, snapshot_);
    painter.setPen(QPen(QColor(96, 96, 96), 1));
    painter.drawRect(imageRect.adjusted(0, 0, -1, -1));
    drawRegions(painter, current);
    drawTableOverlay(painter, current);
    drawRubberBand(painter, current);
}

void RegionSelector::drawRegions(QPainter& painter, const PreviewTransform& current) const {
    for (const auto& region : regions_) {
        const QPoint topLeft = toDisplay(current, Point {region.rect.x, region.rect.y});
        const QRect box(topLeft,
                        toDisplay(current, Point {region.rect.x + region.rect.width,
                                                  region.rect.y + region.rect.height}));
        const bool active = !activeRegionId_.isEmpty() && region.id == activeRegionId_;
        const QColor color = active ? QColor(255, 152, 0) : QColor(33, 150, 243);
        painter.setPen(QPen(color, active ? 2 : 1));
        QColor fill = color;
        fill.setAlpha(active ? 48 : 28);
        painter.fillRect(box.normalized(), fill);
        painter.drawRect(box.normalized());
        if (active) {
            painter.drawText(box.normalized().adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignLeft,
                             region.name);
        }
    }
}

void RegionSelector::drawTableOverlay(QPainter& painter, const PreviewTransform& current) const {
    if (!hasTableOverlay_ || overlayTable_.fields.empty()) return;
    const int regionWidth = overlayRegionRect_.width;
    const int regionHeight = overlayRegionRect_.height;
    if (regionWidth <= 0 || regionHeight <= 0) return;

    painter.setPen(QPen(QColor(255, 235, 59, 200), 1, Qt::DashLine));
    for (const auto& field : overlayTable_.fields) {
        const QPoint top =
            toDisplay(current, Point {overlayRegionRect_.x + field.x, overlayRegionRect_.y});
        const QPoint bottom = toDisplay(current, Point {overlayRegionRect_.x + field.x,
                                                        overlayRegionRect_.y + regionHeight});
        painter.drawLine(top, bottom);
        painter.drawText(top + QPoint(2, 12), toQString(field.name));
    }

    if (overlayTable_.rowPitch <= 0 || overlayTable_.rowHeight <= 0) return;
    painter.setPen(QPen(QColor(255, 235, 59, 120), 1, Qt::DotLine));
    int y = overlayTable_.firstRowY;
    for (int guard = 0; guard < 200 && y < regionHeight; ++guard) {
        const QPoint left = toDisplay(current, Point {overlayRegionRect_.x, overlayRegionRect_.y + y});
        const QPoint right =
            toDisplay(current, Point {overlayRegionRect_.x + regionWidth, overlayRegionRect_.y + y});
        painter.drawLine(left, right);
        y += overlayTable_.rowPitch;
    }
}

void RegionSelector::drawRubberBand(QPainter& painter, const PreviewTransform& current) const {
    if (!dragging_ || !hasDragArea_) return;
    const QRect imageRect(current.offsetX, current.offsetY, current.scaledWidth(),
                          current.scaledHeight());
    const QPoint start(std::clamp(pressDisplay_.x(), imageRect.left(), imageRect.right()),
                       std::clamp(pressDisplay_.y(), imageRect.top(), imageRect.bottom()));
    const QPoint end(std::clamp(currentDisplay_.x(), imageRect.left(), imageRect.right()),
                     std::clamp(currentDisplay_.y(), imageRect.top(), imageRect.bottom()));
    const QRect box = QRect(start, end).normalized();
    painter.setPen(QPen(QColor(0, 230, 118), 2));
    painter.fillRect(box, QColor(0, 230, 118, 40));
    painter.drawRect(box);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(box.adjusted(4, 2, -4, -2), Qt::AlignTop | Qt::AlignRight,
                     QStringLiteral("%1×%2").arg(box.width()).arg(box.height()));
}

void RegionSelector::mousePressEvent(QMouseEvent* event) {
    if (!editingEnabled_ || !hasSnapshot() || event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const PreviewTransform current = transform();
    if (!current.valid()) return;
    pressDisplay_ = event->pos();
    currentDisplay_ = event->pos();
    dragging_ = true;
    hasDragArea_ = true;
    setFocus(Qt::MouseFocusReason);
    update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event) {
    if (!dragging_) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    currentDisplay_ = event->pos();
    update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    dragging_ = false;
    hasDragArea_ = false;
    currentDisplay_ = event->pos();
    const PreviewTransform current = transform();
    if (!current.valid()) {
        update();
        return;
    }
    const Rect candidate = dragToSourceRect(current, pressDisplay_.x(), pressDisplay_.y(),
                                            currentDisplay_.x(), currentDisplay_.y());
    update();
    if (candidate.empty()) return;
    const GeometryCheck check = checkCropRect(candidate, sourceSize_);
    if (!check.ok) return;
    if (rectEditedHandler_) rectEditedHandler_(candidate);
}

void RegionSelector::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && dragging_) {
        dragging_ = false;
        hasDragArea_ = false;
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

}  // namespace stock_monitor
