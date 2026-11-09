#pragma once

#include <tessera/style/resolved_style.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace tessera::detail {

struct GridCell {
    std::size_t column = 0;
    std::size_t row = 0;
    std::size_t column_span = 1;
    std::size_t row_span = 1;
};

struct GridPlacement {
    std::vector<GridCell> cells;       // One per placed item, in item order.
    std::optional<std::size_t> failed; // First item that has no free area; later items are not placed.
};

// Sparse row-major auto placement: each item takes the first free area at or after the cursor, which then
// advances past the item. Earlier holes are never backfilled, and items never create implicit tracks.
inline GridPlacement place_grid(std::size_t columns, std::size_t rows, std::span<const ResolvedStyle* const> items) {
    GridPlacement result;
    std::vector<bool> taken(columns * rows, false);
    std::size_t cursor = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::size_t width = items[i]->grid_column_span;
        const std::size_t height = items[i]->grid_row_span;
        std::optional<GridCell> found;
        if (width >= 1 && height >= 1 && width <= columns && height <= rows) {
            for (std::size_t at = cursor; at < taken.size() && !found; ++at) {
                const auto row = at / columns;
                const auto column = at % columns;
                if (row + height > rows) break;
                if (column + width > columns) continue;
                bool free = true;
                for (std::size_t r = row; r < row + height && free; ++r)
                    for (std::size_t c = column; c < column + width && free; ++c) free = !taken[r * columns + c];
                if (free) found = GridCell{column, row, width, height};
            }
        }
        if (!found) {
            result.failed = i;
            return result;
        }
        for (std::size_t r = found->row; r < found->row + height; ++r)
            for (std::size_t c = found->column; c < found->column + width; ++c) taken[r * columns + c] = true;
        cursor = found->row * columns + found->column + width;
        result.cells.push_back(*found);
    }
    return result;
}

} // namespace tessera::detail
