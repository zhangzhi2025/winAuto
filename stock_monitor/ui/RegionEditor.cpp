#include "RegionEditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>

#include "RegionGeometry.h"
#include "UiImageUtils.h"

namespace stock_monitor {
namespace {

constexpr int kCoordinateLimit = 99999;

const char kRectErrorHint[] =
    "矩形的宽度和高度必须为正，且必须完全落在配置记录的客户区范围内；坐标不会被缩放。";

QString describeRegion(const CaptureRegion& region) {
    return QStringLiteral("%1 · %2 · %3 ms")
        .arg(toQString(region.name))
        .arg(QString::fromUtf8(toString(region.mode)))
        .arg(region.intervalMs);
}

TableTemplate defaultTemplate(const Rect& rect) {
    TableTemplate table;
    table.firstRowY = 0;
    table.rowHeight = rect.height > 0 ? rect.height : 1;
    table.rowPitch = table.rowHeight;

    FieldDefinition field;
    field.id = "field-1";
    field.name = "列 1";
    field.x = 0;
    field.width = rect.width > 0 ? rect.width : 1;
    field.valueType = FieldValueType::Text;
    field.analyzeFontColor = false;
    table.fields.push_back(field);
    return table;
}

}  // namespace

RegionEditor::RegionEditor(QWidget* parent) : QWidget(parent) {
    buildUi();
    loadRegionIntoForm();
}

void RegionEditor::buildUi() {
    auto* listGroup = new QGroupBox(QStringLiteral("区域列表"), this);
    list_ = new QListWidget(listGroup);
    list_->setMinimumWidth(220);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    addButton_ = new QPushButton(QStringLiteral("新增"), listGroup);
    removeButton_ = new QPushButton(QStringLiteral("删除"), listGroup);
    upButton_ = new QPushButton(QStringLiteral("上移"), listGroup);
    downButton_ = new QPushButton(QStringLiteral("下移"), listGroup);

    auto* listButtons = new QHBoxLayout();
    listButtons->addWidget(addButton_);
    listButtons->addWidget(removeButton_);
    listButtons->addWidget(upButton_);
    listButtons->addWidget(downButton_);

    auto* listLayout = new QVBoxLayout(listGroup);
    listLayout->addWidget(list_, 1);
    listLayout->addLayout(listButtons);

    auto* formGroup = new QGroupBox(QStringLiteral("区域设置"), this);
    nameEdit_ = new QLineEdit(formGroup);
    nameEdit_->setPlaceholderText(QStringLiteral("区域名称"));

    modeCombo_ = new QComboBox(formGroup);
    modeCombo_->addItem(QString::fromUtf8(toString(CaptureRegionMode::Ocr)));
    modeCombo_->addItem(QString::fromUtf8(toString(CaptureRegionMode::Table)));

    enabledCheck_ = new QCheckBox(QStringLiteral("参与定时采集"), formGroup);
    colorCheck_ = new QCheckBox(QStringLiteral("分析字体颜色"), formGroup);

    intervalSpin_ = new QSpinBox(formGroup);
    intervalSpin_->setRange(kMinRegionIntervalMs, kMaxRegionIntervalMs);
    intervalSpin_->setSingleStep(100);
    intervalSpin_->setSuffix(QStringLiteral(" ms"));

    xSpin_ = new QSpinBox(formGroup);
    xSpin_->setRange(0, kCoordinateLimit);
    xSpin_->setPrefix(QStringLiteral("x "));
    ySpin_ = new QSpinBox(formGroup);
    ySpin_->setRange(0, kCoordinateLimit);
    ySpin_->setPrefix(QStringLiteral("y "));
    widthSpin_ = new QSpinBox(formGroup);
    widthSpin_->setRange(1, kCoordinateLimit);
    widthSpin_->setPrefix(QStringLiteral("w "));
    heightSpin_ = new QSpinBox(formGroup);
    heightSpin_->setRange(1, kCoordinateLimit);
    heightSpin_->setPrefix(QStringLiteral("h "));

    auto* rectRow = new QHBoxLayout();
    rectRow->addWidget(xSpin_);
    rectRow->addWidget(ySpin_);
    rectRow->addWidget(widthSpin_);
    rectRow->addWidget(heightSpin_);

    auto* rectHolder = new QWidget(formGroup);
    rectHolder->setLayout(rectRow);

    auto* formLayout = new QFormLayout(formGroup);
    formLayout->addRow(QStringLiteral("名称"), nameEdit_);
    formLayout->addRow(QStringLiteral("模式"), modeCombo_);
    formLayout->addRow(QString(), enabledCheck_);
    formLayout->addRow(QString(), colorCheck_);
    formLayout->addRow(QStringLiteral("抓取间隔"), intervalSpin_);
    formLayout->addRow(QStringLiteral("矩形（相对客户区，物理像素）"), rectHolder);

    tableGroup_ = new QGroupBox(QStringLiteral("表格模板"), this);
    firstRowSpin_ = new QSpinBox(tableGroup_);
    firstRowSpin_->setRange(0, kCoordinateLimit);
    firstRowSpin_->setPrefix(QStringLiteral("y "));
    rowHeightSpin_ = new QSpinBox(tableGroup_);
    rowHeightSpin_->setRange(1, kCoordinateLimit);
    rowHeightSpin_->setPrefix(QStringLiteral("h "));
    rowPitchSpin_ = new QSpinBox(tableGroup_);
    rowPitchSpin_->setRange(1, kCoordinateLimit);
    rowPitchSpin_->setPrefix(QStringLiteral("pitch "));

    auto* gridRow = new QHBoxLayout();
    gridRow->addWidget(new QLabel(QStringLiteral("首行偏移"), tableGroup_));
    gridRow->addWidget(firstRowSpin_);
    gridRow->addWidget(new QLabel(QStringLiteral("行高"), tableGroup_));
    gridRow->addWidget(rowHeightSpin_);
    gridRow->addWidget(new QLabel(QStringLiteral("行距"), tableGroup_));
    gridRow->addWidget(rowPitchSpin_);
    gridRow->addStretch(1);

    fieldsTable_ = new QTableWidget(0, 5, tableGroup_);
    fieldsTable_->setHorizontalHeaderLabels({QStringLiteral("字段名"), QStringLiteral("X 偏移"),
                                             QStringLiteral("宽度"), QStringLiteral("类型"),
                                             QStringLiteral("颜色")});
    fieldsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    fieldsTable_->verticalHeader()->setVisible(false);
    fieldsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);

