#pragma once
#include <tessera/ui/document.hpp>

namespace tessera {
inline constexpr std::size_t max_text_edit_bytes = 4096;
// Whether an editing lifetime accepts line breaks. Multiple-line buffers accept LF
// only; hosts normalize CR and CRLF before submitting text.
enum class TextLines : std::uint8_t { single, multiple };
// Uncommitted IME preedit over the committed selection captured at start. The
// ID is the revision that started it, so it is unique per owner and never
// reused. Offsets are UTF-8 bytes at scalar boundaries: begin/end address the
// committed buffer, caret addresses preedit.
struct TextComposition {
    std::uint64_t id = 0;
    std::size_t begin = 0, end = 0;
    std::string preedit;
    std::size_t caret = 0;
    bool operator==(const TextComposition&) const = default;
};
// Host owns observations and issues nonzero owner IDs that are never reused,
// including after removal/recreation. Revisions cover text, selection and
// composition. Offsets are UTF-8 bytes at Unicode scalar boundaries, not
// grapheme indices.
struct TextEditState {
    std::uint64_t owner = 0, revision = 0;
    std::string text;
    std::size_t anchor = 0, caret = 0;
    std::optional<TextComposition> composition; // Committed bytes and selection stay fixed while present.
    TextLines lines = TextLines::single;        // Fixed for the editing lifetime.
    bool operator==(const TextEditState&) const = default;
};
struct TextEditSelection {
    std::size_t anchor = 0, caret = 0;
};
struct TextEditInsert {
    std::string text;
}; // Replace selected committed bytes.
// Word erasure removes the bytes a word move in that direction would cross when the
// selection is empty; a nonempty selection is erased like any other.
enum class TextErase : std::uint8_t { backward, forward, word_backward, word_forward };
struct TextEditErase {
    TextErase direction = TextErase::backward;
};
// Word moves start from the caret and classify scalars as word, punctuation, horizontal space
// or LF. word_left skips preceding spaces, then the run of the scalar class before it; an LF
// reached without skipping anything is one step of its own. word_right steps over one LF at the
// caret, or skips the run of the class at the caret and the spaces after it. Non-ASCII scalars
// other than spaces and CJK symbols count as word scalars; there is no dictionary segmentation.
enum class TextMove : std::uint8_t { left, right, home, end, word_left, word_right };
struct TextEditMove {
    TextMove direction = TextMove::left;
    bool extend = false;
};
// Capture the selection as the replacement range with an empty preedit.
struct TextCompositionStart {};
// Replace the whole preedit; never changes committed bytes.
struct TextCompositionUpdate {
    std::uint64_t id = 0;
    std::string preedit;
    std::size_t caret = 0;
};
// Replace the captured range once with the platform's result text.
struct TextCompositionCommit {
    std::uint64_t id = 0;
    std::string text;
};
// Discard preedit and keep the committed buffer and selection. Allowed under
// read-only and disabled policy so hosts can always close a session.
struct TextCompositionCancel {
    std::uint64_t id = 0;
};
struct TextEditRequest {
    std::uint64_t owner = 0, revision = 0;
    std::variant<TextEditSelection, TextEditInsert, TextEditErase, TextEditMove, TextCompositionStart,
                 TextCompositionUpdate, TextCompositionCommit, TextCompositionCancel>
        operation;
};
struct TextEditPolicy {
    bool enabled = true;
    bool read_only = false;
};
// Presented string: committed text with any preedit spliced over its captured
// range. preedit_begin/end bound the preedit bytes; anchor/caret address this
// string and collapse to the preedit caret during composition.
struct TextEditDisplay {
    std::string text;
    std::size_t anchor = 0, caret = 0;
    std::size_t preedit_begin = 0, preedit_end = 0;
    bool composing = false;
};
// Pure bounded transforms; failures return no candidate. No UI
// traversal, callbacks, history, normalization, platform input or publication.
// No clipboard, grapheme navigation or automatic Text editability. Ordinary
// selection/insert/erase/move requests fail while a composition is open.
Result<TextEditState> make_text_edit_state(std::uint64_t owner, std::string text, TextLines lines = TextLines::single);
Result<TextEditState> edit_text(const TextEditState&, const TextEditRequest&, TextEditPolicy = {});
// The offsets a word_left or word_right move reaches from `offset`, a scalar boundary of valid UTF-8.
std::size_t text_word_left(std::string_view text, std::size_t offset);
std::size_t text_word_right(std::string_view text, std::size_t offset);
// Observation checks shared with edit_text; empty means display_text_edit is valid.
std::vector<Diagnostic> validate(const TextEditState&);
Result<TextEditDisplay> display_text_edit(const TextEditState&);
} // namespace tessera
