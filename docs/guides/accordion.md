# Accordion

`authoring::Accordion` stacks collapsible sections, each with a disclosure header and content that is composed only while the section is expanded. It composes ordinary Boxes and Text. The [Inspector](inspector-hierarchy.md#help-topics) uses one for its help topics below the property card.

## Compose controlled sections

```cpp
namespace ui = tessera::authoring;
auto help = ui::Accordion({
    {"items", "Items", "help.items", state.open.contains("items"), false,
     {ui::Text("Drag rows or Alt+Up/Down to reorder.")}},
    {"editing", "Text editing", "help.editing", state.open.contains("editing"), false,
     {ui::Text("Arrows, Home/End, Backspace/Delete.")}}
}, {.node = {.id = "help"}});
```

`AccordionSection` holds a key, a label, the header's action, a controlled `expanded` flag, a `disabled` flag and the content elements. The host owns every section's flag, so any number of sections may be open at once; a host that wants one open section at a time closes the others in the same command. Register every action in the validation context and command registry. Lowering and input never change expansion: the host executes the header's command and recomposes with the new flags.

The Accordion requires a stable nonempty ID and at least one section. Keys must be nonempty and unique, and every section needs a nonempty label. `CollectionProps::disabled` disables every header and `AccordionSection::disabled` one header; neither disables the content, whose controls keep their own state. Invalid input returns `accordion_id`, `accordion_empty`, `accordion_keys`, `accordion_label` or `accordion_limit` through `Session::lower`, before any binding is evaluated and with the caller's source when supplied.

## Generated parts

With Accordion ID `id`, each section is a `<id>-section-<key>` Column with a 4-unit gap holding:

- `<id>-header-<key>`: a focusable Row with centered alignment and a 6-unit gap that requests the section's action. It holds the presentational `<id>-marker-<key>` Text, `[-]` while expanded and `[+]` while collapsed, then the `<id>-label-<key>` Text naming it.
- `<id>-panel-<key>`: a Column holding the content, present only while the section is expanded.

The Accordion itself is a Column with a 4-unit gap unless `Props::style` sets one. Reserve all generated IDs. Generated parts inherit the caller source. Header direction, alignment and gap are instance declarations; padding, colors, borders and the panel's gap and padding are left to stylesheet rules on the generated IDs, so hover, press and focus feedback can be added without replacing the structure.

Collapsed content is not lowered: its IDs, actions and bindings are absent from the document, so references into it from outside the section, such as a Label target, fail validation while it is collapsed. A focused control inside a section that collapses disappears with it; restore focus to the header as part of the host's collapse policy if needed.

## Semantics and document vocabulary

Each header projects an ordinary `button` named by its label through `labelled_by`, with `expanded` set to the section's flag and, while enabled, one `activate` action. The marker is presentational. The section, header row and panel add no other entries; the content keeps its ordinary semantics. No region role or controls relationship is introduced.

In UI JSON v1, `expanded` may now also appear on a disclosure button: a Box with an `activate` binding and no collection role, toggle role, edit role or modal, separator or scrollbar marker. Other Boxes still reject it with `collection_state`. `expand`/`collapse` bindings remain limited to tree items. Semantic JSON v1 and inspection invariants accept `expanded` on `button` entries; older readers reject such entries. Replay targets headers by the existing `button` role and name.

## Wire input

- Keep the ordinary `PointerDispatcher` and `FocusDispatcher`. Each header is an ordinary Tab stop in document order; a press focuses it and the release requests its action.
- `Activate` (Enter/Space) requests the focused header's action. Arrow keys keep ordinary geometric movement; there is no roving or Home/End behavior among headers.
- Execute returned requests with `CommandSnapshot::resolve_action` and `CommandExecutor`, then recompose. The header keeps its ID, so focus stays on it across the replacement.

## Build and check

```powershell
cmake --build build --config Debug --target tessera_authoring_tests tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^(authoring|inspector_hierarchy)$' --output-on-failure --no-tests=error
```

The [authoring fixture](../../tests/ui/authoring_tests.cpp) covers generated IDs, header state and labels, panels composed only while expanded, default styles, caller sources, button semantics with `expanded`, semantic invocation, Semantic JSON round trips, disabled flags, `accordion_*` rejection before binding evaluation and document rejection of `expanded` on inert Boxes and checkboxes. The [Inspector](inspector-hierarchy.md#help-topics) smoke covers Tab movement between headers, keyboard and pointer toggling through one command and retention across item selection.
