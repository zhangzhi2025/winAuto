#include "TemplateCaptureDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QThread>
#include <QVBoxLayout>

#include "Util.h"
#include "core/CancelToken.h"
#include "core/Json.h"

#ifdef WINAUTO_HAVE_PLATFORM
#include "Win32WindowProvider.h"
#include "WindowGrab.h"
#endif

// ------------------------------------------------------------------ SelectionView

SelectionView::SelectionView(const QImage& image, QWidget* parent) : QWidget(parent), image_(image) {
    setFixedSize(image.size());
    setCursor(Qt::CrossCursor);
}

QPoint SelectionView::clamp(const QPoint& p) const {
    return QPoint(qBound(0, p.x(), image_.width() - 1), qBound(0, p.y(), image_.height() - 1));
}

void SelectionView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawImage(0, 0, image_);
    if (!sel_.isNull()) {
        p.setPen(QPen(QColor(220, 40, 40), 1));
        p.setBrush(QColor(220, 40, 40, 40));
        p.drawRect(sel_.adjusted(0, 0, -1, -1));
    }
}

void SelectionView::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    start_ = clamp(e->pos());
    dragging_ = true;
    sel_ = QRect();
    update();
}

void SelectionView::mouseMoveEvent(QMouseEvent* e) {
    if (!dragging_) return;
    sel_ = QRect(start_, clamp(e->pos())).normalized();
    update();
}

void SelectionView::mouseReleaseEvent(QMouseEvent* e) {
    if (!dragging_ || e->button() != Qt::LeftButton) return;
    dragging_ = false;
    sel_ = QRect(start_, clamp(e->pos())).normalized();
    update();
    if (sel_.width() > 1 && sel_.height() > 1) emit selectionFinished(sel_);
}

// ------------------------------------------------------------------ TemplateCaptureDialog

