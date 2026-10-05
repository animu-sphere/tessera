#pragma once

#include <tessera/ui/document.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace tessera {

// Stable property identifiers. Each is also the JSON v1 member name under node `properties`.
namespace property_names {
inline constexpr std::string_view text = "text";
inline constexpr std::string_view focusable = "focusable";
inline constexpr std::string_view disabled = "disabled";
inline constexpr std::string_view labelled_by = "labelled_by";
} // namespace property_names

// Mirrors the `Property` alternatives; no coercion between types.
enum class PropertyType : std::uint8_t { boolean, number, string, reference };

// Node kinds as a bit set.
enum class NodeKinds : std::uint8_t { none = 0, box = 1 << 0, text = 1 << 1, all = box | text };
constexpr NodeKinds node_kinds(NodeKind kind) noexcept {
    return kind == NodeKind::box ? NodeKinds::box : NodeKinds::text;
}
constexpr bool contains(NodeKinds set, NodeKind kind) noexcept {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(node_kinds(kind))) != 0;
}

// Stages whose output can change when the property changes, as a bit set. Meaning is owned by styling.
enum class PropertyStages : std::uint8_t {
    none = 0,
    style = 1 << 0,
    layout = 1 << 1,
    paint = 1 << 2,
    input = 1 << 3,
    semantics = 1 << 4,
};
constexpr PropertyStages operator|(PropertyStages a, PropertyStages b) noexcept {
    return static_cast<PropertyStages>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr bool affects(PropertyStages set, PropertyStages stage) noexcept {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(stage)) != 0;
}

// Editor grouping only; it never changes validation or defaults.
enum class PropertyCategory : std::uint8_t { content, interaction, accessibility };

// Describes one authored property. Derived values (resolved style, layout, semantics) are not listed.
struct PropertyDescriptor {
    std::string_view name;             // Stable identifier and encoded name; not a display label.
    PropertyType type = PropertyType::boolean;
    NodeKinds accepted = NodeKinds::none; // Kinds that accept the property; others reject it.
    NodeKinds required = NodeKinds::none; // Subset of `accepted` where absence fails validation.
    std::optional<Property> absent_value; // Meaning of absence where not required; never materialized.
    PropertyStages stages = PropertyStages::none;
    PropertyCategory category = PropertyCategory::content;
};

// The complete property vocabulary, ordered by name. Descriptors have static storage duration and stay
// valid for the remainder of the program.
std::span<const PropertyDescriptor> property_descriptors() noexcept;
const PropertyDescriptor* find_property_descriptor(std::string_view name) noexcept;
PropertyType property_type(const Property&) noexcept;

// Authored value when present, otherwise the descriptor's absence value. Returns null for an unknown
// name or an absent property without one. The pointer borrows from `node` or the descriptor table.
const Property* effective_property(const UiNode& node, std::string_view name) noexcept;

} // namespace tessera
