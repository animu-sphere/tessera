#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/color.hpp>
#include <tessera/style/gradient.hpp>
#include <tessera/style/outline.hpp>
#include <tessera/style/shadow.hpp>
#include <tessera/ui/document.hpp>
#include <cmath>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace tessera::detail {

// Shared numeric checks for contract validation. Each value reports at most one diagnostic.
class Checker {
public:
    explicit Checker(std::vector<Diagnostic>& out) : out_(out) {}

    void error(std::string code, std::string path, std::string message) {
        out_.push_back({std::move(code), Severity::error, std::move(path), std::move(message), {}});
    }
    template<class Enum> void enumeration(Enum value, Enum last, std::string path) {
        if (static_cast<unsigned>(value) > static_cast<unsigned>(last))
            error("unknown_value", std::move(path), "Use one of the declared enumeration values.");
    }
    bool finite(float value, const std::string& path) {
        if (std::isfinite(value)) return true;
        error("invalid_number", path, "Numbers must be finite.");
        return false;
    }
    void range(float value, const std::string& path, float minimum, float maximum, std::string_view expected) {
        if (finite(value, path) && (value < minimum || value > maximum))
            error("out_of_range", path, "Expected " + std::string(expected) + ".");
    }
    void non_negative(float value, const std::string& path) {
        if (finite(value, path) && value < 0) error("out_of_range", path, "Expected a value >= 0.");
    }
    void positive(float value, const std::string& path) {
        if (finite(value, path) && value <= 0) error("out_of_range", path, "Expected a value > 0.");
    }
    void point(const Point& value, const std::string& path) {
        finite(value.x, path + "/x");
        finite(value.y, path + "/y");
    }
    void size(const Size& value, const std::string& path) {
        non_negative(value.width, path + "/width");
        non_negative(value.height, path + "/height");
    }
    void rect(const Rect& value, const std::string& path) {
        point(value.origin, path + "/origin");
        size(value.size, path + "/size");
    }
    void edges(const Edges& value, const std::string& path) {
        non_negative(value.top, path + "/top");
        non_negative(value.right, path + "/right");
        non_negative(value.bottom, path + "/bottom");
        non_negative(value.left, path + "/left");
    }
    void color(const Color& value, const std::string& path) {
        range(value.r, path + "/r", 0, 1, "a component in [0, 1]");
        range(value.g, path + "/g", 0, 1, "a component in [0, 1]");
        range(value.b, path + "/b", 0, 1, "a component in [0, 1]");
        range(value.a, path + "/a", 0, 1, "a component in [0, 1]");
    }
    void gradient(const Gradient& value, const std::string& path) {
        std::visit([&](const auto& fill) { gradient_fill(fill, path); }, value);
    }
    void shadow(const BoxShadow& value, const std::string& path) {
        point(value.offset, path + "/offset");
        non_negative(value.softness, path + "/softness");
        color(value.color, path + "/color");
    }
    void stroke(float width, float dash, float gap, float offset, const std::string& path) {
        non_negative(width, path + "/width");
        non_negative(dash, path + "/dash_length");
        non_negative(gap, path + "/gap_length");
        finite(offset, path + "/dash_offset");
        if (dash == 0 && gap > 0)
            error("invalid_dash", path + "/dash_length", "Use a positive dash length when a gap is present.");
    }
    void outline(const Outline& value, const std::string& path) {
        stroke(value.width, value.dash_length, value.gap_length, value.dash_offset, path);
        non_negative(value.offset, path + "/offset");
        color(value.color, path + "/color");
    }

private:
    template<class T> void gradient_fill(const T& value, const std::string& path) {
        if constexpr (std::is_same_v<T, LinearGradient>) {
            for (const auto& [point, name] : {std::pair{value.start, "/start"}, std::pair{value.end, "/end"}}) {
                range(point.x, path + name + "/x", 0, 1, "a coordinate in [0, 1]");
                range(point.y, path + name + "/y", 0, 1, "a coordinate in [0, 1]");
            }
            if (value.start == value.end)
                error("degenerate_gradient", path + "/end", "Use distinct gradient endpoints.");
        } else {
            range(value.center.x, path + "/center/x", 0, 1, "a coordinate in [0, 1]");
            range(value.center.y, path + "/center/y", 0, 1, "a coordinate in [0, 1]");
            positive(value.radius.width, path + "/radius/width");
            positive(value.radius.height, path + "/radius/height");
        }
        color(value.start_color, path + "/start_color");
        color(value.end_color, path + "/end_color");
        if (value.stops.size() == 1)
            error("invalid_gradient_stops", path + "/stops",
                  "Supply at least two stops, or use the two-color shorthand.");
        for (std::size_t i = 0; i < value.stops.size(); ++i) {
            const auto at = path + "/stops/" + std::to_string(i);
            range(value.stops[i].offset, at + "/offset", 0, 1, "an offset in [0, 1]");
            color(value.stops[i].color, at + "/color");
            if (i && value.stops[i].offset < value.stops[i - 1].offset)
                error("invalid_gradient_stops", at + "/offset", "Keep stops in nondecreasing offset order.");
        }
    }

    std::vector<Diagnostic>& out_;
};

} // namespace tessera::detail
