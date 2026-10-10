// Optional Win32 example input adapter. No DirectInput type enters core or the renderer.
#pragma once
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "gamepad.hpp"
#include <iostream>

namespace gamepad {
class DirectInputDevice final {
public:
    DirectInputDevice(WORD vendor, WORD product, bool axes, const char* name)
        : vendor_(vendor), product_(product), axes_(axes), name_(name) {}
    DirectInputDevice(const DirectInputDevice&) = delete;
    DirectInputDevice& operator=(const DirectInputDevice&) = delete;
    ~DirectInputDevice() { release_device(); if (input_) input_->Release(); }

    std::optional<DIJOYSTATE2> read(HWND window, std::chrono::microseconds time) {
        if (GetForegroundWindow() != window) {
            if (device_) device_->Unacquire();
            return std::nullopt;
        }
        if (!device_ && time >= next_scan_) {
            next_scan_ = time + std::chrono::seconds(1);
            if (!input_) {
                const auto result = DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION,
                    IID_IDirectInput8W, reinterpret_cast<void**>(&input_), nullptr);
                if (FAILED(result)) { report("create interface", result); return std::nullopt; }
            }
            window_ = window;
            const auto result = input_->EnumDevices(DI8DEVCLASS_GAMECTRL, enumerate, this, DIEDFL_ATTACHEDONLY);
            if (FAILED(result)) report("enumerate", result);
        }
        if (!device_) return std::nullopt;
        auto result = device_->Poll();
        if (FAILED(result)) {
            result = device_->Acquire();
            if (FAILED(result)) {
                report("acquire", result);
                if (result != DIERR_OTHERAPPHASPRIO) release_device();
                return std::nullopt;
            }
            result = device_->Poll();
        }
        DIJOYSTATE2 state{};
        if (SUCCEEDED(result)) result = device_->GetDeviceState(sizeof(state), &state);
        if (FAILED(result)) {
            report("read state", result);
            release_device();
            return std::nullopt;
        }
        return state;
    }

private:
    static BOOL CALLBACK enumerate(const DIDEVICEINSTANCEW* instance, void* context) {
        auto& self = *static_cast<DirectInputDevice*>(context);
        // Each host profile selects its exact vendor/product descriptor.
        if (LOWORD(instance->guidProduct.Data1) != self.vendor_ || HIWORD(instance->guidProduct.Data1) != self.product_)
            return DIENUM_CONTINUE;
        IDirectInputDevice8W* device = nullptr;
        auto result = self.input_->CreateDevice(instance->guidInstance, &device, nullptr);
        if (FAILED(result)) { self.report("create device", result); return DIENUM_CONTINUE; }
        result = device->SetDataFormat(&c_dfDIJoystick2);
        if (SUCCEEDED(result)) result = device->SetCooperativeLevel(self.window_, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
        bool valid = SUCCEEDED(result);
        if (!valid) self.report("configure device", result);
        for (const DWORD axis : {DWORD(DIJOFS_X), DWORD(DIJOFS_Y)}) {
            DIPROPRANGE range{};
            range.diph = {sizeof(range), sizeof(range.diph), axis, DIPH_BYOFFSET};
            range.lMin = -32768; range.lMax = 32767;
            if (valid && self.axes_) {
                result = device->SetProperty(DIPROP_RANGE, &range.diph);
                valid = SUCCEEDED(result);
                if (!valid) self.report("axis range", result);
            }
        }
        if (!valid) { device->Release(); return DIENUM_CONTINUE; }
        self.device_ = device;
        std::cout << "DirectInput " << self.name_ << " connected; VID=" << std::hex << self.vendor_
                  << " PID=" << self.product_ << std::dec << std::endl;
        return DIENUM_STOP;
    }
    void release_device() {
        if (device_) { device_->Unacquire(); device_->Release(); device_ = nullptr; }
    }
    void report(const char* stage, HRESULT result) {
        if (result == last_error_) return;
        last_error_ = result;
        std::cerr << "DirectInput " << name_ << ' ' << stage << " HRESULT=" << std::hex << static_cast<unsigned long>(result) << std::dec << std::endl;
    }
    IDirectInput8W* input_ = nullptr;
    IDirectInputDevice8W* device_ = nullptr;
    HWND window_ = nullptr;
    std::chrono::microseconds next_scan_{};
    HRESULT last_error_ = S_OK;
    WORD vendor_, product_;
    bool axes_;
    const char* name_;
};

class DualSenseInput final {
public:
    std::optional<Sample> read(HWND window, std::chrono::microseconds time) {
        const auto state = device_.read(window, time);
        if (!state) return std::nullopt;
        return dualsense_sample((state->rgbButtons[1] & 0x80) != 0, (state->rgbButtons[2] & 0x80) != 0,
                                state->rgdwPOV[0], state->lX, state->lY);
    }
private:
    DirectInputDevice device_{0x054c, 0x0ce6, true, "DualSense"};
};

// A deliberately selected L/R pair, held vertically. Neither side alone emits commands.
// The ordinary Bluetooth simple report exposes the left stick as an eight-way POV.
class JoyConPairInput final {
public:
    std::optional<Sample> read(HWND window, std::chrono::microseconds time) {
        const auto left = left_.read(window, time), right = right_.read(window, time);
        if (!left || !right) return std::nullopt;
        const auto bits = [](const DIJOYSTATE2& state) {
            std::uint16_t buttons = 0;
            for (unsigned i = 0; i < 4; ++i) if (state.rgbButtons[i] & 0x80) buttons |= std::uint16_t(1u << i);
            return buttons;
        };
        return joycon_pair_sample(bits(*left), bits(*right), left->rgdwPOV[0]);
    }
private:
    DirectInputDevice left_{0x057e, 0x2006, false, "Joy-Con (L)"};
    DirectInputDevice right_{0x057e, 0x2007, false, "Joy-Con (R)"};
};
} // namespace gamepad