    addFieldButton_ = new QPushButton(QStringLiteral("添加字段"), tableGroup_);
    removeFieldButton_ = new QPushButton(QStringLiteral("删除字段"), tableGroup_);

    auto* fieldButtons = new QHBoxLayout();
    fieldButtons->addWidget(addFieldButton_);
    fieldButtons->addWidget(removeFieldButton_);
    fieldButtons->addStretch(1);

    auto* tableLayout = new QVBoxLayout(tableGroup_);
    tableLayout->addLayout(gridRow);
    tableLayout->addWidget(fieldsTable_, 1);
    tableLayout->addLayout(fieldButtons);

    status_ = new QLabel(this);
    status_->setWordWrap(true);

    auto* rightLayout = new QVBoxLayout();
    rightLayout->addWidget(formGroup);
    rightLayout->addWidget(tableGroup_, 1);
    rightLayout->addWidget(status_);

    auto* rootLayout = new QHBoxLayout(this);
    rootLayout->addWidget(listGroup);
    rootLayout->addLayout(rightLayout, 1);

    connect(addButton_, &QPushButton::clicked, this, [this]() { addRegion(); });
    connect(removeButton_, &QPushButton::clicked, this, [this]() { removeRegion(); });
    connect(upButton_, &QPushButton::clicked, this, [this]() { moveRegion(-1); });
    connect(downButton_, &QPushButton::clicked, this, [this]() { moveRegion(1); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { onSelectionChanged(); });
    connect(nameEdit_, &QLineEdit::textEdited, this, [this](const QString&) { applyRegionFields(); });
    connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        applyModeField();
    });
    connect(enabledCheck_, &QCheckBox::toggled, this, [this](bool) { applyRegionFields(); });
    connect(colorCheck_, &QCheckBox::toggled, this, [this](bool) { applyRegionFields(); });
    connect(intervalSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        applyIntervalField();
    });
    connect(xSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyRectField(); });
    connect(ySpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { applyRectField(); });
    connect(widthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        applyRectField();
    });
    connect(heightSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        applyRectField();
    });
    connect(firstRowSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        applyRegionFields();
    });
    connect(rowHeightSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        applyRegionFields();
    });
    connect(rowPitchSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        applyRegionFields();
    });
    connect(fieldsTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        applyFieldsTable();
    });
    connect(addFieldButton_, &QPushButton::clicked, this, [this]() { addFieldRow(); });
    connect(removeFieldButton_, &QPushButton::clicked, this, [this]() { removeFieldRow(); });
}

