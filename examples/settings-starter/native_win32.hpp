#pragma once

// Optional application host. No Win32 types enter tessera::core.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <tessera/input/event.hpp>
#include <tessera/commands/commands.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <functional>
#include <stdexcept>

namespace settings_native {
inline void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}

// The starter retains placeholder scalar metrics. GDI supplies readable fixed-cell
// coverage for those scalar IDs, rather than claiming a native shaping/IME service.
class Glyphs final : public tessera::GlyphRasterizer {
public:
    std::uint64_t face_revision(tessera::FontId font) const override { return font.value == 0 ? 1 : 0; }
    tessera::Result<tessera::GlyphBitmap> rasterize(const tessera::GlyphRasterRequest& request) override {
        const int size = static_cast<int>(std::lround(request.pixel_size_64 / 64.f));
        const int width = std::max(1, static_cast<int>(std::ceil(size * .5f)));
        const int height = size + 4;
        HDC dc = CreateCompatibleDC(nullptr);
        check(dc != nullptr, "Cannot create glyph DC");
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        HFONT font =
            CreateFontW(-size, width, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FIXED_PITCH, L"Consolas");
        if (!bitmap || !font) {
            if (bitmap) DeleteObject(bitmap);
            if (font) DeleteObject(font);
            DeleteDC(dc);
            throw std::runtime_error("Cannot create glyph resources");
        }
        const auto old_bitmap = SelectObject(dc, bitmap);
        const auto old_font = SelectObject(dc, font);
        PatBlt(dc, 0, 0, width, height, BLACKNESS);
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        SetTextAlign(dc, TA_LEFT | TA_BASELINE);
        const int top = static_cast<int>(std::lround(size * .8f));
        wchar_t text[2]{static_cast<wchar_t>(request.glyph), 0};
        int count = 1;
        if (request.glyph > 0xffff) {
            text[0] = static_cast<wchar_t>(0xd800 + ((request.glyph - 0x10000) >> 10));
            text[1] = static_cast<wchar_t>(0xdc00 + ((request.glyph - 0x10000) & 0x3ff));
            count = 2;
        }
        const bool drawn = TextOutW(dc, 0, top, text, count) != FALSE;
        GdiFlush(); // DIB bytes are read only after GDI's batch completes.
        tessera::GlyphBitmap result{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 0, top,
                                    std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height)};
        const auto* pixels = static_cast<const std::uint8_t*>(bits);
        for (std::size_t i = 0; i < result.coverage.size(); ++i) result.coverage[i] = pixels[i * 4];
        SelectObject(dc, old_font);
        SelectObject(dc, old_bitmap);
        DeleteObject(font);
        DeleteObject(bitmap);
        DeleteDC(dc);
        check(drawn, "Cannot rasterize glyph");
        return {std::move(result), {}};
    }
};

class Window {
public:
    std::function<void(tessera::InputEvent, tessera::CommandSource)> input;
    std::function<tessera::CapturedImage()> capture;
    std::function<std::optional<tessera::Rect>()> focused;
    std::function<bool()> editing;
    std::function<void(tessera::Size)> resized;
    std::function<bool(tessera::Point)> resize_cursor;
    HWND handle = nullptr;
    unsigned presentations = 0;
    const wchar_t* title = L"Tessera Settings - drag boundary: resize, Tab: focus, wheel: scroll";

