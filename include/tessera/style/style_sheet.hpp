#pragma once

#include <tessera/style/resolved_style.hpp>
#include <tessera/ui/tree.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tessera {

// Text values a declaration sets; absent fields leave the value unchanged.
struct TextDeclarations {
    std::optional<FontId> font;
    std::optional<float> size;
    std::optional<std::optional<float>> line_height; // Set to an empty optional to select the shaper default.
    std::optional<std::uint16_t> weight;
    bool operator==(const TextDeclarations&) const = default;
};

// Values a rule or instance override sets, named like the ResolvedStyle fields they replace.
// Absent fields leave the value unchanged.
struct StyleDeclarations {
    std::optional<Display> display;
    std::optional<FlexDirection> direction;
    std::optional<Justify> justify;
    std::optional<Align> align;
    std::optional<Overflow> overflow;
    std::optional<Dimension> width;
    std::optional<Dimension> height;
    std::optional<float> min_width;
    std::optional<float> min_height;
    std::optional<float> max_width;
    std::optional<float> max_height;
    std::optional<Edges> margin;
    std::optional<Edges> border;
    std::optional<Edges> padding;
    std::optional<float> gap;
    std::optional<float> grow;
    std::optional<float> shrink;
    std::optional<Visibility> visibility;
    std::optional<float> opacity;
    std::optional<Color> background;
    std::optional<Color> border_color;
    std::optional<float> corner_radius;
    TextDeclarations text;
    std::optional<Color> color;
    bool operator==(const StyleDeclarations&) const = default;
};

// Copies every declared value into the style.
void apply(const StyleDeclarations&, ResolvedStyle&);
// Each declared value is checked as validate(ResolvedStyle) checks it, at the same "path/<field>".
std::vector<Diagnostic> validate(const StyleDeclarations&, std::string_view path = "");

// Interaction states a selector requires; every set state must match.
struct StyleStates {
    bool hover = false;
    bool active = false;
    bool focus = false;
    bool disabled = false;
    bool any() const noexcept { return hover || active || focus || disabled; }
    bool operator==(const StyleStates&) const = default;
};

// One subject (node type, one class, or author ID) plus required states. No combinators.
struct StyleSelector {
    enum class Kind : std::uint8_t { type, class_name, id };
    Kind kind = Kind::class_name;
    NodeKind type = NodeKind::box; // Kind::type only.
    std::string name;              // Kind::class_name and Kind::id only.
    StyleStates states;

    static StyleSelector of_type(NodeKind type, StyleStates states = {}) { return {Kind::type, type, {}, states}; }
    static StyleSelector of_class(std::string name, StyleStates states = {}) {
        return {Kind::class_name, NodeKind::box, std::move(name), states};
    }
    static StyleSelector of_id(std::string id, StyleStates states = {}) {
        return {Kind::id, NodeKind::box, std::move(id), states};
    }
    bool operator==(const StyleSelector&) const = default;
};

struct StyleRule {
    StyleSelector selector;
    StyleDeclarations declarations;
    bool operator==(const StyleRule&) const = default;
};

// Rules in source order. Base rules (no states) apply first in source order, then state rules in
// source order; later rules replace earlier values. Class-list order on nodes has no effect.
struct StyleSheet {
    std::vector<StyleRule> rules;
    bool operator==(const StyleSheet&) const = default;
};

std::vector<Diagnostic> validate(const StyleSheet&);

// Host-supplied interaction state, e.g. from PointerState::hovered/active and FocusDispatchResult.
// Hovered and active nodes match with their ancestors; focus matches the focused node only.
// No state matches a node that is disabled by itself or an ancestor.
struct InteractionState {
    std::vector<NodeHandle> hovered;
    std::vector<NodeHandle> active;
    std::optional<NodeHandle> focused;
    bool operator==(const InteractionState&) const = default;
};

// Borrowed for one resolution pass.
struct StyleInput {
    const UiTree* tree = nullptr;
    const StyleSheet* sheet = nullptr;
    InteractionState state;
    std::span<const StyleDeclarations> overrides; // Empty, or one instance override per node by NodeHandle::index.
};

std::vector<Diagnostic> validate(const StyleInput&);

// Full-tree resolution, one style per node by NodeHandle::index: primitive defaults, inherited
// text and color from the parent's resolved style, matching rules, then the node's override.
// Fails with validate() diagnostics, or with resolved-value conflicts under /nodes/<index>.
Result<std::vector<ResolvedStyle>> resolve_styles(const StyleInput&);

} // namespace tessera
