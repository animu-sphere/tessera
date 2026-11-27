#pragma once

#include <tessera/input/event.hpp>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tessera {

// How one selection request combines with the current multiple selection of a ListView or TreeView.
enum class SelectionGesture : std::uint8_t {
    replace,   // Select only the key, which becomes the anchor: a plain click, Enter or Space.
    toggle,    // Add or remove the key, which becomes the anchor: Control+click or Control+Space.
    extend,    // Select the rows from the anchor to the key instead: Shift+click or Shift+arrows.
    add_range, // Add the rows from the anchor to the key: Control+Shift+click.
};

// The conventional desktop gesture for a click's modifiers: Shift extends, Control toggles and both
// add a range. Alt and Super do not change the gesture.
SelectionGesture selection_gesture(Modifiers);

// A host-owned multiple selection and the anchor that range gestures start from.
struct CollectionSelection {
    std::vector<std::string> keys;     // Unique selected keys.
    std::optional<std::string> anchor; // The key of the last replace or toggle; range gestures keep it.
    bool operator==(const CollectionSelection&) const = default;
};

// The selection after `gesture` at `key`. `order` lists the rows a user can select now, in display
// order, such as authoring::selectable_keys. A range spans `order` between the anchor and the key;
// without an anchor in `order`, the key starts it. Toggle, add_range and select_all keep selected keys
// outside `order`, such as collapsed descendants, while replace and extend drop them. The result lists
// keys in `order` first, then the kept keys in their previous order. Nullopt when `key` is not in `order`.
std::optional<CollectionSelection> select_items(std::span<const std::string> order, const CollectionSelection&,
                                                std::string_view key, SelectionGesture);

// Every key in `order` added to the selection, as Control+A requests; the anchor is unchanged.
CollectionSelection select_all(std::span<const std::string> order, const CollectionSelection&);

} // namespace tessera