void RegionEditor::setClientSize(const Size& clientSize) {
    clientSize_ = clientSize;
    if(clientSize_.width <= 0 || clientSize_.height <= 0) {
        showStatus(QStringLiteral("尚未记录目标窗口的客户区尺寸，请先选择活动窗口。"), true);
        return;
    }
    const std::string error = firstValidationError();
    if(error.empty()) {
        showStatus(QStringLiteral("客户区 %1 x %2 物理像素，所有区域都在范围内。")
                       .arg(clientSize_.width)
                       .arg(clientSize_.height),
                   false);
    } else {
        showStatus(toQString(error) + QString::fromUtf8(kRectErrorHint), true);
    }
}

void RegionEditor::setRegions(std::vector<CaptureRegion> regions) {
    regions_ = std::move(regions);
    refreshList();
    if(regions_.empty()) {
        currentIndex_ = -1;
        list_->setCurrentRow(-1);
        loadRegionIntoForm();
        return;
    }
    selectRegionIndex(0);
}

void RegionEditor::setEditingEnabled(bool enabled) {
    editingEnabled_ = enabled;
    loadRegionIntoForm();
    if(!enabled) {
        showStatus(QStringLiteral("正在运行，区域配置为只读；请先停止采集再编辑。"), false);
    }
}

QString RegionEditor::selectedRegionId() const {
    const CaptureRegion* region = currentRegion();
    return region == nullptr ? QString() : toQString(region->id);
}

void RegionEditor::selectRegionIndex(int index) {
    if(regions_.empty()) {
        currentIndex_ = -1;
        list_->setCurrentRow(-1);
        loadRegionIntoForm();
        return;
    }
    if(index < 0) {
        index = 0;
    }
    if(static_cast<std::size_t>(index) >= regions_.size()) {
        index = static_cast<int>(regions_.size()) - 1;
    }
    if(list_->currentRow() == index) {
        currentIndex_ = index;
        loadRegionIntoForm();
    } else {
        list_->setCurrentRow(index);
    }
}

std::string RegionEditor::firstValidationError() const {
    for(std::size_t index = 0; index < regions_.size(); ++index) {
        const ConfigValidation validation =
            validateCaptureRegion(regions_[index], clientSize_,
                                  "regions[" + std::to_string(index) + "]");
        if(!validation.valid) {
            return validation.path + ": " + validation.message;
        }
    }
    for(std::size_t index = 0; index < regions_.size(); ++index) {
        for(std::size_t other = index + 1; other < regions_.size(); ++other) {
            if(regions_[index].id == regions_[other].id) {
                return "regions[" + std::to_string(other) + "].id: region id '" +
                       regions_[other].id + "' is duplicated";
            }
        }
    }
    return {};
}

void RegionEditor::setRegionsChangedHandler(std::function<void()> handler) {
    regionsChangedHandler_ = std::move(handler);
}

void RegionEditor::setActiveRegionChangedHandler(std::function<void(const QString&)> handler) {
    activeRegionChangedHandler_ = std::move(handler);
}

void RegionEditor::refreshList() {
    updating_ = true;
    list_->clear();
    for(const CaptureRegion& region : regions_) {
        new QListWidgetItem(describeRegion(region), list_);
    }
    updating_ = false;
}

void RegionEditor::refreshListRow(int index) {
    if(index < 0 || static_cast<std::size_t>(index) >= regions_.size()) {
        return;
    }
    QListWidgetItem* item = list_->item(index);
    if(item != nullptr) {
        item->setText(describeRegion(regions_[static_cast<std::size_t>(index)]));
    }
}

