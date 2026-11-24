#pragma once

#include <QString>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

#include "RegionConfig.h"
#include "StockModel.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace stock_monitor {

// Owns the editable list of capture regions and, for table regions, the columns
// and row grid that turn recognised text into typed values.
//
// The widget only edits the in-memory model. Rectangles are validated against the
// configured client size with the core geometry rules, so an out-of-range edit is
// rejected and the previous value restored instead of being silently accepted.
class RegionEditor final : public QWidget {
public:
    explicit RegionEditor(QWidget* parent = nullptr);

    // Client-area size recorded in the configuration. Rectangles are validated
    // against this size and are never rescaled.
    void setClientSize(const Size& clientSize);
    Size clientSize() const { return clientSize_; }

    void setRegions(std::vector<CaptureRegion> regions);
    const std::vector<CaptureRegion>& regions() const { return regions_; }

    void setEditingEnabled(bool enabled);
    bool editingEnabled() const { return editingEnabled_; }

    int currentIndex() const { return currentIndex_; }
    QString selectedRegionId() const;
    void selectRegionIndex(int index);

    // First problem found in the region list, or an empty string when every
    // region is valid for the recorded client size.
    std::string firstValidationError() const;

    // Applies a rectangle dragged on the preview to the currently selected
    // region. The edit goes through the same geometry validation as the spin
    // boxes, so an invalid rectangle is rejected and the previous one restored.
    bool applyRectFromPreview(const Rect& rect);

    void setRegionsChangedHandler(std::function<void()> handler);
    void setActiveRegionChangedHandler(std::function<void(const QString&)> handler);

private:
    void buildUi();
    void refreshList();
    void refreshListRow(int index);
    void loadRegionIntoForm();
    void rebuildFieldsTable();
    void showStatus(const QString& message, bool error);

    void addRegion();
    void removeRegion();
    void moveRegion(int offset);
    void onSelectionChanged();
    void applyRegionFields();
    void applyRectField();
    void applyModeField();
    void applyIntervalField();
    void applyFieldsTable();
    void addFieldRow();
    void removeFieldRow();

    CaptureRegion* currentRegion();
    const CaptureRegion* currentRegion() const;
    void commit(const std::function<void(CaptureRegion&)>& mutate, bool validateRect);
    void notifyRegionsChanged();
    std::string nextRegionId() const;
    std::string nextFieldId(const TableTemplate& table) const;
    std::string regionPath(int index) const;

    QListWidget* list_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeButton_ = nullptr;
    QPushButton* upButton_ = nullptr;
    QPushButton* downButton_ = nullptr;

    QLineEdit* nameEdit_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QCheckBox* enabledCheck_ = nullptr;
    QCheckBox* colorCheck_ = nullptr;
    QSpinBox* intervalSpin_ = nullptr;
    QSpinBox* xSpin_ = nullptr;
    QSpinBox* ySpin_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;
    QSpinBox* heightSpin_ = nullptr;

    QGroupBox* tableGroup_ = nullptr;
    QSpinBox* firstRowSpin_ = nullptr;
    QSpinBox* rowHeightSpin_ = nullptr;
    QSpinBox* rowPitchSpin_ = nullptr;
    QTableWidget* fieldsTable_ = nullptr;
    QPushButton* addFieldButton_ = nullptr;
    QPushButton* removeFieldButton_ = nullptr;

    QLabel* status_ = nullptr;

    std::vector<CaptureRegion> regions_;
    Size clientSize_;
    int currentIndex_ = -1;
    bool editingEnabled_ = true;
    bool updating_ = false;
    std::function<void()> regionsChangedHandler_;
    std::function<void(const QString&)> activeRegionChangedHandler_;
};

}  // namespace stock_monitor
