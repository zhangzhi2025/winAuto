#pragma once

#include <QDialog>
#include <optional>

#include "WorkflowProposalParser.h"
#include "WorkflowGeneration.h"

class QComboBox;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;

class WorkflowGenerationDialog : public QDialog {
    Q_OBJECT
public:
    explicit WorkflowGenerationDialog(QWidget* parent = nullptr);
    ~WorkflowGenerationDialog() override;
    const std::optional<wa::Workflow>& acceptedWorkflow() const { return acceptedWorkflow_; }

private:
    void loadProviderSettings();
    void generate();
    void cancelOrClose();
    void showResult(const wa::llm::GenerationResult& result);
    void validateDraft();
    void acceptDraft();
    void showValidation(const wa::llm::WorkflowProposalParseResult& result);

    QComboBox* provider_ = nullptr;
    QLineEdit* model_ = nullptr;
    QLineEdit* apiKey_ = nullptr;
    QPlainTextEdit* task_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* generateButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* validateButton_ = nullptr;
    QPushButton* acceptButton_ = nullptr;
    wa::llm::WorkflowGeneration* generation_ = nullptr;
    QJsonObject currentProposal_;
    std::optional<wa::Workflow> acceptedWorkflow_;
};