void RegionEditor::onSelectionChanged() {
    if(updating_) {
        return;
    }
    const int row = list_->currentRow();
    currentIndex_ = (row >= 0 && static_cast<std::size_t>(row) < regions_.size()) ? row : -1;
    loadRegionIntoForm();
    if(activeRegionChangedHandler_) {
        activeRegionChangedHandler_(selectedRegionId());
    }
}

void RegionEditor::showStatus(const QString& message, bool error) {
    status_->setText(message);
    status_->setStyleSheet(error ? QStringLiteral("color: #b00020;")
                                 : QStringLiteral("color: #107c10;"));
}

CaptureRegion* RegionEditor::currentRegion() {
    if(currentIndex_ < 0 || static_cast<std::size_t>(currentIndex_) >= regions_.size()) {
        return nullptr;
    }
    return &regions_[static_cast<std::size_t>(currentIndex_)];
}

const CaptureRegion* RegionEditor::currentRegion() const {
    if(currentIndex_ < 0 || static_cast<std::size_t>(currentIndex_) >= regions_.size()) {
        return nullptr;
    }
    return &regions_[static_cast<std::size_t>(currentIndex_)];
}

void RegionEditor::notifyRegionsChanged() {
    if(regionsChangedHandler_) {
        regionsChangedHandler_();
    }
}

std::string RegionEditor::nextRegionId() const {
    int suffix = 1;
    for(;;) {
        const std::string candidate = "region-" + std::to_string(suffix);
        const bool taken =
            std::any_of(regions_.begin(), regions_.end(), [&candidate](const CaptureRegion& region) {
                return region.id == candidate;
            });
        if(!taken) {
            return candidate;
        }
        ++suffix;
    }
}

std::string RegionEditor::nextFieldId(const TableTemplate& table) const {
    int suffix = 1;
    for(;;) {
        const std::string candidate = "field-" + std::to_string(suffix);
        const bool taken =
            std::any_of(table.fields.begin(), table.fields.end(),
                        [&candidate](const FieldDefinition& field) { return field.id == candidate; });
        if(!taken) {
            return candidate;
        }
        ++suffix;
    }
}

std::string RegionEditor::regionPath(int index) const {
    return "regions[" + std::to_string(index) + "]";
}

void RegionEditor::loadRegionIntoForm() {
    updating_ = true;

    const CaptureRegion* region = currentRegion();
    const bool hasRegion = region != nullptr;
    const bool editable = editingEnabled_ && hasRegion;

    if(hasRegion) {
        nameEdit_->setText(toQString(region->name));
        const int modeIndex = modeCombo_->findText(QString::fromUtf8(toString(region->mode)));
        modeCombo_->setCurrentIndex(modeIndex < 0 ? 0 : modeIndex);
        enabledCheck_->setChecked(region->enabled);
        colorCheck_->setChecked(region->analyzeFontColor);
        intervalSpin_->setValue(region->intervalMs);
        xSpin_->setValue(region->rect.x);
        ySpin_->setValue(region->rect.y);
        widthSpin_->setValue(region->rect.width);
        heightSpin_->setValue(region->rect.height);
    } else {
        nameEdit_->clear();
        modeCombo_->setCurrentIndex(0);
        enabledCheck_->setChecked(true);
        colorCheck_->setChecked(false);
        intervalSpin_->setValue(1000);
        xSpin_->setValue(0);
        ySpin_->setValue(0);
        widthSpin_->setValue(1);
        heightSpin_->setValue(1);
    }

    const bool table = hasRegion && region->mode == CaptureRegionMode::Table;
    if(table) {
        firstRowSpin_->setValue(region->table.firstRowY);
        rowHeightSpin_->setValue(region->table.rowHeight);
        rowPitchSpin_->setValue(region->table.rowPitch);
    } else {
        firstRowSpin_->setValue(0);
        rowHeightSpin_->setValue(1);
        rowPitchSpin_->setValue(1);
    }
    rebuildFieldsTable();

    updating_ = false;

    nameEdit_->setEnabled(editable);
    modeCombo_->setEnabled(editable);
    enabledCheck_->setEnabled(editable);
    colorCheck_->setEnabled(editable);
    intervalSpin_->setEnabled(editable);
    xSpin_->setEnabled(editable);
    ySpin_->setEnabled(editable);
    widthSpin_->setEnabled(editable);
    heightSpin_->setEnabled(editable);
    tableGroup_->setEnabled(editable && table);

    addButton_->setEnabled(editingEnabled_);
    removeButton_->setEnabled(editable);
    upButton_->setEnabled(editable && currentIndex_ > 0);
    downButton_->setEnabled(editable &&
                            static_cast<std::size_t>(currentIndex_ + 1) < regions_.size());
}

