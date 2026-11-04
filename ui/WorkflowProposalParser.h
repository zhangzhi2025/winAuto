#pragma once

#include <QJsonObject>
#include <QString>

#include "core/Json.h"
#include "core/Validator.h"

namespace wa::llm {

struct WorkflowProposalParseResult {
    Workflow workflow;
    ValidationReport validation;
    QString error;
    bool valid = false;
};

WorkflowProposalParseResult parseWorkflowProposal(const QJsonObject& proposal);

}  // namespace wa::llm
