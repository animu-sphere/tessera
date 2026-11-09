#include <tessera/layout/layout_box.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "../detail/grid_placement.hpp"
#include "../detail/layout_snapshot.hpp"
#include "../detail/modal.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <variant>

namespace tessera {
namespace {

// Axis helpers: `horizontal` selects width/left/right, otherwise height/top/bottom.
float along(const Size& size, bool horizontal) { return horizontal ? size.width : size.height; }
float& along(Size& size, bool horizontal) { return horizontal ? size.width : size.height; }
float along(const Point& point, bool horizontal) { return horizontal ? point.x : point.y; }
float& along(Point& point, bool horizontal) { return horizontal ? point.x : point.y; }
float leading(const Edges& edges, bool horizontal) { return horizontal ? edges.left : edges.top; }
float trailing(const Edges& edges, bool horizontal) { return horizontal ? edges.right : edges.bottom; }
float total(const Edges& edges, bool horizontal) {
    return horizontal ? edges.left + edges.right : edges.top + edges.bottom;
}
const Dimension& dimension(const ResolvedStyle& style, bool horizontal) {
    return horizontal ? style.width : style.height;
}

// Border-box limits on one axis. A box never shrinks below its border plus padding.
struct Limits {
    float min = 0;
    float max = 0;
    float clamp(float value) const { return std::min(std::max(value, min), max); }
};
Limits limits(const ResolvedStyle& style, bool horizontal) {
    const float floor = total(style.border, horizontal) + total(style.padding, horizontal);
    const float min = std::max(horizontal ? style.min_width : style.min_height, floor);
    return {min, std::max(horizontal ? style.max_width : style.max_height, min)};
}

class Engine {
public:
    explicit Engine(const LayoutInput& input)
        : tree_(*input.tree), styles_(input.styles), offsets_(input.scroll_offsets), text_(*input.text),
          basis_(tree_.size()) {}

    Result<LayoutResult> run(Size viewport) {
        const auto& style = styles_[0];
        if (style.display == Display::none) return {std::move(result_), {}};
        Rect box{{style.margin.left, style.margin.top}, {}};
        for (bool horizontal : {true, false}) {
            const auto& fixed = dimension(style, horizontal);
            const float available = along(viewport, horizontal) - total(style.margin, horizontal);
            along(box.size, horizontal) =
                limits(style, horizontal).clamp(fixed.kind == Dimension::Kind::points ? fixed.value : available);
        }
        place(0, no_layout_parent, box);
        place_popups(viewport);
        if (!errors_.empty()) return {std::nullopt, std::move(errors_)};
        const auto finite = [](const Rect& r) {
            return std::isfinite(r.origin.x) && std::isfinite(r.origin.y) && std::isfinite(r.size.width) &&
                   std::isfinite(r.size.height);
        };
        const auto finite_scroll = [&](const ScrollGeometry& scroll) { return finite({scroll.offset, scroll.extent}); };
        for (const auto& placed : result_.boxes) {
            if (!finite(placed.border_box) || (placed.clip && !finite(*placed.clip)) ||
                (placed.scroll && !finite_scroll(*placed.scroll))) {
                errors_.push_back({"non_finite_geometry",
                                   Severity::error,
                                   node_path(placed.node.index),
                                   "Layout overflowed float range; reduce sizes, margins, or gaps.",
                                   {}});
            }
        }
        if (!errors_.empty()) return {std::nullopt, std::move(errors_)};
        return {std::move(result_), {}};
    }

private:
    static std::string node_path(std::uint32_t index) { return "/nodes/" + std::to_string(index); }
    NodeHandle handle(std::uint32_t index) const { return {tree_.root().tree, index}; }

    // Displayed children that the parent places itself: flow children and absolute children, in tree order.
    std::vector<std::uint32_t> placed_children(std::uint32_t index) const {
        std::vector<std::uint32_t> result;
        for (const auto child : tree_.children(handle(index)))
            if (styles_[child.index].display != Display::none && !detail::root_layer(*tree_.get(child)))
                result.push_back(child.index);
        return result;
    }
    bool absolute(std::uint32_t index) const { return styles_[index].position == Position::absolute; }
    // Children taking part in Flex/Stack/Grid flow, intrinsic sizes and Grid cells.
    std::vector<std::uint32_t> displayed_children(std::uint32_t index) const {
        auto result = placed_children(index);
        std::erase_if(result, [&](std::uint32_t child) { return absolute(child); });
        return result;
    }

