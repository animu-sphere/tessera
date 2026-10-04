#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/style/color.hpp>
#include <tessera/ui/document.hpp>
#include <cmath>
#include <string>
#include <string_view>
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
    template<class Enum>
    void enumeration(Enum value, Enum last, std::string path) {
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

private:
    std::vector<Diagnostic>& out_;
};

} // namespace tessera::detail
