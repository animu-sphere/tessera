#include <tessera/input/text_edit.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;
namespace {
template<class T> void apply(TextEditState& state, T operation, TextEditPolicy policy = {}) {
    auto result = edit_text(state, {state.owner, state.revision, std::move(operation)}, policy);
    check(bool(result), "Valid logical edit rejected");
    state = std::move(*result.value);
}
void scalar_edits() {
    auto state = *make_text_edit_state(1, "A日本😀Z").value;
    check(state.caret == 12 && state.anchor == 12 && state.revision == 1, "Initial byte indices");
    apply(state, TextEditMove{TextMove::left});
    check(state.caret == 11, "Move past ASCII");
    apply(state, TextEditMove{TextMove::left, true});
    check(state.anchor == 11 && state.caret == 7, "Supplementary scalar selection");
    apply(state, TextEditInsert{"語"});
    check(state.text == "A日本語Z" && state.caret == 10, "Replace reverse selection");
    apply(state, TextEditErase{});
    check(state.text == "A日本Z" && state.caret == 7, "Backspace removes one scalar");
    apply(state, TextEditMove{TextMove::home});
    apply(state, TextEditErase{TextErase::forward});
    check(state.text == "日本Z" && state.caret == 0, "Forward deletion");
    apply(state, TextEditMove{TextMove::right, true});
    check(state.caret == 3 && state.anchor == 0, "Extended scalar move");
    apply(state, TextEditMove{TextMove::right});
    check(state.caret == 3 && state.anchor == 3, "Right collapses selection");
    apply(state, TextEditSelection{7, 0});
    apply(state, TextEditMove{TextMove::left});
    check(state.anchor == 0 && state.caret == 0, "Left collapses reverse selection");
    apply(state, TextEditMove{TextMove::end, true});
    apply(state, TextEditErase{});
    check(state.text.empty() && state.anchor == 0 && state.caret == 0, "Erase selected buffer");
    apply(state, TextEditErase{});
    apply(state, TextEditErase{TextErase::forward});
    apply(state, TextEditMove{TextMove::right});
    check(state.text.empty(), "Empty boundaries are safe");
}
void rejected_requests() {
    const auto state = *make_text_edit_state(11, "é😀").value;
    auto reject = [&](TextEditRequest request, std::string_view code, std::string_view path,
                      TextEditPolicy policy = {}) {
        auto result = edit_text(state, request, policy);
        check(!result && has(result.diagnostics, code, path), "Located rejection required");
        check(state == *make_text_edit_state(11, "é😀").value, "Pure rejection preserves observation");
    };
    reject({12, 1, TextEditInsert{"x"}}, "stale_owner", "/edit/owner");
    reject({11, 0, TextEditInsert{"x"}}, "stale_revision", "/edit/revision");
    for (const auto index : {1u, 3u, 4u, 5u, 7u, std::numeric_limits<unsigned>::max()})
        reject({11, 1, TextEditSelection{index, 0}}, "invalid_selection", "/edit/selection");
    reject({11, 1, TextEditInsert{std::string("\xc0\xaf", 2)}}, "invalid_utf8", "/edit/text");
    reject({11, 1, TextEditInsert{"\n"}}, "multiline_text", "/edit/text");
    reject({11, 1, TextEditInsert{"\r"}}, "multiline_text", "/edit/text");
    reject({11, 1, TextEditInsert{std::string(max_text_edit_bytes, 'a')}}, "text_too_long", "/edit/text");
    reject({11, 1, TextEditMove{static_cast<TextMove>(99)}}, "invalid_operation", "/edit/operation");
    reject({11, 1, TextEditErase{static_cast<TextErase>(99)}}, "invalid_operation", "/edit/operation");
    reject({11, 1, TextEditSelection{}}, "disabled_editor", "/edit", {false});
    reject({11, 1, TextEditInsert{"x"}}, "read_only_editor", "/edit", {true, true});
    reject({11, 1, TextEditErase{}}, "read_only_editor", "/edit", {true, true});
    check(bool(edit_text(state, {11, 1, TextEditMove{}}, {true, true})), "Read-only navigation allowed");
    auto moved = *edit_text(state, {11, 1, TextEditMove{TextMove::home}}).value;
    check(!edit_text(moved, {11, 1, TextEditInsert{"x"}}), "Selection-only changes invalidate old requests");
    moved.revision = std::numeric_limits<std::uint64_t>::max();
    check(has(edit_text(moved, {11, moved.revision, TextEditInsert{"x"}}).diagnostics, "revision_exhausted",
              "/edit/revision"),
          "Revision cannot wrap");
    moved.anchor = 1;
    check(!edit_text(moved, {11, moved.revision, TextEditMove{}}), "Malformed observations reject");
    check(!make_text_edit_state(0, "x") && !make_text_edit_state(1, "x\n"), "Creation validation");
    auto full = *make_text_edit_state(1, std::string(max_text_edit_bytes, 'x')).value;
    apply(full, TextEditSelection{0, full.text.size()});
    apply(full, TextEditInsert{std::string(max_text_edit_bytes, 'y')});
    check(full.text.size() == max_text_edit_bytes, "Bounds account for selected replacement bytes");
}
void japanese_composition() {
    auto state = *make_text_edit_state(21, "名前😀").value;
    apply(state, TextEditSelection{3, 6}); // Replace 前 through the composition.
    const auto selected = state;
    apply(state, TextCompositionStart{});
    check(state.composition && state.composition->id == state.revision && state.composition->begin == 3 &&
              state.composition->end == 6 && state.text == selected.text && state.anchor == 3 && state.caret == 6,
          "Start captures the selection without changing committed bytes");
    const auto id = state.composition->id;
    apply(state, TextCompositionUpdate{id, "にほ", 6});
    apply(state, TextCompositionUpdate{id, "にほんご", 12});
    check(state.text == "名前😀" && state.composition->preedit == "にほんご", "Updates change only preedit");
    auto shown = *display_text_edit(state).value;
    check(shown.composing && shown.text == "名にほんご😀" && shown.preedit_begin == 3 && shown.preedit_end == 15 &&
              shown.caret == 15 && shown.anchor == 15,
          "Display splices preedit over the captured range");
    apply(state, TextCompositionUpdate{id, "日本語", 3});
    check(display_text_edit(state).value->caret == 6, "Display caret follows the preedit caret");
    apply(state, TextCompositionCommit{id, "日本語"});
    check(!state.composition && state.text == "名日本語😀" && state.anchor == 12 && state.caret == 12,
          "Commit replaces the original range once");
    check(!edit_text(state, {21, state.revision, TextCompositionCommit{id, "x"}}) &&
              has(edit_text(state, {21, state.revision, TextCompositionCommit{id, "x"}}).diagnostics, "no_composition",
                  "/edit/composition"),
          "A duplicate commit for a closed composition fails");
    const auto plain = *display_text_edit(state).value;
    check(!plain.composing && plain.text == state.text && plain.caret == 12, "Display without composition");

    const auto before = state;
    apply(state, TextCompositionStart{});
    const auto second = state.composition->id;
    check(second != id, "Composition IDs are never reused within an owner");
    apply(state, TextCompositionUpdate{second, "😀", 4});
    apply(state, TextCompositionCancel{second});
    check(!state.composition && state.text == before.text && state.anchor == before.anchor &&
              state.caret == before.caret && state.revision == before.revision + 3,
          "Cancel discards preedit without a committed edit");
}
void composition_rejections() {
    auto state = *make_text_edit_state(31, "ab").value;
    apply(state, TextCompositionStart{});
    const auto open = state;
    const auto id = open.composition->id;
    auto reject = [&](decltype(TextEditRequest::operation) operation, std::string_view code, std::string_view path,
                      TextEditPolicy policy = {}, std::uint64_t revision = 0) {
        auto result = edit_text(open, {31, revision ? revision : open.revision, std::move(operation)}, policy);
        check(!result && has(result.diagnostics, code, path), "Located composition rejection required");
    };
    reject(TextEditInsert{"x"}, "composition_active", "/edit/operation");
    reject(TextEditMove{}, "composition_active", "/edit/operation");
    reject(TextEditSelection{0, 0}, "composition_active", "/edit/operation");
    reject(TextCompositionStart{}, "composition_active", "/edit/operation");
    reject(TextCompositionUpdate{id + 1, "x"}, "stale_composition", "/edit/composition");
    reject(TextCompositionUpdate{id, "x"}, "stale_revision", "/edit/revision", {}, open.revision - 1);
    reject(TextCompositionUpdate{id, "\xe3\x81", 0}, "invalid_utf8", "/edit/preedit");
    reject(TextCompositionUpdate{id, "a\nb", 0}, "multiline_text", "/edit/preedit");
    reject(TextCompositionUpdate{id, "あ", 1}, "invalid_composition", "/edit/caret");
    reject(TextCompositionUpdate{id, "あ", 4}, "invalid_composition", "/edit/caret");
    reject(TextCompositionUpdate{id, std::string(max_text_edit_bytes - 1, 'x')}, "text_too_long", "/edit/preedit");
    reject(TextCompositionCommit{id, std::string(max_text_edit_bytes - 1, 'x')}, "text_too_long", "/edit/text");
    reject(TextCompositionUpdate{id, "x"}, "read_only_editor", "/edit", {true, true});
    reject(TextCompositionCommit{id, "x"}, "disabled_editor", "/edit", {false});
    check(bool(edit_text(open, {31, open.revision, TextCompositionCancel{id}}, {false})) &&
              bool(edit_text(open, {31, open.revision, TextCompositionCancel{id}}, {true, true})),
          "Cancel closes a session under disabled or read-only policy");
    check(
        has(edit_text(*make_text_edit_state(31, "ab").value, {31, 1, TextCompositionStart{}}, {true, true}).diagnostics,
            "read_only_editor", "/edit"),
        "Read-only editors reject composition start");
    check(has(edit_text(*make_text_edit_state(31, "ab").value, {31, 1, TextCompositionCancel{1}}).diagnostics,
              "no_composition", "/edit/composition"),
          "Cancel without composition fails");
    auto malformed = open;
    malformed.composition->begin = 1;
    check(has(validate(malformed), "invalid_composition", "/edit/state/composition") && !display_text_edit(malformed),
          "Composition must retain the captured selection");
    malformed = open;
    malformed.composition->id = open.revision + 1;
    check(has(validate(malformed), "invalid_composition", "/edit/state/composition"),
          "Composition cannot start after its observation");
    malformed = open;
    malformed.composition->preedit = "あ";
    malformed.composition->caret = 2;
    check(has(validate(malformed), "invalid_composition", "/edit/state/composition/caret"),
          "Observed preedit caret must lie on a scalar boundary");
    check(validate(open).empty(), "Open composition observations validate");
}
}
int main() {
    try {
        scalar_edits();
        rejected_requests();
        japanese_composition();
        composition_rejections();
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
