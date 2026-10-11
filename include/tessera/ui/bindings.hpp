#pragma once

#include <tessera/reactive/runtime.hpp>
#include <tessera/ui/property_metadata.hpp>

namespace tessera {
inline constexpr std::size_t max_property_bindings = max_reactive_nodes;

// Compile-time keys for the reflected semantic vocabulary; never serialized.
enum class BoundProperty { text, focusable, disabled, labelled_by };
template<BoundProperty P>
using BoundPropertyValue = std::conditional_t<P == BoundProperty::text, std::string,
    std::conditional_t<P == BoundProperty::labelled_by, NodeReference, bool>>;

struct BoundDocument {
    UiDocument document;
    // Conservative union of changed properties' metadata, relative to the
    // supplied authored document, not relative to a prior published generation.
    PropertyStages stages = PropertyStages::none;
};

// Host-built bindings addressed by author ID. References are weak and retain
// neither reactive owners nor runtime lifetime. The host retains the last valid
// presentation and publishes effects only after all presentation stages succeed.
class PropertyBindings final {
public:
    template<BoundProperty P>
    Result<std::size_t> bind(std::string target, Signal<BoundPropertyValue<P>> value) {
        static_assert(valid_property<P>);
        return add(std::move(target), P, std::move(value));
    }
    template<BoundProperty P>
    Result<std::size_t> bind(std::string target, Computed<BoundPropertyValue<P>> value) {
        static_assert(valid_property<P>);
        return add(std::move(target), P, std::move(value));
    }
    // Requires a valid authored document with all required properties present.
    // Validates every target before reading; returns no partial candidate on
    // schema or reactive failure. Does not flush, publish, or deliver effects.
    Result<BoundDocument> apply(const ReactiveRuntime&, const UiDocument&,
                                const ValidationContext& = {}) const;
    std::size_t size() const noexcept { return entries_.size(); }

private:
    template<BoundProperty P>
    static constexpr bool valid_property = P == BoundProperty::text || P == BoundProperty::focusable ||
        P == BoundProperty::disabled || P == BoundProperty::labelled_by;
    using Value = std::variant<Signal<std::string>, Computed<std::string>, Signal<bool>, Computed<bool>,
                               Signal<NodeReference>, Computed<NodeReference>>;
    struct Entry { std::string target; BoundProperty property; Value value; };
    Result<std::size_t> add(std::string, BoundProperty, Value);
    std::vector<Entry> entries_;
};
} // namespace tessera