    void place_popups(Size viewport) {
        const auto boundary = intersect({{}, viewport}, result_.boxes.front().content_box());
        for (const auto child : tree_.children(tree_.root())) {
            const auto& node = *tree_.get(child);
            if (detail::modal(node)) {
                if (styles_[child.index].display != Display::none) place(child.index, 0, boundary);
                continue;
            }
            if (!detail::popup_anchor(node) || styles_[child.index].display == Display::none) continue;
            const auto* anchor_box = detail::popup_anchor_box(tree_, result_, node);
            if (!anchor_box) continue;
            const auto anchor = anchor_box->border_box;
            const auto& style = styles_[child.index];
            const auto& side = std::get<std::string>(*effective_property(node, property_names::popup_side));
            const bool horizontal = side == "left" || side == "right";
            // A matched bottom/top popup is at least as wide as its anchor's border box.
            const bool match = !horizontal && !node.properties.contains(property_names::popup_x) &&
                               std::get<bool>(*effective_property(node, property_names::popup_match_width));
            const float width =
                std::min(std::max(preferred(child.index, true), match ? anchor.size.width : 0.f), boundary.size.width);
            const Size size{limits(style, true).clamp(width),
                            limits(style, false)
                                .clamp(std::min(boundary.size.height,
                                                basis(child.index, limits(style, true).clamp(width)).height))};
            if (const auto* x = effective_property(node, property_names::popup_x)) {
                const double left = boundary.origin.x;
                const double top = boundary.origin.y;
                const Point origin{
                    static_cast<float>(std::clamp(std::get<double>(*x), left,
                                                  std::max(left, left + boundary.size.width - size.width))),
                    static_cast<float>(std::clamp(std::get<double>(*effective_property(node, property_names::popup_y)),
                                                  top, std::max(top, top + boundary.size.height - size.height)))};
                place(child.index, 0, {origin, size});
                continue;
            }
            bool after = side == "bottom" || side == "right";
            const double gap = std::get<double>(*effective_property(node, property_names::popup_gap));
            const double start = along(boundary.origin, horizontal);
            const double end = start + along(boundary.size, horizontal);
            const double anchor_start = along(anchor.origin, horizontal);
            const double anchor_end = anchor_start + along(anchor.size, horizontal);
            const double before_room = anchor_start - gap - start;
            const double after_room = end - anchor_end - gap;
            const double needed = along(size, horizontal);
            if ((after ? after_room : before_room) < needed &&
                (after ? before_room > after_room : after_room > before_room))
                after = !after;
            Point origin = anchor.origin;
            const double main = after ? anchor_end + gap : anchor_start - gap - needed;
            along(origin, horizontal) = static_cast<float>(std::clamp(main, start, std::max(start, end - needed)));
            const double cross_start = along(boundary.origin, !horizontal);
            along(origin, !horizontal) = static_cast<float>(std::clamp(
                static_cast<double>(along(origin, !horizontal)), cross_start,
                std::max(cross_start, cross_start + along(boundary.size, !horizontal) - along(size, !horizontal))));
            place(child.index, 0, {origin, size});
        }
    }

