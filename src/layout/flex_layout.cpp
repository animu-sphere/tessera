#include <tessera/layout/layout_box.hpp>
#include <tessera/ui/property_metadata.hpp>
#include "../detail/layout_snapshot.hpp"
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
            along(box.size, horizontal) = limits(style, horizontal).clamp(
                fixed.kind == Dimension::Kind::points ? fixed.value : available);
        }
        place(0, no_layout_parent, box);
        if (!errors_.empty()) return {std::nullopt, std::move(errors_)};
        const auto finite = [](const Rect& r) {
            return std::isfinite(r.origin.x) && std::isfinite(r.origin.y) &&
                   std::isfinite(r.size.width) && std::isfinite(r.size.height);
        };
        const auto finite_scroll = [&](const ScrollGeometry& scroll) {
            return finite({scroll.offset, scroll.extent});
        };
        for (const auto& placed : result_.boxes) {
            if (!finite(placed.border_box) || (placed.clip && !finite(*placed.clip)) ||
                (placed.scroll && !finite_scroll(*placed.scroll))) {
                errors_.push_back({"non_finite_geometry", Severity::error, node_path(placed.node.index),
                                   "Layout overflowed float range; reduce sizes, margins, or gaps.", {}});
            }
        }
        if (!errors_.empty()) return {std::nullopt, std::move(errors_)};
        return {std::move(result_), {}};
    }

private:
    static std::string node_path(std::uint32_t index) { return "/nodes/" + std::to_string(index); }
    NodeHandle handle(std::uint32_t index) const { return {tree_.root().tree, index}; }

    std::vector<std::uint32_t> displayed_children(std::uint32_t index) const {
        std::vector<std::uint32_t> result;
        for (const auto child : tree_.children(handle(index)))
            if (styles_[child.index].display != Display::none) result.push_back(child.index);
        return result;
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
        } else {
            const bool row = style.direction == FlexDirection::row;
            const auto children = displayed_children(index);
            for (const auto child : children) {
                const auto& child_style = styles_[child];
                along(content, row) += preferred(child, row) + total(child_style.margin, row);
                along(content, !row) = std::max(along(content, !row),
                                                preferred(child, !row) + total(child_style.margin, !row));
            }
            if (!children.empty()) along(content, row) += style.gap * static_cast<float>(children.size() - 1);
        }
        Size size;
        for (bool horizontal : {true, false}) {
            const auto& fixed = dimension(style, horizontal);
            along(size, horizontal) = fixed.kind == Dimension::Kind::points
                ? fixed.value
                : along(content, horizontal) + total(style.border, horizontal) + total(style.padding, horizontal);
        }
        const float width = limits(style, true).clamp(border_width.value_or(size.width));
        const float content_width = inset(inset({{}, {width, 0}}, style.border), style.padding).size.width;
        if (style.height.kind == Dimension::Kind::automatic) {
            float height = 0;
            if (node.kind == NodeKind::text) {
                height = measure(index, node, content_width).height;
            } else {
                const auto children = displayed_children(index);
                const bool row = style.direction == FlexDirection::row;
                const auto widths = row ? flex(style, children, content_width, content_width) : std::vector<float>{};
                for (std::size_t i = 0; i < children.size(); ++i) {
                    const auto child = children[i];
                    const float child_width = row ? widths[i] : column_width(style, child, content_width);
                    const float extent = limits(styles_[child], false).clamp(basis(child, child_width).height) +
                                         total(styles_[child].margin, false);
                    height = row ? std::max(height, extent) : height + extent;
                }
                if (!row && !children.empty()) height += style.gap * static_cast<float>(children.size() - 1);
            }
            size.height = height + total(style.border, false) + total(style.padding, false);
        }
        if (!border_width) basis_[index] = size;
        return size;
    }

    float preferred(std::uint32_t index, bool horizontal) {
        return limits(styles_[index], horizontal).clamp(along(basis(index), horizontal));
    }

    float column_width(const ResolvedStyle& parent, std::uint32_t child, float available) {
        const auto& style = styles_[child];
        return parent.align == Align::stretch && style.width.kind == Dimension::Kind::automatic
            ? limits(style, true).clamp(available - total(style.margin, true)) : preferred(child, true);
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
        auto clip = parent == no_layout_parent ? std::nullopt :
            detail::descendant_clip(result_.boxes[parent], styles_[result_.boxes[parent].node.index].overflow);
        result_.boxes.push_back({handle(index), parent, border_box, style.border, style.padding,
                                 style.visibility == Visibility::visible, std::move(clip), std::nullopt});
        if (style.overflow == Overflow::scroll) result_.boxes[at].scroll = ScrollGeometry{};
        const auto content = result_.boxes[at].content_box();
        if (tree_.get(handle(index))->kind == NodeKind::text)
            measure(index, *tree_.get(handle(index)), content.size.width);
        const auto children = displayed_children(index);
        if (children.empty()) {
            if (style.overflow == Overflow::scroll) scroll(at, {}, {});
            return;
        }

        const bool row = style.direction == FlexDirection::row;
        const auto sizes = flex(style, children, along(content.size, row), content.size.width);
        // Space left after items, margins, and gaps; overflow is never redistributed backwards.
        float leftover = along(content.size, row) - style.gap * static_cast<float>(children.size() - 1);
        for (std::size_t i = 0; i < children.size(); ++i)
            leftover -= sizes[i] + total(styles_[children[i]].margin, row);
        leftover = std::max(0.0f, leftover);
        float cursor = along(content.origin, row);
        float spacing = style.gap;
        if (style.justify == Justify::center) cursor += leftover / 2;
        else if (style.justify == Justify::end) cursor += leftover;
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
            const bool stretch = style.align == Align::stretch && dimension(child, !row).kind == Dimension::Kind::automatic;
            const float natural_cross = row
                ? limits(child, false).clamp(basis(children[i], sizes[i]).height) : preferred(children[i], true);
            const float cross = stretch ? limits(child, !row).clamp(room) : natural_cross;
            const float free = std::max(0.0f, room - cross);
            float offset = 0;
            if (style.align == Align::center) offset = free / 2;
            else if (style.align == Align::end) offset = free;
            along(boxes[i].origin, !row) = along(content.origin, !row) +
                                            leading(child.margin, !row) + offset;
            along(boxes[i].size, !row) = cross;
        }
        if (style.overflow == Overflow::scroll) scroll(at, children, boxes);
        for (std::size_t i = 0; i < children.size(); ++i) place(children[i], at, boxes[i]);
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

    // Resolves main-axis border-box sizes for one single-line container: grow distributes positive free
    // space by grow factor, shrink removes overflow by shrink factor times basis, and limit violations
    // freeze items and redistribute until every item satisfies its limits.
    std::vector<float> flex(const ResolvedStyle& style, const std::vector<std::uint32_t>& children,
                            float available, float content_width) {
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
            const float base = along(basis(child, row ? std::nullopt :
                std::optional<float>{column_width(style, child, content_width)}), row);
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
                                     (violation < 0 && item.target < item.unclamped))) item.frozen = true;
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
