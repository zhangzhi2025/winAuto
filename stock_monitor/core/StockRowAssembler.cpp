#include "StockRowAssembler.h"

#include <algorithm>
#include <cmath>

namespace stock_monitor {
namespace {

float overlapRatio(const Rect& a, const Rect& b) {
    const int overlap = std::max(0, std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y));
    const int smallerHeight = std::min(a.height, b.height);
    return smallerHeight <= 0 ? 0.0F : static_cast<float>(overlap) / smallerHeight;
}

bool sameRow(const Rect& a, const Rect& b, const TableLayoutConfig& config) {
    return overlapRatio(a, b) >= config.minVerticalOverlap ||
           std::abs(a.centerY() - b.centerY()) <= config.maxCenterDistancePixels;
}

void addToRow(StockRowCandidate& row, const OcrTextBlock& block, int imageWidth,
              const TableLayoutConfig& config) {
    const float x = imageWidth > 0 ? static_cast<float>(block.bounds.centerX()) / imageWidth : 0.0F;
    if (x >= config.codeColumn.minX && x < config.codeColumn.maxX) {
        row.codeBlocks.push_back(block);
    } else if (x >= config.nameColumn.minX && x < config.nameColumn.maxX) {
        row.nameBlocks.push_back(block);
    } else {
        row.otherBlocks.push_back(block);
    }
    const int left = std::min(row.rowBounds.x, block.bounds.x);
    const int top = std::min(row.rowBounds.y, block.bounds.y);
    const int right = std::max(row.rowBounds.right(), block.bounds.right());
    const int bottom = std::max(row.rowBounds.bottom(), block.bounds.bottom());
    row.rowBounds = {left, top, right - left, bottom - top};
}

}  // namespace

RowAssemblyResult assembleRows(const std::vector<OcrTextBlock>& blocks,
                               int imageWidth,
                               const TableLayoutConfig& config) {
    RowAssemblyResult result;
    if (imageWidth <= 0 || !validate(config).valid) {
        result.unassignedBlocks = blocks;
        return result;
    }
    std::vector<OcrTextBlock> sorted = blocks;
    std::stable_sort(sorted.begin(), sorted.end(), [](const OcrTextBlock& a, const OcrTextBlock& b) {
        if (a.bounds.centerY() != b.bounds.centerY()) return a.bounds.centerY() < b.bounds.centerY();
        if (a.bounds.x != b.bounds.x) return a.bounds.x < b.bounds.x;
        return a.order < b.order;
    });

    for (const auto& block : sorted) {
        const long long right = static_cast<long long>(block.bounds.x) + block.bounds.width;
        if (block.bounds.empty() || block.bounds.x < 0 || block.bounds.y < 0 ||
            right > imageWidth) {
            result.unassignedBlocks.push_back(block);
            continue;
        }
        auto row = std::find_if(result.rows.rbegin(), result.rows.rend(),
            [&](const StockRowCandidate& candidate) {
                return sameRow(candidate.rowBounds, block.bounds, config);
            });
        if (row == result.rows.rend()) {
            StockRowCandidate candidate;
            candidate.rowBounds = block.bounds;
            result.rows.push_back(std::move(candidate));
            row = result.rows.rbegin();
        }
        addToRow(*row, block, imageWidth, config);
    }

    for (auto& row : result.rows) {
        const bool duplicateFields = row.codeBlocks.size() > 1 || row.nameBlocks.size() > 1;
        if (duplicateFields) {
            row.status = RowStatus::Ambiguous;
        } else if (row.codeBlocks.size() == 1 && row.nameBlocks.size() == 1) {
            row.status = RowStatus::Ready;
        } else {
            row.status = RowStatus::Incomplete;
        }
    }
    return result;
}

}  // namespace stock_monitor
