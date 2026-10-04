#include <tessera/input/event.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <type_traits>

namespace tessera {

std::vector<Diagnostic> validate(const InputEvent& event) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    if (event.timestamp.count() < 0)
        check.error("out_of_range", "/timestamp", "Use a non-negative host monotonic timestamp.");
    std::visit([&](const auto& data) {
        using T = std::decay_t<decltype(data)>;
        if constexpr (std::is_same_v<T, PointerMove> || std::is_same_v<T, PointerDown> ||
                      std::is_same_v<T, PointerUp> || std::is_same_v<T, Scroll>) {
            check.point(data.position, "/position");
        }
        if constexpr (std::is_same_v<T, PointerDown> || std::is_same_v<T, PointerUp>) {
            check.enumeration(data.button, PointerButton::middle, "/button");
        } else if constexpr (std::is_same_v<T, Scroll>) {
            check.point(data.delta, "/delta");
        } else if constexpr (std::is_same_v<T, KeyDown> || std::is_same_v<T, KeyUp>) {
            check.enumeration(data.key, Key::f12, "/key");
        } else if constexpr (std::is_same_v<T, TextInput>) {
            if (data.text.empty()) check.error("empty_text", "/text", "Send TextInput only for committed text.");
            else if (!detail::valid_utf8(data.text)) check.error("invalid_utf8", "/text", "Text must be valid UTF-8.");
        } else if constexpr (std::is_same_v<T, Navigate>) {
            check.enumeration(data.direction, Direction::right, "/direction");
        }
    }, event.data);
    return errors;
}

} // namespace tessera
