#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

class ModelSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ModelSettingsDialog(QWidget* parent = nullptr);

private:
    void loadProviderSettings();
    void saveSettings();
    void clearCredential();

    QComboBox* provider_ = nullptr;
    QLineEdit* model_ = nullptr;
    QLineEdit* credential_ = nullptr;
    QLabel* status_ = nullptr;
};
