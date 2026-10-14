#pragma once
#include <tessera/ui/document.hpp>

namespace tessera {
inline constexpr std::size_t max_text_edit_bytes = 4096;
// Host owns observations and issues nonzero owner IDs that are never reused,
// including after removal/recreation. Revisions cover both selection and text.
// Offsets are UTF-8 bytes at Unicode scalar boundaries, not grapheme indices.
struct TextEditState {
    std::uint64_t owner = 0, revision = 0;
    std::string text;
    std::size_t anchor = 0, caret = 0;
    bool operator==(const TextEditState&) const = default;
};
struct TextEditSelection { std::size_t anchor = 0, caret = 0; };
struct TextEditInsert { std::string text; }; // Replace selected committed bytes.
enum class TextErase : std::uint8_t { backward, forward };
struct TextEditErase { TextErase direction = TextErase::backward; };
enum class TextMove : std::uint8_t { left, right, home, end };
struct TextEditMove { TextMove direction = TextMove::left; bool extend = false; };
struct TextEditRequest {
    std::uint64_t owner = 0, revision = 0;
    std::variant<TextEditSelection, TextEditInsert, TextEditErase, TextEditMove> operation;
};
struct TextEditPolicy { bool enabled = true; bool read_only = false; };
// Pure bounded single-line transforms; failures return no candidate. No UI
// traversal, callbacks, history, normalization, platform input or publication.
// No composition, clipboard, grapheme navigation or automatic Text editability.
Result<TextEditState> make_text_edit_state(std::uint64_t owner, std::string text);
Result<TextEditState> edit_text(const TextEditState&, const TextEditRequest&, TextEditPolicy = {});
} // namespace tessera
