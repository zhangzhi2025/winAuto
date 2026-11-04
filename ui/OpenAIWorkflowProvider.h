#pragma once

#include <QByteArray>
#include <QNetworkRequest>

#include "WorkflowGeneration.h"

namespace wa::llm {

class OpenAIWorkflowProvider {
public:
    static QNetworkRequest createRequest(const GenerationRequest& request, QByteArray* body);
    static GenerationResult parseResponse(const QByteArray& response);
};

}  // namespace wa::llm
