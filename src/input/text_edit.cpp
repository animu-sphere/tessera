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
    return offset <= text.size() &&
           (offset == text.size() || (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80);
}
std::size_t previous(const std::string& text, std::size_t offset) {
    if (offset) do {
            --offset;
        } while (!boundary(text, offset));
    return offset;
}
std::size_t next(const std::string& text, std::size_t offset) {
    if (offset < text.size()) do {
            ++offset;
        } while (!boundary(text, offset));
    return offset;
}
Result<TextEditState> validate_text(const std::string& text, TextLines lines, const std::string& path = "/edit/text") {
    if (text.size() > max_text_edit_bytes)
        return error("text_too_long", path, "String editor accepts at most 4096 UTF-8 bytes.");
    if (!detail::valid_utf8(text)) return error("invalid_utf8", path, "Expected valid UTF-8 text.");
    if (lines == TextLines::multiple) {
        if (text.find('\r') != std::string::npos)
            return error("carriage_return", path, "Multiple-line editor accepts LF line breaks only.");
    } else if (text.find_first_of("\r\n") != std::string::npos) {
        return error("multiline_text", path, "Single-line editor rejects CR and LF.");
    }
    return {TextEditState{}, {}};
}
// Whether replacing [begin, end) with `replacement` bytes stays within the buffer limit.
bool fits(const TextEditState& state, std::size_t begin, std::size_t end, std::size_t replacement) {
    return replacement <= max_text_edit_bytes - (state.text.size() - (end - begin));
}
Result<TextEditState> check_state(const TextEditState& state) {
    if (!state.owner || !state.revision)
        return error("invalid_state", "/edit/state", "Observation requires a live owner and revision.");
    if (state.lines != TextLines::single && state.lines != TextLines::multiple)
        return error("invalid_state", "/edit/state/lines", "Unknown line mode.");
    if (auto valid = validate_text(state.text, state.lines); !valid) return valid;
    if (!boundary(state.text, state.anchor) || !boundary(state.text, state.caret))
        return error("invalid_selection", "/edit/state", "Observed selection must lie on UTF-8 scalar boundaries.");
    if (const auto& composition = state.composition) {
        const auto begin = std::min(state.anchor, state.caret), end = std::max(state.anchor, state.caret);
        if (!composition->id || composition->id > state.revision || composition->begin != begin ||
            composition->end != end)
            return error("invalid_composition", "/edit/state/composition",
                         "Composition must name its starting revision and the retained selection.");
        if (auto valid = validate_text(composition->preedit, state.lines, "/edit/state/composition/preedit"); !valid)
            return valid;
        if (!boundary(composition->preedit, composition->caret))
            return error("invalid_composition", "/edit/state/composition/caret",
                         "Preedit caret must lie on a UTF-8 scalar boundary.");
        if (!fits(state, begin, end, composition->preedit.size()))
            return error("text_too_long", "/edit/state/composition/preedit",
                         "Composed text exceeds the 4096-byte buffer limit.");
    }
    return {TextEditState{}, {}};
}
std::uint64_t composition_id(const TextEditRequest& request) {
    if (const auto* update = std::get_if<TextCompositionUpdate>(&request.operation)) return update->id;
    if (const auto* commit = std::get_if<TextCompositionCommit>(&request.operation)) return commit->id;
    return std::get<TextCompositionCancel>(request.operation).id;
}
}
Result<TextEditState> make_text_edit_state(std::uint64_t owner, std::string text, TextLines lines) {
    if (!owner) return error("invalid_owner", "/edit/owner", "Host must supply a nonzero editing lifetime.");
    if (lines != TextLines::single && lines != TextLines::multiple)
        return error("invalid_lines", "/edit/lines", "Unknown line mode.");
    if (auto valid = validate_text(text, lines); !valid) return valid;
    const auto end = text.size();
    return {TextEditState{owner, 1, std::move(text), end, end, std::nullopt, lines}, {}};
}
std::vector<Diagnostic> validate(const TextEditState& state) { return check_state(state).diagnostics; }
Result<TextEditDisplay> display_text_edit(const TextEditState& state) {
    if (auto valid = check_state(state); !valid) return {std::nullopt, std::move(valid.diagnostics)};
    if (!state.composition) return {TextEditDisplay{state.text, state.anchor, state.caret}, {}};
    const auto& composition = *state.composition;
    TextEditDisplay display;
    display.text = state.text.substr(0, composition.begin) + composition.preedit + state.text.substr(composition.end);
    display.preedit_begin = composition.begin;
    display.preedit_end = composition.begin + composition.preedit.size();
    display.anchor = display.caret = composition.begin + composition.caret;
    display.composing = true;
    return {std::move(display), {}};
}
Result<TextEditState> edit_text(const TextEditState& state, const TextEditRequest& request, TextEditPolicy policy) {
    if (auto valid = check_state(state); !valid) return valid;
    if (request.owner != state.owner)
        return error("stale_owner", "/edit/owner", "Request does not address this editing lifetime.");
    if (request.revision != state.revision)
        return error("stale_revision", "/edit/revision",
                     "Request must address the observed text and selection revision.");
    const auto& operation = request.operation;
    const bool cancel = std::holds_alternative<TextCompositionCancel>(operation);
    const bool continues = cancel || std::holds_alternative<TextCompositionUpdate>(operation) ||
                           std::holds_alternative<TextCompositionCommit>(operation);
    if (!cancel && !policy.enabled)
        return error("disabled_editor", "/edit", "Disabled editor rejects editing and selection.");
    const bool mutation = std::holds_alternative<TextEditInsert>(operation) ||
                          std::holds_alternative<TextEditErase>(operation) ||
                          std::holds_alternative<TextCompositionStart>(operation) || (continues && !cancel);
    if (mutation && policy.read_only)
        return error("read_only_editor", "/edit", "Read-only editor rejects text changes.");
    if (continues) {
        if (!state.composition)
            return error("no_composition", "/edit/composition", "No composition is open for this editor.");
        if (composition_id(request) != state.composition->id)
            return error("stale_composition", "/edit/composition", "Request addresses a closed composition.");
    } else if (state.composition) {
        return error("composition_active", "/edit/operation",
                     "Commit or cancel the open composition before other edits.");
    }
    if (state.revision == std::numeric_limits<std::uint64_t>::max())
        return error("revision_exhausted", "/edit/revision", "Revision cannot wrap; recreate with a fresh owner.");
    auto result = state;
    auto begin = std::min(state.anchor, state.caret), end = std::max(state.anchor, state.caret);
    if (const auto* select = std::get_if<TextEditSelection>(&operation)) {
        if (!boundary(state.text, select->anchor) || !boundary(state.text, select->caret))
            return error("invalid_selection", "/edit/selection",
                         "Selection must be in bounds at UTF-8 scalar boundaries.");
        result.anchor = select->anchor;
        result.caret = select->caret;
    } else if (const auto* insert = std::get_if<TextEditInsert>(&operation)) {
        if (auto valid = validate_text(insert->text, state.lines); !valid) return valid;
        if (!fits(state, begin, end, insert->text.size()))
            return error("text_too_long", "/edit/text", "Replacement exceeds the 4096-byte buffer limit.");
        result.text.replace(begin, end - begin, insert->text);
        result.anchor = result.caret = begin + insert->text.size();
    } else if (const auto* erase = std::get_if<TextEditErase>(&operation)) {
        if (erase->direction != TextErase::backward && erase->direction != TextErase::forward)
            return error("invalid_operation", "/edit/operation", "Unknown erase direction.");
        if (begin == end) {
            if (erase->direction == TextErase::backward)
                begin = previous(state.text, begin);
            else
                end = next(state.text, end);
        }
        result.text.erase(begin, end - begin);
        result.anchor = result.caret = begin;
    } else if (const auto* move = std::get_if<TextEditMove>(&operation)) {
        switch (move->direction) {
            case TextMove::left:
                result.caret = !move->extend && begin != end ? begin : previous(state.text, state.caret);
                break;
            case TextMove::right:
                result.caret = !move->extend && begin != end ? end : next(state.text, state.caret);
                break;
            case TextMove::home:
                result.caret = 0;
                break;
            case TextMove::end:
                result.caret = state.text.size();
                break;
            default:
                return error("invalid_operation", "/edit/operation", "Unknown caret direction.");
        }
        if (!move->extend) result.anchor = result.caret;
    } else if (std::holds_alternative<TextCompositionStart>(operation)) {
        result.composition = TextComposition{state.revision + 1, begin, end, {}, 0};
    } else if (const auto* update = std::get_if<TextCompositionUpdate>(&operation)) {
        if (auto valid = validate_text(update->preedit, state.lines, "/edit/preedit"); !valid) return valid;
        if (!boundary(update->preedit, update->caret))
            return error("invalid_composition", "/edit/caret", "Preedit caret must lie on a UTF-8 scalar boundary.");
        if (!fits(state, begin, end, update->preedit.size()))
            return error("text_too_long", "/edit/preedit", "Composed text exceeds the 4096-byte buffer limit.");
        result.composition->preedit = update->preedit;
        result.composition->caret = update->caret;
    } else if (const auto* commit = std::get_if<TextCompositionCommit>(&operation)) {
        if (auto valid = validate_text(commit->text, state.lines); !valid) return valid;
        if (!fits(state, begin, end, commit->text.size()))
            return error("text_too_long", "/edit/text", "Replacement exceeds the 4096-byte buffer limit.");
        result.text.replace(begin, end - begin, commit->text);
        result.anchor = result.caret = begin + commit->text.size();
        result.composition.reset();
    } else {
        result.composition.reset();
    }
    ++result.revision;
    return {std::move(result), {}};
}
} // namespace tessera