    // Unclamped border-box size before flexing: a fixed dimension, else content plus border and padding.
    // Unconstrained bases are cached. Assigned widths remeasure text and descendant heights.
    Size basis(std::uint32_t index, std::optional<float> border_width = std::nullopt) {
        if (!border_width && basis_[index]) return *basis_[index];
        const auto& style = styles_[index];
        const UiNode& node = *tree_.get(handle(index));
        Size content;
        if (node.kind == NodeKind::text) {
            content = measure(index, node);
        } else if (style.display == Display::grid) {
            const auto children = displayed_children(index);
            const auto cells = grid_cells(style, children);
            const auto columns = grid_axis(style, children, cells, true, std::nullopt);
            content.width = track_extent(columns, style.column_gap);
            content.height =
                track_extent(grid_axis(style, children, cells, false, std::nullopt, columns), style.row_gap);
        } else {
            const bool row = style.direction == FlexDirection::row;
            const auto children = displayed_children(index);
            for (const auto child : children) {
                const auto& child_style = styles_[child];
                const float extent = preferred(child, row) + total(child_style.margin, row);
                along(content, row) = style.display == Display::stack ? std::max(along(content, row), extent)
                                                                      : along(content, row) + extent;
                along(content, !row) =
                    std::max(along(content, !row), preferred(child, !row) + total(child_style.margin, !row));
            }
            if (style.display != Display::stack && !children.empty())
                along(content, row) += style.gap * static_cast<float>(children.size() - 1);
        }
        Size size;
        for (bool horizontal : {true, false}) {
            const auto& fixed = dimension(style, horizontal);
            along(size, horizontal) =
                fixed.kind == Dimension::Kind::points
                    ? fixed.value
                    : along(content, horizontal) + total(style.border, horizontal) + total(style.padding, horizontal);
        }
        const float width = limits(style, true).clamp(border_width.value_or(size.width));
        const float content_width = inset(inset({{}, {width, 0}}, style.border), style.padding).size.width;
        if (style.height.kind == Dimension::Kind::automatic) {
            float height = 0;
            if (node.kind == NodeKind::text) {
                height = measure(index, node, content_width).height;
            } else if (style.display == Display::grid) {
                const auto children = displayed_children(index);
                const auto cells = grid_cells(style, children);
                const auto columns = grid_axis(style, children, cells, true, content_width);
                height = track_extent(grid_axis(style, children, cells, false, std::nullopt, columns), style.row_gap);
            } else if (wraps(style)) {
                const auto heights =
                    line_heights(style, flex_lines(style, displayed_children(index), content_width), content_width);
                height = track_extent(heights, style.gap);
            } else {
                const auto children = displayed_children(index);
                const bool stack = style.display == Display::stack;
                const bool row = style.direction == FlexDirection::row;
                const auto widths =
                    row && !stack ? flex(style, children, content_width, content_width) : std::vector<float>{};
                for (std::size_t i = 0; i < children.size(); ++i) {
                    const auto child = children[i];
                    const float child_width = stack
                                                  ? aligned_width(styles_[child].stack_horizontal, child, content_width)
                                              : row ? widths[i]
                                                    : aligned_width(style.align, child, content_width);
                    const float extent = limits(styles_[child], false).clamp(basis(child, child_width).height) +
                                         total(styles_[child].margin, false);
                    height = row || stack ? std::max(height, extent) : height + extent;
                }
                if (!row && !stack && !children.empty()) height += style.gap * static_cast<float>(children.size() - 1);
            }
            size.height = height + total(style.border, false) + total(style.padding, false);
        }
        if (!border_width) basis_[index] = size;
        return size;
    }

    float preferred(std::uint32_t index, bool horizontal) {
        return limits(styles_[index], horizontal).clamp(along(basis(index), horizontal));
    }

    float aligned_width(Align align, std::uint32_t child, float available) {
        const auto& style = styles_[child];
        return align == Align::stretch && style.width.kind == Dimension::Kind::automatic
                   ? limits(style, true).clamp(available - total(style.margin, true))
                   : preferred(child, true);
    }

    static bool wraps(const ResolvedStyle& style) {
        return style.display == Display::flex && style.direction == FlexDirection::row &&
               style.flex_wrap == FlexWrap::wrap;
    }

    // Breaks a wrapping row's children into lines in tree order. A child starts a new line when its clamped basis
    // plus margins, after the gap, would pass the content width; every line holds at least one child.
    std::vector<std::vector<std::uint32_t>> flex_lines(const ResolvedStyle& style,
                                                       const std::vector<std::uint32_t>& children, float available) {
        std::vector<std::vector<std::uint32_t>> lines;
        float used = 0;
        for (const auto child : children) {
            const float outer = preferred(child, true) + total(styles_[child].margin, true);
            if (lines.empty() || used + style.gap + outer > available) {
                lines.emplace_back();
                used = outer;
            } else {
                used += style.gap + outer;
            }
            lines.back().push_back(child);
        }
        return lines;
    }

