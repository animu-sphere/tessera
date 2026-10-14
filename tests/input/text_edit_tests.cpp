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
    check(bool(result), "Valid logical edit rejected"); state = std::move(*result.value);
}
void scalar_edits() {
    auto state = *make_text_edit_state(1, "A日本😀Z").value;
    check(state.caret == 12 && state.anchor == 12 && state.revision == 1, "Initial byte indices");
    apply(state, TextEditMove{TextMove::left}); check(state.caret == 11, "Move past ASCII");
    apply(state, TextEditMove{TextMove::left, true});
    check(state.anchor == 11 && state.caret == 7, "Supplementary scalar selection");
    apply(state, TextEditInsert{"語"}); check(state.text == "A日本語Z" && state.caret == 10, "Replace reverse selection");
    apply(state, TextEditErase{}); check(state.text == "A日本Z" && state.caret == 7, "Backspace removes one scalar");
    apply(state, TextEditMove{TextMove::home});
    apply(state, TextEditErase{TextErase::forward}); check(state.text == "日本Z" && state.caret == 0, "Forward deletion");
    apply(state, TextEditMove{TextMove::right, true}); check(state.caret == 3 && state.anchor == 0, "Extended scalar move");
    apply(state, TextEditMove{TextMove::right}); check(state.caret == 3 && state.anchor == 3, "Right collapses selection");
    apply(state, TextEditSelection{7, 0}); apply(state, TextEditMove{TextMove::left});
    check(state.anchor == 0 && state.caret == 0, "Left collapses reverse selection");
    apply(state, TextEditMove{TextMove::end, true}); apply(state, TextEditErase{});
    check(state.text.empty() && state.anchor == 0 && state.caret == 0, "Erase selected buffer");
    apply(state, TextEditErase{}); apply(state, TextEditErase{TextErase::forward});
    apply(state, TextEditMove{TextMove::right}); check(state.text.empty(), "Empty boundaries are safe");
}
void rejected_requests() {
    const auto state = *make_text_edit_state(11, "é😀").value;
    auto reject = [&](TextEditRequest request, std::string_view code, std::string_view path, TextEditPolicy policy = {}) {
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
    check(has(edit_text(moved, {11, moved.revision, TextEditInsert{"x"}}).diagnostics,
        "revision_exhausted", "/edit/revision"), "Revision cannot wrap");
    moved.anchor = 1; check(!edit_text(moved, {11, moved.revision, TextEditMove{}}), "Malformed observations reject");
    check(!make_text_edit_state(0, "x") && !make_text_edit_state(1, "x\n"), "Creation validation");
    auto full = *make_text_edit_state(1, std::string(max_text_edit_bytes, 'x')).value;
    apply(full, TextEditSelection{0, full.text.size()});
    apply(full, TextEditInsert{std::string(max_text_edit_bytes, 'y')});
    check(full.text.size() == max_text_edit_bytes, "Bounds account for selected replacement bytes");
}
}
int main() {
    try { scalar_edits(); rejected_requests(); }
    catch (const std::exception& failure) { std::cerr << failure.what() << '\n'; return 1; }
}