void RegionEditor::rebuildFieldsTable() {
    const bool wasUpdating = updating_;
    updating_ = true;

    fieldsTable_->clearContents();
    fieldsTable_->setRowCount(0);

    const CaptureRegion* region = currentRegion();
    if(region != nullptr && region->mode == CaptureRegionMode::Table) {
        const std::vector<FieldDefinition>& fields = region->table.fields;
        fieldsTable_->setRowCount(static_cast<int>(fields.size()));
        for(std::size_t index = 0; index < fields.size(); ++index) {
            const int row = static_cast<int>(index);
            const FieldDefinition& field = fields[index];

            auto* nameItem = new QTableWidgetItem(toQString(field.name));
            nameItem->setData(Qt::UserRole, toQString(field.id));
            fieldsTable_->setItem(row, 0, nameItem);

            fieldsTable_->setItem(row, 1, new QTableWidgetItem(QString::number(field.x)));
            fieldsTable_->setItem(row, 2, new QTableWidgetItem(QString::number(field.width)));

            auto* typeCombo = new QComboBox(fieldsTable_);
            typeCombo->addItem(QString::fromUtf8(toString(FieldValueType::Text)));
            typeCombo->addItem(QString::fromUtf8(toString(FieldValueType::Integer)));
            typeCombo->addItem(QString::fromUtf8(toString(FieldValueType::Decimal)));
            typeCombo->addItem(QString::fromUtf8(toString(FieldValueType::Percent)));
            const int typeIndex = typeCombo->findText(QString::fromUtf8(toString(field.valueType)));
            typeCombo->setCurrentIndex(typeIndex < 0 ? 0 : typeIndex);
            connect(typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                    [this](int) { applyFieldsTable(); });
            fieldsTable_->setCellWidget(row, 3, typeCombo);

            auto* colorItem = new QTableWidgetItem();
            colorItem->setFlags((colorItem->flags() | Qt::ItemIsUserCheckable) &
                                ~Qt::ItemIsEditable);
            colorItem->setCheckState(field.analyzeFontColor ? Qt::Checked : Qt::Unchecked);
            fieldsTable_->setItem(row, 4, colorItem);
        }
    }

    updating_ = wasUpdating;
}

void RegionEditor::applyRegionFields() {
    if(updating_) {
        return;
    }
    commit(
        [this](CaptureRegion& region) {
            region.name = nameEdit_->text().toStdString();
            region.enabled = enabledCheck_->isChecked();
            region.analyzeFontColor = colorCheck_->isChecked();
            region.table.firstRowY = firstRowSpin_->value();
            region.table.rowHeight = rowHeightSpin_->value();
            region.table.rowPitch = rowPitchSpin_->value();
        },
        false);
}

void RegionEditor::applyRectField() {
    if(updating_) {
        return;
    }
    if(clientSize_.width <= 0 || clientSize_.height <= 0) {
        showStatus(QStringLiteral("请先选择目标窗口并记录客户区尺寸，再配置区域矩形。"), true);
        loadRegionIntoForm();
        return;
    }
    commit(
        [this](CaptureRegion& region) {
            region.rect.x = xSpin_->value();
            region.rect.y = ySpin_->value();
            region.rect.width = widthSpin_->value();
            region.rect.height = heightSpin_->value();
        },
        true);
}

