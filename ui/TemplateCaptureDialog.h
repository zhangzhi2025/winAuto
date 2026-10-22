#pragma once
// 截取模板：先截下目标窗口当前画面，用户在画面上拖出一块区域；保存前用 core 的评估器自检
// （纯色 / 太小 / 出现多处），结果以文字和颜色告诉用户。通过后把选区存成 PNG，并生成 image 定位器 JSON。
#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QWidget>

#include "core/GrayImage.h"
#include "core/Model.h"
#include "core/TemplateAssess.h"

class QCheckBox;
class QLabel;
class QPushButton;

// 在截图上拖拽选区的控件（1:1 显示，不缩放，保证选区坐标就是像素坐标）
class SelectionView : public QWidget {
    Q_OBJECT
public:
    explicit SelectionView(const QImage& image, QWidget* parent = nullptr);
    QRect selection() const { return sel_; }

signals:
    void selectionFinished(const QRect& rect);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QPoint clamp(const QPoint& p) const;
    QImage image_;
    QRect sel_;
    QPoint start_;
    bool dragging_ = false;
};

class TemplateCaptureDialog : public QDialog {
    Q_OBJECT
public:
    // image：目标窗口 client 区域截图；baseDir：流程文件所在目录（模板保存到 baseDir/templates/）；
    // stepId：用来给文件命名。
    TemplateCaptureDialog(const QImage& image, const QString& baseDir, const QString& stepId,
                          QWidget* parent = nullptr);

    // 确认后：image 定位器 JSON（template 是相对流程文件的路径）
    QString locatorJson() const { return locatorJson_; }

    // 隐藏 mainWindow、激活目标窗口、截取其 client 区域。失败返回 false 并给出原因。
    // 仅 Windows 有效；其他平台直接失败。
    static bool grabTarget(const wa::WindowSpec& spec, QWidget* mainWindow, QImage& out, QString& error);

public slots:
    void accept() override;

private slots:
    void onSelection(const QRect& rect);

private:
    void showAssessment();
    bool saveTemplate(QString& relPath, QString& error);

    QImage image_;
    wa::GrayImage gray_;
    QString baseDir_;
    QString stepId_;
    QString locatorJson_;
    wa::TemplateAssessment assessment_;
    bool haveSelection_ = false;

    SelectionView* view_ = nullptr;
    QLabel* info_ = nullptr;
    QCheckBox* region_ = nullptr;
    QPushButton* okButton_ = nullptr;
};
