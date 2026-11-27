#include <tessera/input/collection_selection.hpp>
#include <algorithm>
#include <set>

namespace tessera {
namespace {

using KeySet = std::set<std::string, std::less<>>;

std::optional<std::size_t> position(std::span<const std::string> order, std::string_view key) {
    const auto found = std::find(order.begin(), order.end(), key);
    if (found == order.end()) return std::nullopt;
    return static_cast<std::size_t>(found - order.begin());
}

// Keys of `chosen` in `order`, then the `kept` ones outside it in their previous order.
std::vector<std::string> arrange(std::span<const std::string> order, const KeySet& chosen,
                                 const std::vector<std::string>& kept) {
    std::vector<std::string> keys;
    KeySet listed;
    for (const auto& key : order)
        if (chosen.contains(key) && listed.insert(key).second) keys.push_back(key);
    for (const auto& key : kept)
        if (chosen.contains(key) && listed.insert(key).second) keys.push_back(key);
    return keys;
}

} // namespace

SelectionGesture selection_gesture(Modifiers modifiers) {
    if (modifiers.shift) return modifiers.control ? SelectionGesture::add_range : SelectionGesture::extend;
    return modifiers.control ? SelectionGesture::toggle : SelectionGesture::replace;
}

std::optional<CollectionSelection> select_items(std::span<const std::string> order, const CollectionSelection& current,
                                                std::string_view key, SelectionGesture gesture) {
    const auto at = position(order, key);
    if (!at) return std::nullopt;
    KeySet chosen(current.keys.begin(), current.keys.end());
    CollectionSelection next;
    switch (gesture) {
        case SelectionGesture::replace:
            next.keys = {std::string(key)};
            next.anchor = std::string(key);
            return next;
        case SelectionGesture::toggle:
            if (const auto found = chosen.find(key); found != chosen.end())
                chosen.erase(found);
            else
                chosen.emplace(key);
            next.keys = arrange(order, chosen, current.keys);
            next.anchor = std::string(key);
            return next;
        case SelectionGesture::extend:
        case SelectionGesture::add_range: {
            const auto from = current.anchor ? position(order, *current.anchor) : std::nullopt;
            const auto start = from.value_or(*at);
            if (gesture == SelectionGesture::extend) chosen.clear();
            for (auto i = std::min(start, *at); i <= std::max(start, *at); ++i) chosen.insert(order[i]);
            next.keys = arrange(order, chosen, current.keys);
            next.anchor = order[start];
            return next;
        }
    }
    return std::nullopt;
}

CollectionSelection select_all(std::span<const std::string> order, const CollectionSelection& current) {
    KeySet chosen(current.keys.begin(), current.keys.end());
    chosen.insert(order.begin(), order.end());
    return {arrange(order, chosen, current.keys), current.anchor};
}

} // namespace tessera
