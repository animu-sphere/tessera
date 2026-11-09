#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/color.hpp>
#include <tessera/style/fill_shape.hpp>
#include <tessera/style/gradient.hpp>
#include <tessera/style/outline.hpp>
#include <tessera/style/procedural_fill.hpp>
#include <tessera/style/shadow.hpp>
#include <tessera/text/text.hpp>
#include <tessera/ui/document.hpp>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace tessera {

struct Dimension {
    enum class Kind : std::uint8_t { automatic, points };
    Kind kind = Kind::automatic;
    float value = 0; // Logical units when kind is points; ignored otherwise.
    static constexpr Dimension points(float value) noexcept { return {Kind::points, value}; }
    bool operator==(const Dimension&) const = default;
};

enum class Display : std::uint8_t { flex, none, stack, grid };
enum class Visibility : std::uint8_t { visible, hidden };
enum class FlexDirection : std::uint8_t { column, row };
// wrap: a row container breaks its children into lines; column containers ignore it.
enum class FlexWrap : std::uint8_t { no_wrap, wrap };
enum class Justify : std::uint8_t { start, center, end, space_between };
enum class Align : std::uint8_t { start, center, end, stretch };
enum class Overflow : std::uint8_t { visible, clip, scroll };
// absolute: removed from the parent's Flex/Stack/Grid flow and placed against its padding box.
enum class Position : std::uint8_t { flow, absolute };

struct GridTrack {
    enum class Kind : std::uint8_t { automatic, points, fraction };
    Kind kind = Kind::automatic;
    float value = 0;
    float minimum = 0;
    float maximum = std::numeric_limits<float>::infinity();
    static constexpr GridTrack points(float value) noexcept { return {Kind::points, value}; }
    static constexpr GridTrack fraction(float weight = 1, float minimum = 0,
                                        float maximum = std::numeric_limits<float>::infinity()) noexcept {
        return {Kind::fraction, weight, minimum, maximum};
    }
    bool operator==(const GridTrack&) const = default;
};
inline constexpr std::size_t max_grid_tracks = 256;
inline constexpr std::uint16_t max_grid_span = 256;

// Typed values after defaults, inheritance, rules, and local overrides have been applied.
// Default construction yields the primitive defaults.
struct ResolvedStyle {
    // Layout
    Display display = Display::flex;
    FlexDirection direction = FlexDirection::column;
    FlexWrap flex_wrap = FlexWrap::no_wrap;
    Justify justify = Justify::start;
    Align align = Align::stretch;
    // clip: descendants paint and hit only inside the padding box. scroll: clips the same way and moves
    // descendants by the box's layout scroll offset.
    Overflow overflow = Overflow::visible;
    Dimension width;
    Dimension height;
    float min_width = 0;
    float min_height = 0;
    float max_width = std::numeric_limits<float>::infinity();
    float max_height = std::numeric_limits<float>::infinity();
    Edges margin;
    Edges border;
    Edges padding;
    float gap = 0;
    float grow = 0;
    float shrink = 0;

    // Paint and interaction eligibility
    Visibility visibility = Visibility::visible;
    float opacity = 1;
    Color background;
    Color border_color;
    float corner_radius = 0;

    // Text
    TextStyle text;
    Color color{0, 0, 0, 1};

    // Placement within a Stack parent's content box; does not affect this node's children.
    Align stack_horizontal = Align::stretch;
    Align stack_vertical = Align::stretch;

    std::vector<GridTrack> grid_columns;
    std::vector<GridTrack> grid_rows;
    float column_gap = 0;
    float row_gap = 0;
    Align grid_horizontal = Align::stretch;
    Align grid_vertical = Align::stretch;
    // Consecutive tracks this node occupies within a Grid parent; does not affect its own children.
    std::uint16_t grid_column_span = 1;
    std::uint16_t grid_row_span = 1;

    // Absolute placement offsets from the parent's padding-box edges; empty is automatic.
    // Negative values extend past the edge. Ignored unless position is absolute.
    Position position = Position::flow;
    std::optional<float> left;
    std::optional<float> top;
    std::optional<float> right;
    std::optional<float> bottom;

    // Replaces background when present; not inherited.
    std::optional<Gradient> background_gradient;
    std::optional<BoxShadow> box_shadow;
    std::optional<Outline> outline;
    FillShape background_shape = FillShape::rectangle;
    bool background_antialias = false;
    // Requests a procedural fill inside the background shape; background/background_gradient
    // remain the fallback a backend draws when it lacks the shader. Not inherited.
    std::optional<ProceduralFill> background_procedural;

    bool operator==(const ResolvedStyle&) const = default;
};

// Diagnostic paths are `path` followed by "/<field>".
std::vector<Diagnostic> validate(const ResolvedStyle&, std::string_view path = "");

} // namespace tessera
