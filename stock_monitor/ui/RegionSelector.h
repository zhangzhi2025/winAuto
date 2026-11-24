#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

#include "RegionConfig.h"
#include "RegionGeometry.h"
#include "StockModel.h"

class QPainter;

namespace stock_monitor {

// Read-only client-area preview that lets the user drag a new region rectangle.
//
// The widget only maps mouse positions to source client pixels; it never sends
// input to the monitored window and never modifies the captured pixels. All
// rectangles it produces are validated with the shared geometry rules before they
// are reported to the owner.
class RegionSelector final : public QWidget {
public:
    explicit RegionSelector(QWidget* parent = nullptr);

    void setSnapshot(const Image& clientImage);
    void clearSnapshot();
    bool hasSnapshot() const { return !snapshot_.isNull(); }
    Size sourceSize() const { return sourceSize_; }

    void setRegions(const std::vector<CaptureRegion>& regions);
    void setActiveRegion(const QString& regionId);
    void setTableOverlay(const Rect& regionRect, const TableTemplate& table, bool enabled);
    void clearTableOverlay();

    void setEditingEnabled(bool enabled);
    bool editingEnabled() const { return editingEnabled_; }

    // Called with a validated, non-empty source rectangle when a drag finishes.
    void setRectEditedHandler(std::function<void(const Rect&)> handler);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct OverlayRegion {
        QString id;
        QString name;
        Rect rect;
    };

    PreviewTransform transform() const;
    QPoint toDisplay(const PreviewTransform& transform, const Point& source) const;
    void drawRegions(QPainter& painter, const PreviewTransform& transform) const;
    void drawTableOverlay(QPainter& painter, const PreviewTransform& transform) const;
    void drawRubberBand(QPainter& painter, const PreviewTransform& transform) const;

    QImage snapshot_;
    Size sourceSize_;
    std::vector<OverlayRegion> regions_;
    QString activeRegionId_;
    Rect overlayRegionRect_;
    TableTemplate overlayTable_;
    bool hasTableOverlay_ = false;
    bool editingEnabled_ = true;
    bool dragging_ = false;
    Point dragStart_;
    Point dragEnd_;
    bool hasDragArea_ = false;
    QPoint pressDisplay_;
    QPoint currentDisplay_;
    std::function<void(const Rect&)> rectEditedHandler_;
};

}  // namespace stock_monitor