void RegionEditor::applyModeField() {
    if(updating_) {
        return;
    }
    CaptureRegionMode mode = CaptureRegionMode::Ocr;
    if(!parseCaptureRegionMode(modeCombo_->currentText().toStdString(), mode)) {
        loadRegionIntoForm();
        return;
    }
    commit(
        [mode](CaptureRegion& region) {
            region.mode = mode;
            if(mode == CaptureRegionMode::Table && region.table.fields.empty()) {
                region.table = defaultTemplate(region.rect);
            }
        },
        false);
    loadRegionIntoForm();
}

void RegionEditor::applyIntervalField() {
    if(updating_) {
        return;
    }
    commit([this](CaptureRegion& region) { region.intervalMs = intervalSpin_->value(); }, false);
}

void RegionEditor::applyFieldsTable() {
    if(updating_) {
        return;
    }
    CaptureRegion* region = currentRegion();
    if(region == nullptr || region->mode != CaptureRegionMode::Table) {
        return;
    }

    std::vector<FieldDefinition> fields;
    const int rowCount = fieldsTable_->rowCount();
    fields.reserve(static_cast<std::size_t>(rowCount));
    for(int row = 0; row < rowCount; ++row) {
        FieldDefinition field;
        QTableWidgetItem* nameItem = fieldsTable_->item(row, 0);
        if(nameItem != nullptr) {
            const QString id = nameItem->data(Qt::UserRole).toString();
            field.id = id.isEmpty() ? ("field-" + std::to_string(row + 1)) : id.toStdString();
            field.name = nameItem->text().toStdString();
        } else {
            field.id = "field-" + std::to_string(row + 1);
            field.name = field.id;
        }
        if(field.name.empty()) {
            field.name = field.id;
        }

        QTableWidgetItem* xItem = fieldsTable_->item(row, 1);
        field.x = xItem == nullptr ? 0 : xItem->text().toInt();
        QTableWidgetItem* widthItem = fieldsTable_->item(row, 2);
        field.width = widthItem == nullptr ? 0 : widthItem->text().toInt();

        FieldValueType valueType = FieldValueType::Text;
        if(auto* combo = qobject_cast<QComboBox*>(fieldsTable_->cellWidget(row, 3))) {
            if(!parseFieldValueType(combo->currentText().toStdString(), valueType)) {
                valueType = FieldValueType::Text;
            }
        }
        field.valueType = valueType;

        QTableWidgetItem* colorItem = fieldsTable_->item(row, 4);
        field.analyzeFontColor = colorItem != nullptr && colorItem->checkState() == Qt::Checked;

        fields.push_back(field);
    }

    CaptureRegion candidate = *region;
    candidate.table.fields = fields;
    const ConfigValidation validation =
        validateCaptureRegion(candidate, clientSize_, regionPath(currentIndex_));
    if(!validation.valid) {
        showStatus(toQString(validation.path) + QStringLiteral(": ") + toQString(validation.message),
                   true);
        return;
    }

    region->table.fields = fields;
    notifyRegionsChanged();
    showStatus(QStringLiteral("表格字段已更新。"), false);
}

void RegionEditor::addFieldRow() {
    if(!editingEnabled_) {
        return;
    }
    CaptureRegion* region = currentRegion();
    if(region == nullptr || region->mode != CaptureRegionMode::Table) {
        return;
    }
    const std::string fieldId = nextFieldId(region->table);
    const std::string fieldName = "列 " + std::to_string(region->table.fields.size() + 1);
    const int fieldWidth = region->rect.width > 0 ? region->rect.width : 1;
    commit(
        [fieldId, fieldName, fieldWidth](CaptureRegion& target) {
            FieldDefinition field;
            field.id = fieldId;
            field.name = fieldName;
            field.x = 0;
            field.width = fieldWidth;
            field.valueType = FieldValueType::Text;
            field.analyzeFontColor = false;
            target.table.fields.push_back(field);
        },
        false);
    loadRegionIntoForm();
}

void RegionEditor::removeFieldRow() {
    if(!editingEnabled_) {
        return;
    }
    const int row = fieldsTable_->currentRow();
    if(row < 0) {
        showStatus(QStringLiteral("请先选择要删除的字段行。"), true);
        return;
    }
    commit(
        [row](CaptureRegion& region) {
            if(static_cast<std::size_t>(row) < region.table.fields.size()) {
                region.table.fields.erase(region.table.fields.begin() + row);
            }
        },
        false);
    loadRegionIntoForm();
}

