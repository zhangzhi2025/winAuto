#include "ElementPickerDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "Util.h"
#include "core/CancelToken.h"
#include "core/Json.h"
#include "core/UiaMatch.h"

#ifdef WINAUTO_HAVE_PLATFORM
#include <windows.h>

#include "UiaSession.h"
#include "Win32WindowProvider.h"
#endif

ElementPickerDialog::ElementPickerDialog(const wa::WindowSpec& window, QWidget* parent)
    : QDialog(parent), window_(window) {
    setWindowTitle(QStringLiteral("拾取控件"));
    resize(820, 600);
    auto* layout = new QVBoxLayout(this);

    info_ = new QLabel(this);
    info_->setWordWrap(true);
    layout->addWidget(info_);

    auto* bar = new QHBoxLayout();
    auto* reload = new QPushButton(QStringLiteral("重新读取"), this);
    hoverButton_ = new QPushButton(QStringLiteral("鼠标拾取（3 秒后取鼠标下的控件）"), this);
    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(QStringLiteral("按名称 / automationId / 类型过滤"));
    filter_->setClearButtonEnabled(true);
    bar->addWidget(reload);
    bar->addWidget(hoverButton_);
    bar->addWidget(filter_, 1);
    layout->addLayout(bar);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(4);
    tree_->setHeaderLabels({QStringLiteral("类型"), QStringLiteral("名称"), QStringLiteral("automationId"),
                            QStringLiteral("能力")});
    tree_->setColumnWidth(0, 130);
    tree_->setColumnWidth(1, 280);
    tree_->setColumnWidth(2, 200);
    layout->addWidget(tree_, 3);

    suggestion_ = new QPlainTextEdit(this);
    suggestion_->setReadOnly(true);
    suggestion_->setFixedHeight(120);
    layout->addWidget(suggestion_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    okButton_ = buttons->button(QDialogButtonBox::Ok);
    okButton_->setText(QStringLiteral("使用此定位器"));
    okButton_->setEnabled(false);
    layout->addWidget(buttons);

    timer_ = new QTimer(this);
    timer_->setInterval(1000);

    connect(buttons, &QDialogButtonBox::accepted, this, &ElementPickerDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(reload, &QPushButton::clicked, this, &ElementPickerDialog::loadTree);
    connect(hoverButton_, &QPushButton::clicked, this, &ElementPickerDialog::startHoverPick);
    connect(timer_, &QTimer::timeout, this, &ElementPickerDialog::onHoverTick);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &ElementPickerDialog::onSelectionChanged);
    connect(filter_, &QLineEdit::textChanged, this, &ElementPickerDialog::applyFilter);

#ifdef WINAUTO_HAVE_PLATFORM
    session_ = new wa::platform::UiaSession();
#endif
    // 先让对话框显示出来，再读取（读取大窗口可能要几秒）
    QTimer::singleShot(0, this, &ElementPickerDialog::loadTree);
}

ElementPickerDialog::~ElementPickerDialog() {
#ifdef WINAUTO_HAVE_PLATFORM
    delete session_;
#endif
}

void ElementPickerDialog::loadTree() {
    tree_->clear();
    items_.clear();
    snapshot_ = wa::UiaSnapshot();
    suggestion_->clear();
    okButton_->setEnabled(false);
    haveSuggestion_ = false;

#ifndef WINAUTO_HAVE_PLATFORM
    info_->setText(QStringLiteral("控件拾取仅在 Windows 上可用。"));
    return;
#else
    if (!session_->available()) {
        info_->setText(QStringLiteral("UI Automation 不可用：") + qs(session_->initError()));
        return;
    }
    wa::platform::Win32WindowProvider provider;
    auto found = provider.findWindow(window_);
    if (!found.ok()) {
        info_->setText(QStringLiteral("找不到目标窗口：") + qs(found.detail) +
                       QStringLiteral("\n请先打开目标程序，并确认\"目标窗口\"里的进程名/标题关键字。"));
        return;
    }
    if (found.value.elevated && !provider.isSelfElevated()) {
        info_->setText(QStringLiteral("目标窗口以管理员权限运行，本工具无法读取或操作它。"));
        return;
    }
    windowHandle_ = found.value.handle;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    wa::CancelToken token;
    auto snap = session_->snapshot(windowHandle_, token);
    QApplication::restoreOverrideCursor();
    if (!snap.ok()) {
        info_->setText(QStringLiteral("读取控件树失败：") + qs(snap.detail));
        return;
    }
    snapshot_ = std::move(snap.value);

    items_.assign(snapshot_.nodes.size(), nullptr);
    for (size_t i = 0; i < snapshot_.nodes.size(); ++i) {
        const wa::UiaNode& n = snapshot_.nodes[i];
        QStringList cols;
        QString caps;
        if (n.supportsInvoke) caps += QStringLiteral("Invoke ");
        if (n.supportsValue) caps += QStringLiteral("Value ");
        if (!n.enabled) caps += QStringLiteral("(禁用) ");
        if (n.offscreen) caps += QStringLiteral("(屏幕外)");
        cols << qs(n.controlType) << qs(n.name) << qs(n.automationId) << caps.trimmed();
        QTreeWidgetItem* item = n.parent >= 0 && items_[static_cast<size_t>(n.parent)]
                                    ? new QTreeWidgetItem(items_[static_cast<size_t>(n.parent)], cols)
                                    : new QTreeWidgetItem(tree_, cols);
        item->setData(0, Qt::UserRole, static_cast<int>(i));
        items_[i] = item;
    }
    tree_->expandAll();
    QString text = QStringLiteral("共 %1 个控件").arg(snapshot_.nodes.size());
    if (snapshot_.truncated) text += QStringLiteral("（已截断）");
    if (snapshot_.nodes.size() <= 1) {
        text += QStringLiteral("。该窗口几乎不暴露 UI Automation 控件，建议改用 window_point 坐标定位。");
    }
    info_->setText(text);
#endif
}

void ElementPickerDialog::onSelectionChanged() {
    haveSuggestion_ = false;
    okButton_->setEnabled(false);
    const auto selected = tree_->selectedItems();
    if (selected.isEmpty()) {
        suggestion_->clear();
        return;
    }
    const int index = selected.first()->data(0, Qt::UserRole).toInt();
    auto r = wa::suggestLocator(snapshot_, static_cast<size_t>(index));
    if (!r.ok()) {
        suggestion_->setPlainText(qs(r.detail));
        return;
    }
    QString text = qs(wa::locatorToJson(wa::Locator{r.value.locator}));
    for (const auto& w : r.value.warnings) text += QStringLiteral("\n⚠ ") + qs(w);
    suggestion_->setPlainText(text);
    locatorJson_ = qs(wa::locatorToJson(wa::Locator{r.value.locator}));
    haveSuggestion_ = true;
    okButton_->setEnabled(true);
}

void ElementPickerDialog::applyFilter(const QString& text) {
    const QString needle = text.trimmed();
    // 先全部隐藏，再把命中的节点及其祖先显示出来；空过滤则全部显示
    for (QTreeWidgetItem* item : items_) {
        if (item) item->setHidden(!needle.isEmpty());
    }
    if (needle.isEmpty()) return;
    for (QTreeWidgetItem* item : items_) {
        if (!item) continue;
        bool hit = false;
        for (int c = 0; c < 3; ++c) hit = hit || item->text(c).contains(needle, Qt::CaseInsensitive);
        if (!hit) continue;
        for (QTreeWidgetItem* p = item; p; p = p->parent()) p->setHidden(false);
    }
}

void ElementPickerDialog::selectNode(int nodeIndex) {
    if (nodeIndex < 0 || static_cast<size_t>(nodeIndex) >= items_.size()) return;
    QTreeWidgetItem* item = items_[static_cast<size_t>(nodeIndex)];
    filter_->clear();
    tree_->setCurrentItem(item);
    tree_->scrollToItem(item);
}

void ElementPickerDialog::startHoverPick() {
    if (snapshot_.nodes.empty()) return;
    countdown_ = 3;
    hoverButton_->setEnabled(false);
    hoverButton_->setText(QStringLiteral("请把鼠标放到目标控件上… %1").arg(countdown_));
    timer_->start();
}

void ElementPickerDialog::onHoverTick() {
    --countdown_;
    if (countdown_ > 0) {
        hoverButton_->setText(QStringLiteral("请把鼠标放到目标控件上… %1").arg(countdown_));
        return;
    }
    timer_->stop();
    hoverButton_->setEnabled(true);
    hoverButton_->setText(QStringLiteral("鼠标拾取（3 秒后取鼠标下的控件）"));
#ifdef WINAUTO_HAVE_PLATFORM
    POINT cursor{};
    if (!GetCursorPos(&cursor)) return;
    auto idx = session_->indexAtPoint(wa::Point{static_cast<int>(cursor.x), static_cast<int>(cursor.y)});
    if (!idx.ok()) {
        info_->setText(QStringLiteral("鼠标下没有可用的控件：") + qs(idx.detail));
        return;
    }
    selectNode(static_cast<int>(idx.value));
#endif
}

void ElementPickerDialog::accept() {
    if (!haveSuggestion_) return;
    QDialog::accept();
}
