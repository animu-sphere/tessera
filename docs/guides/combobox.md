# ComboBox

`authoring::ComboBox` picks one value from a fixed list of choices, such as an enum property. A compact trigger shows the selected label; opening it shows a popup list of options below the trigger. Both parts are ordinary Boxes and Text. The [Inspector](inspector-hierarchy.md#purpose-combobox) uses it for each item's Purpose.

## Compose a controlled ComboBox

```cpp
namespace ui = tessera::authoring;
auto purpose = ui::ComboBox("Purpose", item.purpose, {
    {"default", "Default", "purpose.default"},
    {"render", "Render", "purpose.render"},
    {"proxy", "Proxy", "purpose.proxy"},
    {"guide", "Guide", "purpose.guide"}
}, "purpose.toggle", {.node = {.id = "purpose"}, .list = {.classes = {"popup"}}, .open = state.purpose_open});
auto form = ui::Column({
    ui::Text("Purpose"),
    std::move(purpose.field),   // In place, like any control.
    /* ordinary content */
    std::move(purpose.list)     // Directly under the root, after ordinary content.
});
```

`ComboBox` returns `ComboBoxParts{field, list}`. Compose `field` where the control belongs and `list` as a direct root child after ordinary content, like a [Menu](menu-context.md). `ComboBoxOption` is the same `{key, label, action, disabled}` description as `ListItem`. The host owns the selected key, the open flag, the toggle command and every option's command; register each action in the validation context and command registry. Lowering and navigation never change the selection, and an unknown selected key fails lowering instead of falling back.

The ComboBox requires a stable nonempty `node.id`, a nonempty label and at least one option. Keys must be nonempty and unique, and every option needs a nonempty label. `ComboBoxProps::disabled` disables the trigger and every option; `ComboBoxOption::disabled` disables one option. `list` supplies classes, styles and source for the popup; its ID is always generated, so supplying one is an error. Invalid input returns `combobox_*` diagnostics through `Session::lower`; `combobox_list_id` is located at the list, the others at the trigger.

The trigger is a Row with centered alignment and an 8-unit gap unless `node.style` sets them. Generated IDs are `<id>-title` (display-none, names the trigger), `<id>-value` (the selected label, growing to fill the row) and `<id>-arrow` (`v` when closed, `^` when open). The list is `<id>-list`: an anchored [Popup](popup-tooltip.md) with `side` and `gap` from the props, scroll overflow and a display-none `<id>-list-title`. It holds `<id>-option-<key>` rows, each with a `*`/blank marker and its `<id>-label-<key>` Text. The list is at least as wide as the trigger's border box through `popup_match_width`, still limited by its own size limits and the root boundary.

## Semantics and document vocabulary

The trigger projects the additive `combobox` role, named by its title, with `value` set to the selected option's label, `expanded` set to the open flag and, while enabled, one ordinary `activate` action that requests the toggle. Its Text children are presentational. An open list projects `listbox`, named by its title, without an action; each option projects `option`, named by its label, with `selected` and, while enabled, one `activate` action. Disabled and modal-blocked options remain exposed without actions or focus. A closed list is display-none and has no entries. No controls/owns relationship or active-descendant field is introduced.

In UI JSON v1, Box-only `collection_role` adds `combobox`, `listbox` and `option`. A combobox requires an activate action, `expanded` and the presented `edit_value`, and contains only noninteractive Text; it may sit inside a toolbar. A listbox needs a stable ID and `popup_anchor` and contains options and noninteractive Text; options are its direct children, contain only noninteractive Text, require an activate action and may carry `selected`, with at most one selected per listbox. Box-only boolean `popup_match_width` (absent: false) widens a bottom/top anchored popup to at least its anchor's width; it is inert without `popup_anchor` and for point-placed popups. Violations report `combobox_action`, `combobox_state`, `combobox_child`, `edit_value`, `listbox_popup`, `listbox_child`, `option_action`, `collection_parent`, `collection_selection` and `collection_state`. The roles, `value` and `expanded` round trip in Semantic JSON v1, Replay v1/v2 and inspection/invariant targets. Older readers reject the additions; existing documents keep their encoding.

## Wire input

Include [popup.hpp](../../include/tessera/input/popup.hpp) and retain a `ComboBoxDispatcher` constructed with the trigger's ID, plus ordinary `PointerDispatcher`/`FocusDispatcher` instances.

1. Refresh the dispatcher on every accepted view, including closed ones and views without the ComboBox, and apply its `focus`. Opening requests the selected option when it is eligible, otherwise the first eligible option. Closing returns focus to an eligible trigger. A `dismiss` from refresh means the list's trigger disappeared or became ineligible while open: close it in host state and publish.
2. Dispatch before ordinary input, passing current-snapshot focus. Keep at most one menu or list open and offer input to the open one; a closed ComboBox only reacts to Alt+Down.
3. The trigger is an ordinary Tab stop. Enter/Space, gamepad `Activate` or a pointer press and release request its toggle action. Non-repeating Alt+Down on the focused trigger returns `open` and `consumed`: run the same toggle command. Plain Up/Down keep ordinary navigation and never change the value.
4. Inside the list, Up/Down move between eligible options without wrapping and unmodified Home/End reach the first/last, all without requests; Left/Right do nothing. Pointer hover requests an eligible option's focus. `Activate` or a primary press and release request the option's action through ordinary dispatch.
5. After an accepted option command, store the key, close in host state and publish; refresh then restores trigger focus. A rejected command must not close the list as if a choice had been made.
6. Escape, `Cancel`, Alt+Up and presses outside the list, including on the trigger, return consumed dismissal with trigger focus. Reset ordinary pointer presses so later releases cannot click through. Tab/Shift-Tab or `FocusNext`/`FocusPrevious` return **unconsumed** dismissal: close, publish and refresh first, then forward that one sequential command from the trigger.

Replay plays normalized `Navigate`/`Activate` and semantic action steps through the same focus dispatcher; host-built replacement views carry the accepted selection and open state.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_combobox_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(combobox|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [ComboBox fixture](../../tests/input/combobox_tests.cpp) covers composition, popup placement below the trigger and width matching inside the boundary, semantics of the closed and open parts, disabled options and ComboBoxes, UI/Semantic JSON, inspection/invariant targets, Replay v1/v2, Alt+Down opening only on the focused trigger, opening at the selected or first eligible option, non-wrapping movement and Home/End, disabled-option skipping, activation, every dismissal path, hover focus, trigger focus on close and lowering/document/semantic rejection.

Editable or filtering comboboxes, type-ahead, multiple selection, grouped or separator options, opening on plain Down, a controls/active-descendant relationship, native accessibility and physical-input validation are not included. [Support](../reference/support-matrix.md) owns validation scope.