TemplateCaptureDialog::TemplateCaptureDialog(const QImage& image, const QString& baseDir, const QString& stepId,
                                             QWidget* parent)
    : QDialog(parent), image_(image.convertToFormat(QImage::Format_RGB32)), baseDir_(baseDir), stepId_(stepId) {
    setWindowTitle(QStringLiteral("截取模板"));
    resize(960, 700);

    // 灰度图只算一次：QImage::Format_RGB32 在小端机器上字节序就是 BGRA
    auto g = wa::grayFromBgra(image_.constBits(), static_cast<size_t>(image_.sizeInBytes()), image_.width(),
                              image_.height(), static_cast<int>(image_.bytesPerLine()));
    if (g.ok()) gray_ = std::move(g.value);

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(QStringLiteral("在下面的画面上按住鼠标左键拖出一块区域（框住图标、按钮或一小段文字，不要框大片空白）。"), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    view_ = new SelectionView(image_, this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(view_);
    scroll->setBackgroundRole(QPalette::Dark);
    layout->addWidget(scroll, 1);

    info_ = new QLabel(this);
    info_->setWordWrap(true);
    info_->setTextFormat(Qt::PlainText);
    layout->addWidget(info_);

    region_ = new QCheckBox(QStringLiteral("限定搜索范围到选区周围（更快，也能避免匹配到别处的相同图案）"), this);
    region_->setEnabled(false);
    layout->addWidget(region_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    okButton_ = buttons->button(QDialogButtonBox::Ok);
    okButton_->setText(QStringLiteral("保存模板并使用"));
    okButton_->setEnabled(false);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &TemplateCaptureDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(view_, &SelectionView::selectionFinished, this, &TemplateCaptureDialog::onSelection);

    info_->setText(QStringLiteral("目标窗口画面 %1×%2 像素。").arg(image_.width()).arg(image_.height()));
}

void TemplateCaptureDialog::onSelection(const QRect& rect) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const wa::CancelToken token;
    assessment_ = wa::assessTemplate(gray_, wa::Rect{rect.x(), rect.y(), rect.width(), rect.height()}, token);
    QApplication::restoreOverrideCursor();
    haveSelection_ = true;
    showAssessment();
}

void TemplateCaptureDialog::showAssessment() {
    QStringList lines;
    lines << QStringLiteral("选区 %1×%2 像素，位置 (%3, %4)")
                 .arg(assessment_.selection.w)
                 .arg(assessment_.selection.h)
                 .arg(assessment_.selection.x)
                 .arg(assessment_.selection.y);
    for (const auto& e : assessment_.errors) lines << QStringLiteral("✖ ") + qs(e);
    for (const auto& w : assessment_.warnings) lines << QStringLiteral("⚠ ") + qs(w);
    if (assessment_.usable && assessment_.warnings.empty()) lines << QStringLiteral("✔ 这块区域特征明显，在窗口里唯一，可以使用。");
    info_->setStyleSheet(!assessment_.usable ? QStringLiteral("color:#c0392b;")
                         : !assessment_.warnings.empty() ? QStringLiteral("color:#b07800;")
                                                          : QStringLiteral("color:#1e8449;"));
    info_->setText(lines.join(QLatin1Char('\n')));

    okButton_->setEnabled(assessment_.usable);
    region_->setEnabled(assessment_.usable);
    // 出现多处相同图案时默认限定范围；否则默认不限定（窗口里控件位置可能会变，限定太死反而找不到）
    region_->setChecked(assessment_.usable && assessment_.suggestedRegion.has_value());
}

bool TemplateCaptureDialog::saveTemplate(QString& relPath, QString& error) {
    const QDir dir(QDir(baseDir_).filePath(QStringLiteral("templates")));
    if (!QDir().mkpath(dir.absolutePath())) {
        error = QStringLiteral("无法创建文件夹：") + dir.absolutePath();
        return false;
    }
    QString stem;
    for (const QChar c : stepId_) stem += (c.isLetterOrNumber() || c == QLatin1Char('-') || c == QLatin1Char('_')) ? c : QLatin1Char('_');
    if (stem.isEmpty()) stem = QStringLiteral("template");
    // 不覆盖已有文件：其他步骤可能还在用同名模板
    QString name;
    for (int n = 1; n < 10000; ++n) {
        name = QStringLiteral("%1-%2.png").arg(stem).arg(n);
        if (!QFileInfo::exists(dir.filePath(name))) break;
    }
    const wa::Rect s = assessment_.selection;
    const QImage part = image_.copy(s.x, s.y, s.w, s.h).convertToFormat(QImage::Format_RGB32);  // 无 alpha：运行时拒绝带透明的模板
    if (!part.save(dir.filePath(name), "PNG")) {
        error = QStringLiteral("保存失败：") + dir.filePath(name);
        return false;
    }
    relPath = QStringLiteral("templates/") + name;
    return true;
}

void TemplateCaptureDialog::accept() {
    if (!haveSelection_ || !assessment_.usable) return;
    QString rel, error;
    if (!saveTemplate(rel, error)) {
        info_->setStyleSheet(QStringLiteral("color:#c0392b;"));
        info_->setText(error);
        return;
    }
    wa::ImageLocator loc;
    loc.templatePath = stdStr(rel);
    if (region_->isChecked()) {
        loc.searchRegion = assessment_.suggestedRegion
                               ? *assessment_.suggestedRegion
                               : wa::expandRegion(assessment_.selection, 40, gray_.w, gray_.h);
    }
    locatorJson_ = qs(wa::locatorToJson(wa::Locator{loc}));
    QDialog::accept();
}

bool TemplateCaptureDialog::grabTarget(const wa::WindowSpec& spec, QWidget* mainWindow, QImage& out, QString& error) {
#ifndef WINAUTO_HAVE_PLATFORM
    Q_UNUSED(spec);
    Q_UNUSED(mainWindow);
    Q_UNUSED(out);
    error = QStringLiteral("截取模板仅在 Windows 上可用。");
    return false;
#else
    wa::platform::Win32WindowProvider provider;
    auto found = provider.findWindow(spec);
    if (!found.ok()) {
        error = QStringLiteral("找不到目标窗口：") + qs(found.detail) +
                QStringLiteral("\n请先打开目标程序，并确认\"目标窗口\"里的进程名/标题关键字。");
        return false;
    }
    if (found.value.elevated && !provider.isSelfElevated()) {
        error = QStringLiteral("目标窗口以管理员权限运行，本工具无法操作它。");
        return false;
    }
    // 先让出屏幕：隐藏本程序窗口并把目标窗口带到前台，等它重绘完再截
    if (mainWindow) mainWindow->hide();
    QApplication::processEvents();
    auto act = provider.activateWindow(found.value.handle);
    QThread::msleep(600);
    auto shot = wa::platform::grabWindowClient(found.value.handle);
    if (mainWindow) {
        mainWindow->show();
        mainWindow->raise();
        mainWindow->activateWindow();
    }
    if (!act.ok()) {
        error = QStringLiteral("无法把目标窗口切到前台：") + qs(act.detail);
        return false;
    }
    if (!shot.ok()) {
        error = QStringLiteral("截取窗口画面失败：") + qs(shot.detail);
        return false;
    }
    const auto& b = shot.value;
    // 深拷贝：Bitmap 是局部变量，QImage 不能引用它的内存
    out = QImage(b.bgra.data(), b.w, b.h, b.w * 4, QImage::Format_RGB32).copy();
    return true;
#endif
}
