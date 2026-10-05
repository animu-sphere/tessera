// Host gamepad policy for the Win32 menu: dead zones, D-pad/stick direction, and repeat timing
// turn sampled controller state into logical focus commands. It makes no device calls, so the
// smoke checks it with scripted samples and timestamps instead of a physical controller.
#pragma once

#include <tessera/input/event.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace gamepad {

// Buttons the menu reads, independent of the device API.
enum Button : std::uint16_t { dpad_up = 1, dpad_down = 2, dpad_left = 4, dpad_right = 8, accept = 16, back = 32 };

// One controller sample: held buttons and the left stick, each axis in [-32768, 32767] with +y up.
struct Sample {
    std::uint16_t buttons = 0;
    std::int16_t stick_x = 0, stick_y = 0;
};

// The stick engages a direction above `engage` of full deflection and keeps it until it falls to
// `release`, XInput's recommended left-stick dead zone, so noise near one threshold cannot
// retrigger navigation.
constexpr float engage = 0.5f;
constexpr float release = 7849 / 32767.0f;
// A held direction navigates once, then repeats after the delay at the interval, at most once per sample.
constexpr std::chrono::microseconds repeat_delay{400'000}, repeat_interval{120'000};

class Navigator {
public:
    // `sample` is empty while no controller is connected or the window does not receive input.
    // Each change of held direction is one non-repeat Navigate; A and B press edges are Activate and
    // Cancel, emitted after navigation. Input already held when a sample first arrives is ignored
    // until released, so connecting or refocusing never activates or navigates.
    std::vector<tessera::InputEvent> update(const std::optional<Sample>& sample, std::chrono::microseconds now) {
        std::vector<tessera::InputEvent> events;
        if (!sample) { *this = {}; return events; }
        const auto stick_direction = stick(*sample);
        const auto pad_direction = dpad(sample->buttons);
        const auto held = pad_direction ? pad_direction : stick_direction;
        if (!connected_) {
            connected_ = true; buttons_ = sample->buttons; direction_ = held; suppressed_ = held.has_value();
            return events;
        }
        if (held != direction_) {
            direction_ = held; suppressed_ = false;
            if (held) { events.push_back({now, tessera::Navigate{*held, false}}); next_repeat_ = now + repeat_delay; }
        } else if (held && !suppressed_ && now >= next_repeat_) {
            events.push_back({now, tessera::Navigate{*held, true}});
            next_repeat_ = now + repeat_interval;
        }
        const auto pressed = sample->buttons & ~buttons_;
        buttons_ = sample->buttons;
        if (pressed & accept) events.push_back({now, tessera::Activate{}});
        if (pressed & back) events.push_back({now, tessera::Cancel{}});
        return events;
    }

private:
    // Diagonal D-pad input resolves to its vertical direction, as menus are mostly vertical.
    static std::optional<tessera::Direction> dpad(std::uint16_t buttons) {
        if (buttons & dpad_up) return tessera::Direction::up;
        if (buttons & dpad_down) return tessera::Direction::down;
        if (buttons & dpad_left) return tessera::Direction::left;
        if (buttons & dpad_right) return tessera::Direction::right;
        return std::nullopt;
    }
    // Radial dead zone with hysteresis; the dominant axis selects the direction, vertical on ties.
    std::optional<tessera::Direction> stick(const Sample& sample) {
        const float x = std::max(sample.stick_x / 32767.0f, -1.0f), y = std::max(sample.stick_y / 32767.0f, -1.0f);
        engaged_ = std::hypot(x, y) > (engaged_ ? release : engage);
        if (!engaged_) return std::nullopt;
        if (std::abs(y) >= std::abs(x)) return y > 0 ? tessera::Direction::up : tessera::Direction::down;
        return x > 0 ? tessera::Direction::right : tessera::Direction::left;
    }

    bool connected_ = false, engaged_ = false;
    std::uint16_t buttons_ = 0;
    std::optional<tessera::Direction> direction_;
    bool suppressed_ = false; // The direction was held at connection and has not changed since.
    std::chrono::microseconds next_repeat_{0};
};

} // namespace gamepad
