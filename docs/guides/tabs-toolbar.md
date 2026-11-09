# Tabs and Toolbar

`authoring::Tabs` switches one host-owned panel between named sections, and `authoring::Toolbar` groups ordinary controls behind a single Tab stop. Both compose ordinary Boxes and Text. The [Inspector](inspector-hierarchy.md#property-sections) uses Tabs for its General/Display property sections and a Toolbar for its header [item tools](inspector-hierarchy.md#item-tools).

## Compose controlled tabs

```cpp
namespace ui = tessera::authoring;
auto sections = ui::Tabs("Property sections", state.section, {
    {"general", "General", "section.general"},
    {"display", "Display", "section.display"}
}, {panel_for(state.section)}, {.node = {.id = "sections"}});
```

`TabOption` is the same `{key, label, action, disabled}` description as `ListItem`. The host owns the selected key and every tab's command, and supplies only the selected tab's panel content; register each action in the validation context and command registry. Lowering and navigation never change the selection. Exactly one tab is selected: an unknown key fails lowering instead of falling back.

Tabs require a stable nonempty ID, a nonempty label and at least one tab. Keys must be nonempty and unique, and every tab needs a nonempty label. `CollectionProps::disabled` disables every tab but not the panel; `TabOption::disabled` disables one tab. Invalid input returns `tabs_*` diagnostics through `Session::lower`.

The element is a Column with an 8-unit gap unless `Props::style` sets a direction or gap. Generated IDs are `<id>-list`, `<id>-title`, `<id>-tab-<key>`, `<id>-label-<key>` and `<id>-panel`. The list is a Row with a 2-unit gap; its display-none title names it. Each tab is a Box with 4/10-unit padding and a 2-unit bottom border that is blue for the selected tab and transparent otherwise; its label Text is its only child. The tab's padding, border and border color are local declarations, so stylesheet rules can add hover/active backgrounds, text color or an outline but do not replace the indicator. The panel is a Column holding the supplied elements.

## Compose a toolbar

```cpp
auto tools = ui::Toolbar("Item tools", {
    ui::Button("Item actions", "menu.open", {.node = {.id = "actions"}}),
    ui::Button("Rename", "item.rename", {.node = {.id = "tool-rename"}}),
    ui::Button("Reset", "item.reset", {.node = {.id = "tool-reset"}})
}, {.id = "item-tools"});
```

A toolbar is a Row with centered alignment and a 4-unit gap unless `Props::style` sets them; it is always horizontal. It requires a stable nonempty ID and a nonempty label, reported as `toolbar_id`/`toolbar_label`. Its display-none `<id>-title` names it. Controls keep their own actions, roles and state, so a Menu trigger, Checkbox or Switch works as usual inside a toolbar. Collections (ListView, TreeView, Menu, RadioGroup, Tabs) and nested toolbars are rejected.

## Semantics and document vocabulary

The tab list projects the additive `tab_list` role, named by its title, without an action. Each tab projects `tab`, named only by its label through `labelled_by`, with `selected` and one ordinary `activate` action while enabled. Disabled and modal-blocked tabs remain exposed without actions or focus. The panel and its content keep their ordinary semantics; no tab panel role or controls relationship is introduced.

The toolbar projects the additive `toolbar` role, named by its title, without an action or focus. Its controls are ordinary entries parented to it.

In UI JSON v1, Box-only `collection_role` adds `tab_list`, `tab` and `toolbar`. Tabs are direct children of a `tab_list`, require an activate action and may carry `selected`; at most one is selected per list. A tab list contains tabs and noninteractive Text, and a tab contains only noninteractive Text. Neither a tab list nor a toolbar may have an activate action, and no collection role may appear inside a toolbar. Violations report `collection_parent`, `tab_child`, `tab_action`, `collection_selection`, `collection_action` and `toolbar_child`. The three roles round trip in Semantic JSON v1, Replay v1/v2 and inspection/invariant targets. Older readers reject the additions; existing documents keep their encoding.

## Wire input

- Keep the ordinary `PointerDispatcher` and `FocusDispatcher`. The host focuses a pressed tab or tool; release requests its action.
- Translate arrows and gamepad directions to `Navigate`.
- On a focused tab, Left/Right move to the previous/next eligible tab of its list, wrapping, and unmodified Home/End reach the first/last. Movement to a different tab also returns that tab's `activate` request, so selection follows focus through one ordinary command; staying on the same tab requests nothing. Up/Down keep ordinary geometric movement, for example from a tab down into its panel, and never select.
- On a focused toolbar control, Left/Right move to the previous/next eligible control of that toolbar, wrapping, and Home/End reach the first/last. Toolbar movement requests no action. Up/Down keep geometric movement. Route keys to a control's own editor or dispatcher first when it consumes them, as for text and sliders elsewhere.
- `Activate` requests the focused tab's or control's own action.
- `FocusNext`/`FocusPrevious` enter each tab list once, at its selected tab when eligible, otherwise at its first eligible tab, and each toolbar once at its first eligible control, even when a later toggle is checked. From any tab or control in the group, either direction leaves it. A group with no eligible members has no stop.
- Execute returned requests with `CommandSnapshot::resolve_action` and `CommandExecutor`, then recompose with the new selected key and its panel. A rejected command leaves focus on the new tab while the accepted selection and panel stay unchanged.

Replay plays normalized `Navigate`/`Activate` and semantic action steps through the same dispatcher; host-built replacement views carry the accepted selection.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_tabs_tests tessera_toolbar_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(tabs|toolbar|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [Tabs fixture](../../tests/input/tabs_tests.cpp) covers composition, the selected indicator, semantics, disabled tabs and lists, UI/Semantic JSON, inspection/invariant targets, Replay v1/v2, Tab entry/exit through the panel, wrapping Left/Right with selection requests, Home/End, geometric Up/Down and lowering/document rejection. The [Toolbar fixture](../../tests/input/toolbar_tests.cpp) covers composition, semantics, codecs and Replay, single-stop entry that ignores checked toggles, wrapping movement without requests, disabled controls, own-action activation and rejection of collections, nested toolbars and toolbar actions.

Manual activation (focus without selection), vertical tab lists or toolbars, closable/reorderable/overflowing tabs, a tab panel role, remembered toolbar focus, type-ahead, native accessibility and physical-input validation are not included. [Support](../reference/support-matrix.md) owns validation scope.
