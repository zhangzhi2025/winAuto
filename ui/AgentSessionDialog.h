#pragma once

#include <QDialog>

#include "AgentConnectionSettings.h"
#include "AgentSession.h"

class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QCloseEvent;

class AgentSessionDialog : public QDialog {
    Q_OBJECT
public:
    explicit AgentSessionDialog(QWidget* parent = nullptr);
    ~AgentSessionDialog() override;

public slots:
    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void browseDirectory();
    void saveSettings();
    void clearSettings();
    void connectSession();
    void disconnectSession();
    void sendPrompt();
    void abortTurn();
    void onEvent(const wa::agent::AgentEvent& event);
    void onConnectionState(wa::agent::ConnectionState state);
    void appendHistory(const QString& text);
    wa::agent::AgentConnectionSettings formSettings() const;

    QLineEdit* url_ = nullptr;
    QLineEdit* directory_ = nullptr;
    QLineEdit* sessionId_ = nullptr;
    QLineEdit* prompt_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* sessionDetails_ = nullptr;
    QPlainTextEdit* history_ = nullptr;
    QPushButton* connect_ = nullptr;
    QPushButton* disconnect_ = nullptr;
    QPushButton* send_ = nullptr;
    QPushButton* abort_ = nullptr;
    wa::agent::AgentSession* session_ = nullptr;
    bool closePending_ = false;
};