bool RegionEditor::applyRectFromPreview(const Rect& rect) {
    if(!editingEnabled_ || updating_ || currentRegion() == nullptr) {
        return false;
    }
    commit([&rect](CaptureRegion& region) { region.rect = rect; }, true);
    const CaptureRegion* applied = currentRegion();
    if(applied == nullptr) {
        return false;
    }
    return applied->rect.x == rect.x && applied->rect.y == rect.y &&
           applied->rect.width == rect.width && applied->rect.height == rect.height;
}

void RegionEditor::commit(const std::function<void(CaptureRegion&)>& mutate, bool validateRect) {
    if(!editingEnabled_) {
        return;
    }
    CaptureRegion* region = currentRegion();
    if(region == nullptr) {
        return;
    }

    CaptureRegion candidate = *region;
    mutate(candidate);

    if(validateRect) {
        const GeometryCheck check = checkCropRect(candidate.rect, clientSize_);
        if(!check.ok) {
            showStatus(toQString(check.message) + QString::fromUtf8(kRectErrorHint), true);
            loadRegionIntoForm();
            return;
        }
    }

    *region = candidate;
    refreshListRow(currentIndex_);

    const ConfigValidation validation =
        validateCaptureRegion(candidate, clientSize_, regionPath(currentIndex_));
    if(validation.valid) {
        showStatus(QStringLiteral("区域已更新。"), false);
    } else {
        showStatus(toQString(validation.path) + QStringLiteral(": ") + toQString(validation.message),
                   true);
    }
    notifyRegionsChanged();
}

void RegionEditor::addRegion() {
    if(!editingEnabled_) {
        return;
    }

    const int availableWidth = clientSize_.width > 0 ? clientSize_.width : 200;
    const int availableHeight = clientSize_.height > 0 ? clientSize_.height : 80;

    CaptureRegion region;
    region.id = nextRegionId();
    region.name = region.id;
    region.rect.x = 0;
    region.rect.y = 0;
    region.rect.width = std::min(200, availableWidth);
    region.rect.height = std::min(80, availableHeight);
    region.intervalMs = 1000;
    region.enabled = true;
    region.mode = CaptureRegionMode::Ocr;
    region.analyzeFontColor = false;

    regions_.push_back(region);
    refreshList();
    const int index = static_cast<int>(regions_.size()) - 1;
    selectRegionIndex(index);
    notifyRegionsChanged();

    const ConfigValidation validation = validateCaptureRegion(region, clientSize_, regionPath(index));
    if(validation.valid) {
        showStatus(QStringLiteral("已新增区域 %1，请核对矩形后保存配置。").arg(toQString(region.id)),
                   false);
    } else {
        showStatus(toQString(validation.path) + QStringLiteral(": ") + toQString(validation.message),
                   true);
    }
}

void RegionEditor::removeRegion() {
    if(!editingEnabled_ || currentIndex_ < 0) {
        return;
    }
    regions_.erase(regions_.begin() + currentIndex_);
    refreshList();
    if(regions_.empty()) {
        currentIndex_ = -1;
        list_->setCurrentRow(-1);
        loadRegionIntoForm();
        showStatus(QStringLiteral("区域已删除，当前没有区域。"), false);
    } else {
        int next = currentIndex_;
        if(static_cast<std::size_t>(next) >= regions_.size()) {
            next = static_cast<int>(regions_.size()) - 1;
        }
        selectRegionIndex(next);
        showStatus(QStringLiteral("区域已删除。"), false);
    }
    notifyRegionsChanged();
}

void RegionEditor::moveRegion(int offset) {
    if(!editingEnabled_ || currentIndex_ < 0) {
        return;
    }
    const int target = currentIndex_ + offset;
    if(target < 0 || static_cast<std::size_t>(target) >= regions_.size()) {
        return;
    }
    std::swap(regions_[static_cast<std::size_t>(currentIndex_)],
              regions_[static_cast<std::size_t>(target)]);
    refreshList();
    selectRegionIndex(target);
    notifyRegionsChanged();
    showStatus(QStringLiteral("区域顺序已更新。"), false);
}

}  // namespace stock_monitor
