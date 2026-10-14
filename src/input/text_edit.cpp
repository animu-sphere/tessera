#include <tessera/input/text_edit.hpp>
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <limits>

namespace tessera {
namespace {
Result<TextEditState> error(std::string code, std::string path, std::string message) {
    return {std::nullopt, {{std::move(code), Severity::error, std::move(path), std::move(message), {}}}};
}
bool boundary(const std::string& text, std::size_t offset) {
    return offset <= text.size() && (offset == text.size() ||
        (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80);
}
std::size_t previous(const std::string& text, std::size_t offset) {
    if (offset) do { --offset; } while (!boundary(text, offset));
    return offset;
}
std::size_t next(const std::string& text, std::size_t offset) {
    if (offset < text.size()) do { ++offset; } while (!boundary(text, offset));
    return offset;
}
Result<TextEditState> validate_text(const std::string& text) {
    if (text.size() > max_text_edit_bytes)
        return error("text_too_long", "/edit/text", "String editor accepts at most 4096 UTF-8 bytes.");
    if (!detail::valid_utf8(text))
        return error("invalid_utf8", "/edit/text", "Expected valid UTF-8 committed text.");
    if (text.find_first_of("\r\n") != std::string::npos)
        return error("multiline_text", "/edit/text", "Single-line editor rejects CR and LF.");
    return {TextEditState{}, {}};
}
}
Result<TextEditState> make_text_edit_state(std::uint64_t owner, std::string text) {
    if (!owner) return error("invalid_owner", "/edit/owner", "Host must supply a nonzero editing lifetime.");
    if (auto valid = validate_text(text); !valid) return valid;
    const auto end = text.size();
    return {TextEditState{owner, 1, std::move(text), end, end}, {}};
}
Result<TextEditState> edit_text(const TextEditState& state, const TextEditRequest& request, TextEditPolicy policy) {
    if (!state.owner || !state.revision)
        return error("invalid_state", "/edit/state", "Observation requires a live owner and revision.");
    if (auto valid = validate_text(state.text); !valid) return valid;
    if (!boundary(state.text, state.anchor) || !boundary(state.text, state.caret))
        return error("invalid_selection", "/edit/state", "Observed selection must lie on UTF-8 scalar boundaries.");
    if (request.owner != state.owner)
        return error("stale_owner", "/edit/owner", "Request does not address this editing lifetime.");
    if (request.revision != state.revision)
        return error("stale_revision", "/edit/revision", "Request must address the observed text and selection revision.");
    if (!policy.enabled) return error("disabled_editor", "/edit", "Disabled editor rejects editing and selection.");
    const bool mutation = std::holds_alternative<TextEditInsert>(request.operation) ||
        std::holds_alternative<TextEditErase>(request.operation);
    if (mutation && policy.read_only) return error("read_only_editor", "/edit", "Read-only editor rejects text changes.");
    if (state.revision == std::numeric_limits<std::uint64_t>::max())
        return error("revision_exhausted", "/edit/revision", "Revision cannot wrap; recreate with a fresh owner.");
    auto result = state;
    auto begin = std::min(state.anchor, state.caret), end = std::max(state.anchor, state.caret);
    if (const auto* select = std::get_if<TextEditSelection>(&request.operation)) {
        if (!boundary(state.text, select->anchor) || !boundary(state.text, select->caret))
            return error("invalid_selection", "/edit/selection", "Selection must be in bounds at UTF-8 scalar boundaries.");
        result.anchor = select->anchor; result.caret = select->caret;
    } else if (const auto* insert = std::get_if<TextEditInsert>(&request.operation)) {
        if (auto valid = validate_text(insert->text); !valid) return valid;
        if (insert->text.size() > max_text_edit_bytes - (state.text.size() - (end - begin)))
            return error("text_too_long", "/edit/text", "Replacement exceeds the 4096-byte buffer limit.");
        result.text.replace(begin, end - begin, insert->text);
        result.anchor = result.caret = begin + insert->text.size();
    } else if (const auto* erase = std::get_if<TextEditErase>(&request.operation)) {
        if (erase->direction != TextErase::backward && erase->direction != TextErase::forward)
            return error("invalid_operation", "/edit/operation", "Unknown erase direction.");
        if (begin == end) {
            if (erase->direction == TextErase::backward) begin = previous(state.text, begin);
            else end = next(state.text, end);
        }
        result.text.erase(begin, end - begin); result.anchor = result.caret = begin;
    } else {
        const auto& move = std::get<TextEditMove>(request.operation);
        switch (move.direction) {
        case TextMove::left: result.caret = !move.extend && begin != end ? begin : previous(state.text, state.caret); break;
        case TextMove::right: result.caret = !move.extend && begin != end ? end : next(state.text, state.caret); break;
        case TextMove::home: result.caret = 0; break;
        case TextMove::end: result.caret = state.text.size(); break;
        default: return error("invalid_operation", "/edit/operation", "Unknown caret direction.");
        }
        if (!move.extend) result.anchor = result.caret;
    }
    ++result.revision;
    return {std::move(result), {}};
}
} // namespace tessera