    // Each line's cross size: its largest child margin-box height at the widths flexed within that line.
    std::vector<float> line_heights(const ResolvedStyle& style, const std::vector<std::vector<std::uint32_t>>& lines,
                                    float content_width) {
        std::vector<float> heights;
        heights.reserve(lines.size());
        for (const auto& line : lines) {
            const auto widths = flex(style, line, content_width, content_width);
            float height = 0;
            for (std::size_t i = 0; i < line.size(); ++i) {
                const auto& child = styles_[line[i]];
                height = std::max(height, limits(child, false).clamp(basis(line[i], widths[i]).height) +
                                              total(child.margin, false));
            }
            heights.push_back(height);
        }
        return heights;
    }

    static float track_extent(std::span<const float> sizes, float gap) {
        if (sizes.empty()) return 0;
        float result = gap * static_cast<float>(sizes.size() - 1);
        for (const float size : sizes) result += size;
        return result;
    }

    std::vector<detail::GridCell> grid_cells(const ResolvedStyle& style, const std::vector<std::uint32_t>& children) {
        std::vector<const ResolvedStyle*> items;
        items.reserve(children.size());
        for (const auto child : children) items.push_back(&styles_[child]);
        // Validation guarantees every item fits.
        return detail::place_grid(style.grid_columns.size(), style.grid_rows.size(), items).cells;
    }

    static float span_extent(std::span<const float> sizes, std::size_t first, std::size_t count, float gap) {
        float result = gap * static_cast<float>(count - 1);
        for (std::size_t i = first; i < first + count; ++i) result += sizes[i];
        return result;
    }

