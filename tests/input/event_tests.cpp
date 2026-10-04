#include <tessera/input/event.hpp>
#include "../check.hpp"
#include <cmath>
#include <iostream>

using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {

void normalized_events() {
    const tessera::InputEvent valid[] = {
        {10us, tessera::PointerDown{{1}, {12.5f, 4}, tessera::PointerButton::primary, {.shift = true}}},
        {11us, tessera::PointerCancel{{1}}},
        {12us, tessera::Scroll{{0, 0}, {0, -40}, {}}},
        {13us, tessera::KeyDown{tessera::Key::f12, {.control = true}, true}},
        {14us, tessera::TextInput{"日本"}},
        {15us, tessera::Navigate{tessera::Direction::left, false}},
        {16us, tessera::Activate{}},
    };
    for (const auto& event : valid) check(tessera::validate(event).empty(), "Valid event rejected");

    check(has(tessera::validate({-1us, tessera::Cancel{}}), "out_of_range", "/timestamp"), "Negative timestamp accepted");
    check(has(tessera::validate({0us, tessera::PointerMove{{}, {std::nanf(""), 0}, {}}}), "invalid_number", "/position/x"),
          "Non-finite pointer position accepted");
    check(has(tessera::validate({0us, tessera::Scroll{{}, {0, INFINITY}, {}}}), "invalid_number", "/delta/y"),
          "Non-finite scroll delta accepted");
    check(has(tessera::validate({0us, tessera::KeyUp{static_cast<tessera::Key>(200), {}}}), "unknown_value", "/key"),
          "Unknown key accepted");
    check(has(tessera::validate({0us, tessera::PointerUp{{}, {}, static_cast<tessera::PointerButton>(9), {}}}),
              "unknown_value", "/button"), "Unknown button accepted");
    check(has(tessera::validate({0us, tessera::TextInput{""}}), "empty_text", "/text"), "Empty text input accepted");
    check(has(tessera::validate({0us, tessera::TextInput{"\xc0\xaf"}}), "invalid_utf8", "/text"), "Invalid UTF-8 accepted");
}

} // namespace

int main() {
    try {
        normalized_events();
        std::cout << "Normalized input event checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
