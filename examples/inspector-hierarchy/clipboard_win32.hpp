#pragma once

// Application-owned Win32 clipboard adapter. No platform types enter core APIs.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tessera/input/text_edit.hpp>
#include <algorithm>
#include <cstring>
#include <string_view>

namespace inspector_clipboard {
inline std::vector<tessera::Diagnostic> error(std::string code, std::string message) {
    return {{std::move(code), tessera::Severity::error, "/clipboard", std::move(message)}};
}
inline tessera::Result<std::string> decode(std::wstring_view text) {
    if (text.size() > tessera::max_text_edit_bytes)
        return {std::nullopt, error("text_too_long", "Clipboard text exceeds the single-line editor limit.")};
    if (text.empty()) return {std::string{}, {}};
    const auto count = static_cast<int>(text.size());
    const int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), count, nullptr, 0, nullptr, nullptr);
    if (!size) return {std::nullopt, error("invalid_utf16", "Clipboard text has invalid UTF-16.")};
    if (size > tessera::max_text_edit_bytes)
        return {std::nullopt, error("text_too_long", "Clipboard text exceeds 4096 UTF-8 bytes.")};
    std::string result(size, '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), count, result.data(), size, nullptr, nullptr) !=
        size)
        return {std::nullopt, error("clipboard_conversion", "Cannot convert clipboard text to UTF-8.")};
    return {std::move(result), {}};
}
inline tessera::Result<std::wstring> encode(std::string_view text) {
    if (text.size() > tessera::max_text_edit_bytes)
        return {std::nullopt, error("text_too_long", "Copy exceeds 4096 UTF-8 bytes.")};
    if (text.find('\0') != std::string_view::npos)
        return {std::nullopt, error("clipboard_nul", "Copy contains an embedded NUL.")};
    if (text.empty()) return {std::wstring{}, {}};
    const auto count = static_cast<int>(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), count, nullptr, 0);
    if (!size) return {std::nullopt, error("invalid_utf8", "Copy has invalid UTF-8.")};
    std::wstring result(size, L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), count, result.data(), size) != size)
        return {std::nullopt, error("clipboard_conversion", "Cannot convert copy to UTF-16.")};
    return {std::move(result), {}};
}
struct Access {
    bool opened;
    explicit Access(HWND owner) : opened(OpenClipboard(owner) != FALSE) {}
    ~Access() {
        if (opened) CloseClipboard();
    }
    Access(const Access&) = delete;
    Access& operator=(const Access&) = delete;
};
struct Memory {
    HGLOBAL handle;
    ~Memory() {
        if (handle) GlobalFree(handle);
    }
};
struct Lock {
    HGLOBAL handle;
    void* data;
    explicit Lock(HGLOBAL memory) : handle(memory), data(GlobalLock(memory)) {}
    ~Lock() {
        if (data) GlobalUnlock(handle);
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
};
inline tessera::Result<std::string> read(HWND owner) {
    Access access(owner);
    if (!access.opened) return {std::nullopt, error("clipboard_unavailable", "Cannot open the Windows clipboard.")};
    const auto data = GetClipboardData(CF_UNICODETEXT);
    if (!data) return {std::nullopt, error("clipboard_format", "Clipboard has no Unicode text.")};
    const auto bytes = GlobalSize(data);
    Lock locked(data);
    const auto* text = static_cast<const wchar_t*>(locked.data);
    if (!text) return {std::nullopt, error("clipboard_unavailable", "Cannot lock clipboard text.")};
    // Bound the scan and allocation even when another application supplies malformed data.
    const auto capacity = std::min<std::size_t>(bytes / sizeof(wchar_t), tessera::max_text_edit_bytes + 1);
    const auto end = std::find(text, text + capacity, L'\0');
    return end == text + capacity
               ? tessera::Result<std::string>{std::nullopt,
                                              error("clipboard_bounds", "Clipboard text is oversized or unterminated.")}
               : decode({text, static_cast<std::size_t>(end - text)});
}
inline std::vector<tessera::Diagnostic> write(HWND owner, std::string_view text) {
    auto converted = encode(text);
    if (!converted) return std::move(converted.diagnostics);
    // Windows text uses CRLF line breaks; multiple-line notes copy with LF.
    std::wstring lines;
    lines.reserve(converted.value->size());
    for (const auto unit : *converted.value) {
        if (unit == L'\n') lines += L'\r';
        lines += unit;
    }
    *converted.value = std::move(lines);
    const auto bytes = (converted.value->size() + 1) * sizeof(wchar_t);
    Memory memory{GlobalAlloc(GMEM_MOVEABLE, bytes)};
    if (!memory.handle) return error("clipboard_memory", "Cannot allocate clipboard text.");
    {
        Lock locked(memory.handle);
        if (!locked.data) return error("clipboard_memory", "Cannot lock copy memory.");
        std::memcpy(locked.data, converted.value->c_str(), bytes);
    }
    Access access(owner);
    if (!access.opened) return error("clipboard_unavailable", "Cannot open the Windows clipboard.");
    if (!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT, memory.handle))
        return error("clipboard_write", "Cannot publish Unicode text to the Windows clipboard.");
    memory.handle = nullptr; // Successful SetClipboardData transfers ownership to Windows.
    return {};
}
} // namespace inspector_clipboard
