#include <tessera/ui/property_metadata.hpp>
#include <algorithm>
#include <array>
#include <type_traits>

namespace tessera {

template<PropertyType type>
using PropertyAlternative = std::variant_alternative_t<static_cast<std::size_t>(type), Property>;
static_assert(std::is_same_v<PropertyAlternative<PropertyType::boolean>, bool>);
static_assert(std::is_same_v<PropertyAlternative<PropertyType::number>, double>);
static_assert(std::is_same_v<PropertyAlternative<PropertyType::string>, std::string>);
static_assert(std::is_same_v<PropertyAlternative<PropertyType::reference>, NodeReference>);

std::span<const PropertyDescriptor> property_descriptors() noexcept {
    using enum PropertyStages;
    static const std::array<PropertyDescriptor, 4> descriptors{{
        {property_names::disabled, PropertyType::boolean, NodeKinds::all, NodeKinds::none, Property{false},
         input | semantics, PropertyCategory::interaction},
        {property_names::focusable, PropertyType::boolean, NodeKinds::all, NodeKinds::none, Property{false},
         semantics, PropertyCategory::interaction},
        {property_names::labelled_by, PropertyType::reference, NodeKinds::all, NodeKinds::none, std::nullopt,
         semantics, PropertyCategory::accessibility},
        {property_names::text, PropertyType::string, NodeKinds::text, NodeKinds::text, std::nullopt,
         layout | paint, PropertyCategory::content},
    }};
    return descriptors;
}

const PropertyDescriptor* find_property_descriptor(std::string_view name) noexcept {
    const auto descriptors = property_descriptors();
    const auto found = std::find_if(descriptors.begin(), descriptors.end(),
                                    [&](const PropertyDescriptor& descriptor) { return descriptor.name == name; });
    return found == descriptors.end() ? nullptr : &*found;
}

PropertyType property_type(const Property& value) noexcept {
    return static_cast<PropertyType>(value.index());
}

const Property* effective_property(const UiNode& node, std::string_view name) noexcept {
    if (const auto found = node.properties.find(name); found != node.properties.end()) return &found->second;
    const auto* descriptor = find_property_descriptor(name);
    return descriptor && descriptor->absent_value ? &*descriptor->absent_value : nullptr;
}

} // namespace tessera