    ~Window() {
        if (handle) DestroyWindow(handle);
    }
    int run(const std::function<void(Window&)>& smoke = {}) {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW type{};
        type.lpfnWndProc = procedure;
        type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        type.lpszClassName = L"TesseraSettingsStarter";
        check(RegisterClassW(&type) != 0, "Cannot register Settings window");
        const auto style = WS_OVERLAPPEDWINDOW;
        const UINT dpi = GetDpiForSystem();
        RECT bounds{0, 0, MulDiv(960, dpi, 96), MulDiv(360, dpi, 96)};
        AdjustWindowRectExForDpi(&bounds, style, FALSE, 0, dpi);
        handle = CreateWindowExW(0, type.lpszClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                                 bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr,
                                 type.hInstance, this);
        check(handle != nullptr, "Cannot create Settings window");
        ShowWindow(handle, smoke ? SW_SHOWNOACTIVATE : SW_SHOW);
        UpdateWindow(handle);
        if (smoke && !failure) {
            try {
                smoke(*this);
            } catch (...) {
                failure = std::current_exception();
            }
            SendMessageW(handle, WM_CLOSE, 0, 0);
        }
        MSG message{};
        int result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        check(result >= 0, "Settings message loop failed");
        if (failure) std::rethrow_exception(failure);
        return 0;
    }
    // Smoke uses the same window procedure and mapping as physical messages.
    void message(UINT type, WPARAM w = 0, LPARAM l = 0) {
        SendMessageW(handle, type, w, l);
        if (failure) std::rethrow_exception(failure);
        UpdateWindow(handle);
        if (failure) std::rethrow_exception(failure);
    }
    POINT physical(tessera::Point point) const {
        const auto view = mapping();
        return {static_cast<LONG>(std::lround(view.x + point.x * view.scale)),
                static_cast<LONG>(std::lround(view.y + point.y * view.scale))};
    }

private:
    struct Mapping {
        float x, y, scale;
    };
    std::exception_ptr failure;
    wchar_t high_surrogate = 0;
    Mapping mapping() const {
        const float scale = static_cast<float>(GetDpiForWindow(handle)) / 96;
        return {0, 0, scale};
    }
    tessera::Point logical(POINT point) const {
        const auto view = mapping();
        return {(point.x - view.x) / view.scale, (point.y - view.y) / view.scale};
    }
    static tessera::Modifiers modifiers() {
        return {.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                .control = (GetKeyState(VK_CONTROL) & 0x8000) != 0,
                .alt = (GetKeyState(VK_MENU) & 0x8000) != 0,
                .super = (GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0};
    }
    template<class T> void send(T event, tessera::CommandSource source = tessera::CommandSource::keyboard) {
        input(
            {std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()),
             std::move(event)},
            source);
        InvalidateRect(handle, nullptr, FALSE);
    }
    void cancel_pointer() {
        high_surrogate = 0;
        send(tessera::PointerCancel{{1}}, tessera::CommandSource::pointer);
        if (GetCapture() == handle) ReleaseCapture();
    }
    void character(wchar_t unit) {
        if (unit >= 0xd800 && unit <= 0xdbff) {
            high_surrogate = unit;
            return;
        }
        wchar_t text[2]{unit, 0};
        int count = 1;
        if (unit >= 0xdc00 && unit <= 0xdfff) {
            if (!high_surrogate) return;
            text[0] = high_surrogate;
            text[1] = unit;
            count = 2;
        }
        high_surrogate = 0;
        if (unit < 0x20 || unit == 0x7f || !editing()) return;
        const auto mods = modifiers();
        if ((mods.control || mods.alt || mods.super) && !(mods.control && mods.alt && !mods.super)) return;
        std::string utf8(8, '\0');
        const int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, count, utf8.data(),
                                              static_cast<int>(utf8.size()), nullptr, nullptr);
        if (bytes > 0) {
            utf8.resize(bytes);
            send(tessera::TextInput{std::move(utf8)});
        }
    }
    bool keyboard(WPARAM key, LPARAM flags) {
        const auto mods = modifiers();
        const bool repeat = (flags & (LPARAM{1} << 30)) != 0;
        if (!mods.control && !mods.alt && !mods.super) {
            if (key == VK_TAB) {
                high_surrogate = 0;
                if (mods.shift)
                    send(tessera::FocusPrevious{});
                else
                    send(tessera::FocusNext{});
                return true;
            }
            if ((key == VK_RETURN || key == VK_SPACE) && !mods.shift && !editing()) {
                if (!repeat) send(tessera::Activate{});
                return true;
            }
        }
        std::optional<tessera::Key> mapped;
        switch (key) {
            case 'A':
                mapped = tessera::Key::a;
                break;
            case 'C':
                mapped = tessera::Key::c;
                break;
            case 'V':
                mapped = tessera::Key::v;
                break;
            case VK_LEFT:
                mapped = tessera::Key::left;
                break;
            case VK_RIGHT:
                mapped = tessera::Key::right;
                break;
            case VK_UP:
                mapped = tessera::Key::up;
                break;
            case VK_DOWN:
                mapped = tessera::Key::down;
                break;
            case VK_HOME:
                mapped = tessera::Key::home;
                break;
            case VK_END:
                mapped = tessera::Key::end;
                break;
            case VK_BACK:
                mapped = tessera::Key::backspace;
                break;
            case VK_DELETE:
                mapped = tessera::Key::delete_key;
                break;
            case VK_ESCAPE:
                mapped = tessera::Key::escape;
                break;
            case VK_F10:
                mapped = tessera::Key::f10;
                break;
        }
        if (!mapped) return false;
        send(tessera::KeyDown{*mapped, mods, repeat});
        return true;
    }
    void paint() {
        PAINTSTRUCT state{};
        const auto dc = BeginPaint(handle, &state);
        try {
            auto image = capture();
            // Capture RGB contains the linear source-over result on transparent
            // black, encoded as sRGB. Composite white in linear light, then use BGRX.
            for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
                const unsigned alpha = image.pixels[i + 3];
                if (alpha < 255)
                    for (unsigned c = 0; c < 3; ++c) {
                        const double value = image.pixels[i + c] / 255.0;
                        const double linear = value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
                        const double opaque = std::min(1.0, linear + (255 - alpha) / 255.0);
                        const double encoded =
                            opaque <= .0031308 ? opaque * 12.92 : 1.055 * std::pow(opaque, 1 / 2.4) - .055;
                        image.pixels[i + c] = static_cast<std::uint8_t>(std::lround(encoded * 255));
                    }
                std::swap(image.pixels[i], image.pixels[i + 2]);
            }
            RECT client{};
            GetClientRect(handle, &client);
            FillRect(dc, &client, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            BITMAPINFO info{};
            info.bmiHeader = {sizeof(BITMAPINFOHEADER),
                              static_cast<LONG>(image.extent.width),
                              -static_cast<LONG>(image.extent.height),
                              1,
                              32,
                              BI_RGB};
            const auto view = mapping();
            SetStretchBltMode(dc, COLORONCOLOR);
            const auto rows =
                StretchDIBits(dc, static_cast<int>(std::lround(view.x)), static_cast<int>(std::lround(view.y)),
                              static_cast<int>(std::lround(image.extent.width * view.scale)),
                              static_cast<int>(std::lround(image.extent.height * view.scale)), 0, 0, image.extent.width,
                              image.extent.height, image.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            check(rows != 0 && rows != GDI_ERROR, "Cannot present Settings frame");
            if (const auto rect = focused()) {
                const auto a = physical(rect->origin);
                const auto b = physical({rect->origin.x + rect->size.width, rect->origin.y + rect->size.height});
                RECT ring{a.x, a.y, b.x, b.y};
                DrawFocusRect(dc, &ring);
            }
            ++presentations;
        } catch (...) {
            EndPaint(handle, &state);
            throw;
        }
        EndPaint(handle, &state);
    }
    LRESULT dispatch(UINT type, WPARAM w, LPARAM l) {
        switch (type) {
            case WM_SETCURSOR: {
                if (LOWORD(l) != HTCLIENT) break;
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(handle, &point);
                const int cursor = resize_cursor(logical(point)) ? 32644 : 32512;
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(cursor)));
                return TRUE;
            }
            case WM_PAINT:
                paint();
                return 0;
            case WM_ERASEBKGND:
                return 1;
            case WM_MOUSEMOVE:
                send(tessera::PointerMove{{1}, logical({GET_X_LPARAM(l), GET_Y_LPARAM(l)}), modifiers()},
                     tessera::CommandSource::pointer);
                return 0;
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
                high_surrogate = 0;
                SetFocus(handle);
                SetCapture(handle);
                send(tessera::PointerDown{{1},
                                          logical({GET_X_LPARAM(l), GET_Y_LPARAM(l)}),
                                          type == WM_LBUTTONDOWN ? tessera::PointerButton::primary
                                                                 : tessera::PointerButton::secondary,
                                          modifiers()},
                     tessera::CommandSource::pointer);
                return 0;
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
                send(tessera::PointerUp{{1},
                                        logical({GET_X_LPARAM(l), GET_Y_LPARAM(l)}),
                                        type == WM_LBUTTONUP ? tessera::PointerButton::primary
                                                             : tessera::PointerButton::secondary,
                                        modifiers()},
                     tessera::CommandSource::pointer);
                if ((w & (MK_LBUTTON | MK_RBUTTON)) == 0 && GetCapture() == handle) ReleaseCapture();
                return 0;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL: {
                POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ScreenToClient(handle, &point);
                const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA * 48;
                send(tessera::Scroll{logical(point),
                                     type == WM_MOUSEWHEEL ? tessera::Point{0, -delta} : tessera::Point{delta, 0},
                                     modifiers()},
                     tessera::CommandSource::pointer);
                return 0;
            }
            case WM_KEYDOWN:
                if (keyboard(w, l)) return 0;
                break;
            case WM_SYSKEYDOWN:
                if (w == VK_F10 && keyboard(w, l)) return 0;
                break;
            case WM_CHAR:
                character(static_cast<wchar_t>(w));
                return 0;
            case WM_CAPTURECHANGED:
                send(tessera::PointerCancel{{1}}, tessera::CommandSource::pointer);
                return 0;
            case WM_KILLFOCUS:
            case WM_CANCELMODE:
                cancel_pointer();
                return 0;
            case WM_SIZE: {
                cancel_pointer();
                RECT client{};
                GetClientRect(handle, &client);
                if (client.right > 0 && client.bottom > 0) {
                    const auto scale = mapping().scale;
                    resized({client.right / scale, client.bottom / scale});
                }
                InvalidateRect(handle, nullptr, FALSE);
                return 0;
            }
            case WM_DPICHANGED: {
                const auto* rect = reinterpret_cast<const RECT*>(l);
                SetWindowPos(handle, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case WM_CLOSE:
                DestroyWindow(handle);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            case WM_NCDESTROY: {
                const auto window = handle;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                handle = nullptr;
                return DefWindowProcW(window, type, w, l);
            }
        }
        return DefWindowProcW(handle, type, w, l);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT type, WPARAM w, LPARAM l) noexcept {
        auto* self = reinterpret_cast<Window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (type == WM_NCCREATE) {
            self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            self->handle = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window, type, w, l);
        try {
            return self->dispatch(type, w, l);
        } catch (...) {
            if (!self->failure) self->failure = std::current_exception();
            PostMessageW(window, WM_CLOSE, 0, 0);
            return 0;
        }
    }
};
} // namespace settings_native
