# TextEditor

`authoring::TextEditor` presents a host-owned multiple-line `TextEditState` as a labelled editor. Its text may contain LF line breaks, wraps to the field width and grows with its rows. The [Inspector](inspector-hierarchy.md#notes-text-editor) uses one for each item's Notes.

## Compose an editor

```cpp
namespace ui = tessera::authoring;
auto notes = take(make_text_edit_state(owner, "First line\nSecond", TextLines::multiple));
auto editor = ui::TextEditor("Notes", notes, "item.notes", {.node = {.id = "notes"}});
```

The editor takes the same label, `Value<TextEditState>` and `TextEditorProps::disabled` (the `TextInputProps` type) as a [TextInput](text-number-input.md), and the same stable nonempty ID requirement (`text_editor_id`). It generates the same parts: a Column of `<id>-label`, `<id>-value` (the presented text) and an unnamed `Selection <anchor>:<caret>` Text, which reads `Composition <begin>:<end> caret <caret>` during composition.

The state must be created with `TextLines::multiple`. Lowering rejects a single-line state at the value Text's `text` property with `edit_lines`, and TextInput and NumberInput reject a multiple-line state the same way. Other malformed observations fail with the [logical editor's](../architecture/input.md#implemented-logical-string-editing) diagnostics, such as `carriage_return`.

## Line breaks and limits

A multiple-line buffer accepts LF only. Insertions, preedit and commit text containing CR fail with `carriage_return`; hosts normalize CRLF and lone CR to LF before submitting text, for example on paste. The 4096-byte UTF-8 limit covers the whole buffer, including its line breaks. Selection and caret offsets stay UTF-8 bytes at scalar boundaries, and an LF is one ordinary byte: Backspace at a line start joins the lines.

Home and End in a `TextEditMove` still address the buffer ends. Row-aware movement is geometric and belongs to the host, below.

## Words

`TextMove::word_left`/`word_right` move the caret by word, keeping the anchor when `extend` is set, and `TextErase::word_backward`/`word_forward` erase the bytes the same move would cross when the selection is empty; a nonempty selection is erased like any other. Each is a logical operation on the buffer, so TextInput and NumberInput drafts accept them too. `text_word_left` and `text_word_right` return the reached offsets.

Scalars are classified as word, punctuation, horizontal space or LF. A move to the left skips the preceding spaces and then the run of the class before them; an LF reached without skipping anything is one step of its own, so the caret stops at a line start before joining the previous line. A move to the right steps over one LF at the caret, or skips the run of the class at the caret and the spaces after it. Letters, digits and `_` are word scalars; other ASCII, general punctuation, CJK symbols and full-width ASCII punctuation are punctuation; other non-ASCII scalars count as word scalars. There is no dictionary segmentation, so a run of Japanese text without punctuation is one word.

## Caret geometry

`layout_text_carets(shaper, text, style, constraints)` in `<tessera/text/text_caret.hpp>` returns the `TextCaretLayout` of one Text box. Pass the same text, resolved `TextStyle` and content width that layout and paint use. Each `TextCaretLine` is one visual row: LF ends a hard line and wrapping ends a soft one, which is where the next row begins. Stops are shaped cluster starts plus each hard line's end, with x taken from glyph pen positions or the line's measured advance.

- `line_of(offset)` returns the presenting row; a wrap boundary belongs to the following row.
- `caret(offset)` is a 1-unit-wide, line-height-tall rectangle relative to the Text box, for candidate-window placement or a host-drawn caret.
- `offset_at(row, x)` and `offset_at(point)` return the nearest stop, clamped to the existing rows.
- `line_end(row)` is the last offset that presents on that row: a soft row ends at its last stop.
- `range(begin, end)` returns one rectangle per row presenting part of the byte range, in either order. A row continued by the range reaches its `right` edge, the measured end of its text; a selected LF adds half a line height, so selected breaks and empty rows stay visible.

Geometry assumes left-to-right rows and does not reorder bidirectional text; offsets inside a cluster present at the cluster start.

## Paint the caret and selection

The host presents editing state through `PaintInput::text_editing`, at most one `TextEditingPaint` per Text node. Give the editor's `<id>-value` node, `selection` rectangles from `range(anchor, caret)` and the `caret` rectangle from `caret(caret)`, all relative to the value's content box, with their colors. Paint draws the selection after the node's background and border and before its glyphs, and the caret after them, with the node's opacity, visibility and recorded clip, so the presentation stays inside a scrolled card and below popups. During composition a host can pass the preedit range as a thin underline instead of a selection. Present nothing for an editor without focus.

Editing presentation is host state: it is not part of the document, semantics or Replay, so [snapshot bundles](inspector-hierarchy.md#snapshot-bundles) replayed from a recording show no caret.

## Place the caret with the pointer

`TextPointerDispatcher` in `<tessera/input/text_pointer.hpp>` captures one pointer over every TextInput and TextEditor in a snapshot. Dispatch it before ordinary pointer routing.

- A primary press on the editor or one of its parts, other than `<id>-label`, captures the pointer, returns the owner as `focus` and reports the press position as `point`, with `extend` when Shift is held. `text_pointer_target` answers whether a position would start one, for example to show an I-beam cursor. Disabled editors are not targets.
- Each move of the captured pointer and its primary release report the position with `extend`, so the press stays the anchor. Positions outside the editor still report; caret lookup clamps them to its rows.
- `value` names the `<id>-value` Text. Map `point` into its content box, take `offset_at` from its caret layout and request `TextEditSelection{extend ? anchor : offset, offset}` through the editor's command. Skip a selection equal to the observed one and do not request one while a composition is open.
- `PointerCancel` ends capture and keeps the last selection. Keyboard, text and logical input end capture unconsumed; KeyUp, Scroll and other pointers pass through. Call `refresh` at each accepted replacement: capture survives edits and ends when the editor disappears, changes command or becomes ineligible.

## Semantics and document vocabulary

A TextEditor projects the additive `text_editor` role, named by its label through `labelled_by`, and carries `value`: the presented text with its LF bytes, including any preedit. While enabled it exposes focus and one ordinary `activate` action; a disabled editor stays exposed with its value but without actions or focus. Label, value and selection Text are presentational.

In UI JSON v1, `edit_role` gains `text_editor` with the same `edit_value` and activate requirements as the other edit roles. The role and its value round trip in Semantic JSON v1, Replay v1/v2 and inspection/invariant targets; JSON strings escape LF as `\u000a`. Older readers reject the new role.

## Wire input

- Keep the ordinary `PointerDispatcher` and `FocusDispatcher`. The editor is one Tab stop, and Tab still moves focus rather than inserting text. Activating it supplies no edit payload.
- While it is focused, map committed `TextInput`, Backspace/Delete, Left/Right and Control+A to revision-addressed `TextEditRequest`s as for a TextInput, before navigation. Control+Left/Right map to word moves, extending with Shift, and Control+Backspace/Delete to word erasure.
- Map Enter (optionally with Shift) to `TextEditInsert{"\n"}` and Control+Home/End to buffer-end `TextEditMove`s.
- For Home/End and Up/Down, build the caret layout from the accepted view and request a `TextEditSelection` at the row start, `line_end` or `offset_at(row ± 1, x)`, keeping the anchor when Shift is held. Retain the x where vertical movement began while the state keeps the revision that movement produced, so a short row does not lose the column.
- Dispatch pointers through `TextPointerDispatcher` before ordinary routing, as above.
- Execute each request through the editor's command and recompose. A rejected command leaves the accepted text unchanged.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_text_edit_tests tessera_text_caret_tests tessera_text_editor_tests tessera_text_pointer_tests tessera_paint_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(text_edit|text_caret|text_editor|text_pointer|paint|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [logical editing](../../tests/input/text_edit_tests.cpp), [caret geometry](../../tests/text/text_caret_tests.cpp), [text pointer](../../tests/input/text_pointer_tests.cpp), [paint](../../tests/render/paint_tests.cpp) and [TextEditor](../../tests/input/text_editor_tests.cpp) owner tests cover LF editing and CR rejection, line modes, word moves and erasure, hard/soft rows, stops, ranges and clamped lookups, cluster stops, pointer targets, capture and replacement, editing paint order and validation, generated parts, wrapped presentation, preedit, semantics, Tab order, UI/Semantic JSON, inspection/invariant targets, Replay v1/v2 and lowering rejection.

Double-click word selection, scrolling inside the editor or while drag-selecting, caret blinking, editing presentation in Replay, undo/redo inside the widget, grapheme or bidirectional caret movement, native editable accessibility and physical-input validation are not included. [Support](../reference/support-matrix.md) owns validation scope.