    std::vector<float> grid_axis(const ResolvedStyle& style, const std::vector<std::uint32_t>& children,
                                 const std::vector<detail::GridCell>& cells, bool horizontal,
                                 std::optional<float> available, std::span<const float> columns = {}) {
        const auto& tracks = horizontal ? style.grid_columns : style.grid_rows;
        const float gap = horizontal ? style.column_gap : style.row_gap;
        const auto first = [&](const detail::GridCell& cell) { return horizontal ? cell.column : cell.row; };
        const auto span = [&](const detail::GridCell& cell) { return horizontal ? cell.column_span : cell.row_span; };
        // Automatic tracks, and fractional tracks without an assigned axis size, size to their items.
        const auto sized = [&](std::size_t track) {
            return tracks[track].kind == GridTrack::Kind::automatic ||
                   (!available && tracks[track].kind == GridTrack::Kind::fraction);
        };
        const auto contribution = [&](std::size_t i) {
            const auto child = children[i];
            const auto& cell = cells[i];
            const float size =
                horizontal ? preferred(child, true)
                           : limits(styles_[child], false)
                                 .clamp(basis(child, aligned_width(styles_[child].grid_horizontal, child,
                                                                   span_extent(columns, cell.column, cell.column_span,
                                                                               style.column_gap)))
                                            .height);
            return size + total(styles_[child].margin, horizontal);
        };
        std::vector<float> sizes(tracks.size(), 0);
        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto track = first(cells[i]);
            if (span(cells[i]) == 1 && sized(track)) sizes[track] = std::max(sizes[track], contribution(i));
        }
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            const auto& track = tracks[i];
            sizes[i] = std::clamp(track.kind == GridTrack::Kind::points ? track.value : sizes[i], track.minimum,
                                  track.maximum);
        }
        // Spanning items, fewest tracks first, add any excess over their spanned extent in equal shares to the
        // content-sized tracks they cross; capped tracks freeze and their unused share is redistributed. An item
        // crossing a fractional track on an assigned axis leaves the excess to that track.
        std::vector<std::size_t> spanning;
        for (std::size_t i = 0; i < children.size(); ++i)
            if (span(cells[i]) > 1) spanning.push_back(i);
        std::stable_sort(spanning.begin(), spanning.end(),
                         [&](std::size_t a, std::size_t b) { return span(cells[a]) < span(cells[b]); });
        for (const auto i : spanning) {
            const auto start = first(cells[i]);
            const auto count = span(cells[i]);
            std::vector<std::size_t> growable;
            bool flexible = false;
            for (std::size_t track = start; track < start + count; ++track) {
                if (available && tracks[track].kind == GridTrack::Kind::fraction) flexible = true;
                if (sized(track)) growable.push_back(track);
            }
            if (flexible || growable.empty()) continue;
            double excess = static_cast<double>(contribution(i)) - span_extent(sizes, start, count, gap);
            while (excess > 0 && !growable.empty()) {
                const double share = excess / static_cast<double>(growable.size());
                const auto capped = std::find_if(growable.begin(), growable.end(), [&](std::size_t track) {
                    return static_cast<double>(sizes[track]) + share >= tracks[track].maximum;
                });
                if (capped != growable.end()) {
                    excess -= static_cast<double>(tracks[*capped].maximum) - sizes[*capped];
                    sizes[*capped] = tracks[*capped].maximum;
                    growable.erase(capped);
                    continue;
                }
                for (const auto track : growable) sizes[track] = static_cast<float>(sizes[track] + share);
                break;
            }
        }
        std::vector<bool> flexible(tracks.size(), false);
        double free = available.value_or(0) - static_cast<double>(gap) * (tracks.size() - 1);
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            flexible[i] = available && tracks[i].kind == GridTrack::Kind::fraction;
            free -= sizes[i];
        }
        // Fractional tracks share positive space above their minima; capped tracks
        // freeze and their unused share is redistributed. Minima may overflow.
        while (available && free > 0) {
            double weight = 0;
            for (std::size_t i = 0; i < tracks.size(); ++i)
                if (flexible[i]) weight += tracks[i].value;
            if (weight == 0) break;
            bool capped = false;
            for (std::size_t i = 0; i < tracks.size(); ++i) {
                if (!flexible[i]) continue;
                const double share = free * tracks[i].value / weight;
                if (static_cast<double>(sizes[i]) + share >= tracks[i].maximum) {
                    free -= static_cast<double>(tracks[i].maximum) - sizes[i];
                    sizes[i] = tracks[i].maximum;
                    flexible[i] = false;
                    capped = true;
                    break;
                }
            }
            if (capped) continue;
            for (std::size_t i = 0; i < tracks.size(); ++i)
                if (flexible[i]) sizes[i] = static_cast<float>(sizes[i] + free * tracks[i].value / weight);
            break;
        }
        return sizes;
    }

    Size measure(std::uint32_t index, const UiNode& node, std::optional<float> width = std::nullopt) {
        const auto found = node.properties.find(property_names::text);
        const auto* text = found == node.properties.end() ? nullptr : std::get_if<std::string>(&found->second);
        auto metrics = text_.measure(text ? *text : std::string(), styles_[index].text, {width});
        if (metrics) return metrics.value->size;
        for (auto& diagnostic : metrics.diagnostics) {
            diagnostic.path = node_path(index) + diagnostic.path;
            errors_.push_back(std::move(diagnostic));
        }
        return {};
    }

    void place(std::uint32_t index, std::uint32_t parent, const Rect& border_box) {
        const auto& style = styles_[index];
        const auto at = static_cast<std::uint32_t>(result_.boxes.size());
        auto clip =
            parent == no_layout_parent
                ? std::nullopt
                : detail::descendant_clip(result_.boxes[parent], styles_[result_.boxes[parent].node.index].overflow);
        result_.boxes.push_back({handle(index), parent, border_box, style.border, style.padding,
                                 style.visibility == Visibility::visible, std::move(clip), std::nullopt});
        if (style.overflow == Overflow::scroll) result_.boxes[at].scroll = ScrollGeometry{};
        const auto content = result_.boxes[at].content_box();
        if (tree_.get(handle(index))->kind == NodeKind::text)
            measure(index, *tree_.get(handle(index)), content.size.width);
        const auto flow = displayed_children(index);
        const auto flow_boxes = flow.empty()                      ? std::vector<Rect>{}
                                : style.display == Display::grid  ? grid_boxes(style, flow, content)
                                : style.display == Display::stack ? stack_boxes(index, flow, content)
                                                                  : flex_boxes(style, flow, content);
        // Absolute children keep their tree position among the flow children they do not affect.
        const auto padding = result_.boxes[at].padding_box();
        const auto children = placed_children(index);
        std::vector<Rect> boxes;
        boxes.reserve(children.size());
        std::size_t next = 0;
        for (const auto child : children)
            boxes.push_back(absolute(child) ? absolute_box(child, padding) : flow_boxes[next++]);
        if (style.overflow == Overflow::scroll) scroll(at, children, boxes);
        for (std::size_t i = 0; i < children.size(); ++i) place(children[i], at, boxes[i]);
    }

    std::vector<Rect> grid_boxes(const ResolvedStyle& style, const std::vector<std::uint32_t>& children,
                                 const Rect& content) {
        const auto cells = grid_cells(style, children);
        const auto columns = grid_axis(style, children, cells, true, content.size.width);
        const auto rows = grid_axis(style, children, cells, false, content.size.height, columns);
        const auto starts = [](std::span<const float> sizes, float origin, float gap) {
            std::vector<float> result;
            for (const float size : sizes) {
                result.push_back(origin);
                origin += size + gap;
            }
            return result;
        };
        const auto xs = starts(columns, content.origin.x, style.column_gap);
        const auto ys = starts(rows, content.origin.y, style.row_gap);
        std::vector<Rect> boxes;
        boxes.reserve(children.size());
        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto child = children[i];
            const auto& child_style = styles_[child];
            const auto& cell = cells[i];
            const Size area{span_extent(columns, cell.column, cell.column_span, style.column_gap),
                            span_extent(rows, cell.row, cell.row_span, style.row_gap)};
            Rect box{{xs[cell.column], ys[cell.row]},
                     {aligned_width(child_style.grid_horizontal, child, area.width), 0}};
            box.size.height =
                child_style.grid_vertical == Align::stretch && child_style.height.kind == Dimension::Kind::automatic
                    ? limits(child_style, false).clamp(area.height - total(child_style.margin, false))
                    : limits(child_style, false).clamp(basis(child, box.size.width).height);
            for (bool horizontal : {true, false}) {
                const auto align = horizontal ? child_style.grid_horizontal : child_style.grid_vertical;
                const float free = std::max(0.f, along(area, horizontal) - total(child_style.margin, horizontal) -
                                                     along(box.size, horizontal));
                along(box.origin, horizontal) +=
                    leading(child_style.margin, horizontal) + (align == Align::center ? free / 2
                                                               : align == Align::end  ? free
                                                                                      : 0);
            }
            boxes.push_back(box);
        }
        return boxes;
    }

    std::vector<Rect> stack_boxes(std::uint32_t index, const std::vector<std::uint32_t>& children,
                                  const Rect& content) {
        std::vector<Rect> boxes;
        boxes.reserve(children.size());
        const bool bounded = detail::modal(*tree_.get(handle(index)));
        for (const auto child : children) {
            const auto& child_style = styles_[child];
            Rect box{{}, {aligned_width(child_style.stack_horizontal, child, content.size.width), 0}};
            if (bounded && child_style.width.kind == Dimension::Kind::automatic)
                box.size.width =
                    limits(child_style, true)
                        .clamp(std::min(box.size.width,
                                        std::max(0.f, content.size.width - total(child_style.margin, true))));
            const float room = content.size.height - total(child_style.margin, false);
            box.size.height =
                child_style.stack_vertical == Align::stretch && child_style.height.kind == Dimension::Kind::automatic
                    ? limits(child_style, false).clamp(room)
                    : limits(child_style, false).clamp(basis(child, box.size.width).height);
            if (bounded && child_style.height.kind == Dimension::Kind::automatic)
                box.size.height = limits(child_style, false).clamp(std::min(box.size.height, std::max(0.f, room)));
            for (bool horizontal : {true, false}) {
                const auto align = horizontal ? child_style.stack_horizontal : child_style.stack_vertical;
                const float free =
                    std::max(0.0f, along(content.size, horizontal) - total(child_style.margin, horizontal) -
                                       along(box.size, horizontal));
                const float offset = align == Align::center ? free / 2 : align == Align::end ? free : 0;
                along(box.origin, horizontal) =
                    along(content.origin, horizontal) + leading(child_style.margin, horizontal) + offset;
            }
            boxes.push_back(box);
        }
        return boxes;
    }

    // A wrapping row stacks its lines from the content top, separated by the gap. Positive cross space left after
    // the lines and gaps is shared equally among them; each line then places its children like a single-line row.
    std::vector<Rect> flex_boxes(const ResolvedStyle& style, const std::vector<std::uint32_t>& children,
                                 const Rect& content) {
        if (!wraps(style)) return line_boxes(style, children, content);
        const auto lines = flex_lines(style, children, content.size.width);
        const auto heights = line_heights(style, lines, content.size.width);
        const float extra =
            std::max(0.0f, content.size.height - track_extent(heights, style.gap)) / static_cast<float>(lines.size());
        std::vector<Rect> boxes;
        boxes.reserve(children.size());
        float top = content.origin.y;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const Rect band{{content.origin.x, top}, {content.size.width, heights[i] + extra}};
            for (const auto& box : line_boxes(style, lines[i], band)) boxes.push_back(box);
            top += band.size.height + style.gap;
        }
        return boxes;
    }

    // Places one line of children within `content`, its main extent and cross band.
    std::vector<Rect> line_boxes(const ResolvedStyle& style, const std::vector<std::uint32_t>& children,
                                 const Rect& content) {
        const bool row = style.direction == FlexDirection::row;
        const auto sizes = flex(style, children, along(content.size, row), content.size.width);
        // Space left after items, margins, and gaps; overflow is never redistributed backwards.
        float leftover = along(content.size, row) - style.gap * static_cast<float>(children.size() - 1);
        for (std::size_t i = 0; i < children.size(); ++i)
            leftover -= sizes[i] + total(styles_[children[i]].margin, row);
        leftover = std::max(0.0f, leftover);
        float cursor = along(content.origin, row);
        float spacing = style.gap;
        if (style.justify == Justify::center)
            cursor += leftover / 2;
        else if (style.justify == Justify::end)
            cursor += leftover;
        else if (style.justify == Justify::space_between && children.size() > 1)
            spacing += leftover / static_cast<float>(children.size() - 1);

        std::vector<Rect> boxes(children.size());
        const float cross_space = along(content.size, !row);
        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto& child = styles_[children[i]];
            cursor += leading(child.margin, row);
            along(boxes[i].origin, row) = cursor;
            along(boxes[i].size, row) = sizes[i];
            cursor += sizes[i] + total(child.margin, row) - leading(child.margin, row) + spacing;

            const float room = cross_space - total(child.margin, !row);
            const bool stretch =
                style.align == Align::stretch && dimension(child, !row).kind == Dimension::Kind::automatic;
            const float natural_cross =
                row ? limits(child, false).clamp(basis(children[i], sizes[i]).height) : preferred(children[i], true);
            const float cross = stretch ? limits(child, !row).clamp(room) : natural_cross;
            const float free = std::max(0.0f, room - cross);
            float offset = 0;
            if (style.align == Align::center)
                offset = free / 2;
            else if (style.align == Align::end)
                offset = free;
            along(boxes[i].origin, !row) = along(content.origin, !row) + leading(child.margin, !row) + offset;
            along(boxes[i].size, !row) = cross;
        }
        return boxes;
    }

    // Places an absolute child against its parent's padding box. An automatic axis with both offsets fills the
    // space between them; otherwise width is the natural width bounded by the remaining space, and height is
    // measured at that width. A leading offset wins over a trailing one; neither starts at the leading edge.
    Rect absolute_box(std::uint32_t child, const Rect& container) {
        const auto& style = styles_[child];
        Rect box;
        for (bool horizontal : {true, false}) {
            const auto& start = horizontal ? style.left : style.top;
            const auto& end = horizontal ? style.right : style.bottom;
            const auto bounds = limits(style, horizontal);
            const bool automatic = dimension(style, horizontal).kind == Dimension::Kind::automatic;
            const float room = along(container.size, horizontal) - start.value_or(0) - end.value_or(0) -
                               total(style.margin, horizontal);
            float size = 0;
            if (automatic && start && end)
                size = bounds.clamp(room);
            else if (horizontal)
                size = automatic ? bounds.clamp(std::min(preferred(child, true), std::max(0.f, room)))
                                 : preferred(child, true);
            else
                size = bounds.clamp(basis(child, box.size.width).height);
            along(box.size, horizontal) = size;
            const float origin = along(container.origin, horizontal);
            along(box.origin, horizontal) =
                start ? origin + *start + leading(style.margin, horizontal)
                : end ? origin + along(container.size, horizontal) - *end - trailing(style.margin, horizontal) - size
                      : origin + leading(style.margin, horizontal);
        }
        return box;
    }

    // Records a scroll box's extent (its padding box grown to cover each child's margin box plus the box's
    // trailing padding), clamps the requested offset into [0, extent - viewport], and moves the children's
    // border boxes back by the applied offset before they are placed.
    void scroll(std::uint32_t at, std::span<const std::uint32_t> children, std::span<Rect> boxes) {
        auto& box = result_.boxes[at];
        const auto viewport = box.padding_box();
        const auto& style = styles_[box.node.index];
        auto& geometry = *box.scroll;
        for (bool horizontal : {true, false}) {
            float extent = along(viewport.size, horizontal);
            for (std::size_t i = 0; i < children.size(); ++i) {
                const float end = along(boxes[i].origin, horizontal) + along(boxes[i].size, horizontal) +
                                  trailing(styles_[children[i]].margin, horizontal) +
                                  trailing(style.padding, horizontal);
                extent = std::max(extent, end - along(viewport.origin, horizontal));
            }
            along(geometry.extent, horizontal) = extent;
        }
        const auto limit = box.scroll_limit();
        const auto requested = offsets_.empty() ? Point{} : offsets_[box.node.index];
        for (bool horizontal : {true, false}) {
            const float offset = std::min(std::max(along(requested, horizontal), 0.0f), along(limit, horizontal));
            along(geometry.offset, horizontal) = offset;
            for (auto& rect : boxes) along(rect.origin, horizontal) -= offset;
        }
    }

    // Resolves main-axis border-box sizes for one flex line: grow distributes positive free
    // space by grow factor, shrink removes overflow by shrink factor times basis, and limit violations
    // freeze items and redistribute until every item satisfies its limits.
    std::vector<float> flex(const ResolvedStyle& style, const std::vector<std::uint32_t>& children, float available,
                            float content_width) {
        const bool row = style.direction == FlexDirection::row;
        struct Item {
            float basis, target, unclamped, factor;
            Limits limits;
            bool frozen;
        };
        std::vector<Item> items;
        float space = available - style.gap * static_cast<float>(children.size() - 1);
        float hypothetical = 0;
        for (const auto child : children) {
            const auto& child_style = styles_[child];
            const auto bounds = limits(child_style, row);
            const float base =
                along(basis(child, row ? std::nullopt
                                       : std::optional<float>{aligned_width(style.align, child, content_width)}),
                      row);
            items.push_back({base, bounds.clamp(base), base, 0, bounds, false});
            space -= total(child_style.margin, row);
            hypothetical += items.back().target;
        }
        const bool growing = hypothetical < space;
        for (std::size_t i = 0; i < items.size(); ++i) {
            auto& item = items[i];
            const auto& child_style = styles_[children[i]];
            item.factor = growing ? child_style.grow : child_style.shrink * item.basis;
            item.frozen = item.factor == 0 || (growing ? item.basis > item.target : item.basis < item.target);
        }
        for (;;) {
            float free = space;
            float factors = 0;
            for (const auto& item : items) {
                free -= item.frozen ? item.target : item.basis;
                if (!item.frozen) factors += item.factor;
            }
            if (factors == 0) break; // Every item is frozen or has no remaining weight.
            float violation = 0;
            for (auto& item : items) {
                if (item.frozen) continue;
                item.unclamped = item.basis + free * item.factor / factors;
                item.target = item.limits.clamp(item.unclamped);
                violation += item.target - item.unclamped;
            }
            for (auto& item : items) {
                if (!item.frozen && (violation == 0 || (violation > 0 && item.target > item.unclamped) ||
                                     (violation < 0 && item.target < item.unclamped)))
                    item.frozen = true;
            }
        }
        std::vector<float> sizes;
        for (const auto& item : items) sizes.push_back(item.target);
        return sizes;
    }

    const UiTree& tree_;
    std::span<const ResolvedStyle> styles_;
    std::span<const Point> offsets_;
    TextShaper& text_;
    std::vector<std::optional<Size>> basis_;
    std::vector<Diagnostic> errors_;
    LayoutResult result_;
};

} // namespace

Result<LayoutResult> compute_layout(const LayoutInput& input) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    return Engine(input).run(input.viewport);
}

} // namespace tessera
