#pragma once

#include <memory>

#include "StockMonitorService.h"

namespace stock_monitor {

class RapidOcrAdapter final : public OcrProvider {
public:
    RapidOcrAdapter();
    ~RapidOcrAdapter() override;

    RapidOcrAdapter(const RapidOcrAdapter&) = delete;
    RapidOcrAdapter& operator=(const RapidOcrAdapter&) = delete;

    bool ready() const;
    const std::string& initializationError() const;
    OcrProviderResult recognize(const Image& image, const std::atomic_bool& cancelled) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace stock_monitor
