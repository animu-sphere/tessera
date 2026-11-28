# Select and edit an Inspector item

The [reference application](../../examples/inspector-hierarchy/main.cpp) combines ordinary public C++ widgets for the [Application Experience operation](../roadmap/current.md). It edits host-owned scene-like items without an engine, OpenUSD, source editor or second runtime.

## Build and run

Use the prerequisites and core configuration from [development](development.md):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DTESSERA_TEST_NATIVE_HOST=ON
cmake --build build --config Debug --target tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^inspector_hierarchy$' --output-on-failure --no-tests=error
& .\build\Debug\tessera_inspector_hierarchy.exe --native
ctest --test-dir build -C Debug -R '^inspector_hierarchy_native$' --output-on-failure --no-tests=error
```

Use `Release` for the other configuration. Without arguments the executable runs the window-free consumer smoke. Windows `--native` opens the interactive CPU-rendered application; `--native-smoke` exercises the same window procedure with injected messages and exits. The opt-in native CTest requires a desktop session.

## Operate

- Drag a Hierarchy row other than Scene up or down to reorder Scene's children; a blue line marks the insertion point and Escape cancels. With a row focused, Alt+Up/Down moves it one place. See [Reorder the Hierarchy](#reorder-the-hierarchy).
- Select Cube or another row in the [TreeView](tree-list.md). Arrows move focus, Enter/Space selects, and Left/Right collapses/expands Scene. Disclosure never changes controlled selection. The selected row casts a small shadow onto the next row, even while that row is hovered; see [Selection elevation](#selection-elevation).
- Control+click, Shift+click or Shift+arrows to select several rows; Enabled and Reset then apply to all of them. See [Multiple selection](#multiple-selection).
- Tab to the General/Display [tabs](tabs-toolbar.md) above the properties; Left/Right switch sections. General holds Display name, Enabled, Amount, Scale, Position and Notes; Display holds Visibility, Cast shadows, Purpose and Color. See [Property sections](#property-sections).
- Tab into the General [Grid Inspector](grid.md). Edit Display name with committed text, exact Ctrl+A, arrows, Shift selection, Home/End and Backspace/Delete; Control+arrows and Control+Backspace/Delete work by word. Click to place the caret, Shift+click to extend and drag to select; the focused editor paints its caret and selection. Text editing consumes supported keys before tree navigation.
- Click a property caption, such as Display name or Scale, to focus the control beside it without changing it; see [Property captions](#property-captions).
- With Display name focused, exact non-repeating Ctrl+C copies the selected committed bytes, Ctrl+X copies and then deletes the selection, and Ctrl+V replaces the selection through the ordinary name-edit command. Windows interactive mode exchanges Unicode text with the OS clipboard. Empty selection copy/cut and empty text paste do nothing. Paste rejects CR/LF, embedded NUL, malformed Unicode and results above 4096 UTF-8 bytes, preserving the accepted name and selection; diagnostics print to stderr and the window remains usable.
- With Display name focused, type Japanese through the Windows IME. Preedit appears inline in the editor, the candidate window opens at the preedit caret, Enter commits and Escape cancels; see [Japanese IME composition](#japanese-ime-composition).
- Rejected name input, including typing beyond the 4096-byte limit, keeps the accepted text, selection and focus. Windows shows name-edit and clipboard failures in the window title, with a short reason for text validation failures. A successful name edit or clipboard operation clears the notice; navigation alone retains it. Stderr records the operation, original target item and located diagnostics. Presentation rejection also preserves the accepted inspection and view, so retry can use the retained revision.
- Activate Enabled or adjust Amount with Left/Down, Right/Up, Home/End or a pointer drag. Amount has integer stops from 0 to 100 in steps of 5.
- Tab to Scale, a [NumberInput](text-number-input.md) from 0 to 10. Type a decimal and press Enter to apply it, or Escape to restore the accepted value; Up/Down or the `-`/`+` parts step by 0.1. Drag its label or field sideways to scrub; see [Scrub numeric fields](#scrub-numeric-fields).
- Tab on to Position, a [VectorInput](vector-input.md) whose X, Y and Z are separate Tab stops from -1000 to 1000. Each edits, applies, reverts, steps by 0.1 and scrubs like Scale. See [Position vector input](#position-vector-input).
- Tab on to Notes, a multiple-line [TextEditor](text-editor.md). Enter starts a line, Up/Down move by row, Home/End move within the row and Control+Home/End to the ends; copy, cut, paste and the IME work as for Display name. See [Notes text editor](#notes-text-editor).
- On the Display tab, Tab to Visibility, which enters at the checked choice. Arrows move between Inherited, Visible and Hidden, wrapping, and select the focused choice; a pointer press selects directly. See [Visibility choice](#visibility-choice).
- Tab to Cast shadows and press Enter/Space, or click its track, thumb or label, to toggle the [Switch](checkbox-switch.md). See [Shadows switch](#shadows-switch).
- Tab to Purpose, a [ComboBox](combobox.md). Enter/Space, Alt+Down or a click opens its list at the current choice; Up/Down/Home/End move, Enter or a click chooses, and Escape, Alt+Up, Tab or an outside press closes without choosing. See [Purpose ComboBox](#purpose-combobox).
- Tab to Color, a [ColorPicker](color-picker.md) showing a swatch and `#RRGGBB`. Enter/Space, Alt+Down or a click opens its panel at the saturation/value area; arrows adjust saturation and value, Tab moves to the hue strip, whose arrows and Home/End adjust hue, and a press or drag on either applies the color under the pointer. Enter, Escape, Tab past the strip or an outside press closes the panel and keeps the applied color. See [Display color picker](#display-color-picker).
- Drag the closed Color swatch onto a Hierarchy row to copy the shown item's Display color to that item; a dashed ring marks the row and a small swatch follows the pointer. See [Copy a Display color by dragging](#copy-a-display-color-by-dragging).
- Change any property and a blue Modified badge appears on the property card's top edge; Reset, or a property returning to its reset value, removes it. See [Scale hint and Modified badge](#scale-hint-and-modified-badge).
- Select another item and return. Every item retains its own name and notes owners, revisions, UTF-8 byte selections, enabled flag, amount, accepted scale and pending scale draft, visibility, shadows value, purpose and display color. The edited display name also appears in the Hierarchy.
- Wheel either pane or drag the [SplitPane](split-pane.md) boundary. Focus navigation reveals controls; scrolling and pane/window resizing do not change item values. Pane minima are 140/320 logical units plus the 8-unit handle; smaller windows clip overflowing minima.
- Use the header [toolbar](tabs-toolbar.md): Tab enters at Item actions, and Left/Right/Home/End move to Rename and Reset without running them. Reset restores every selected item's properties except its name. See [Item tools](#item-tools).
- Press Ctrl+Z to undo the last edit and Ctrl+Y or Ctrl+Shift+Z to redo it, or use the toolbar's Undo and Redo. Typing into one field undoes as a whole, and so does a drag. Undo shows the item and tab the edit was made on. See [Undo and Redo](#undo-and-redo).
- Open Item actions, or right-click the selected-item button at the top of the Inspector. Shift+F10 opens the same context actions from that button. Rename, from the menu or the toolbar, shows General and focuses the retained name editor; Toggle enabled shares the Checkbox command. Escape/Cancel or an outside press dismisses the menu. These operations need no destructive confirmation, so no Dialog is added.

The context target is the selected-item button, not arbitrary unselected hierarchy rows. No command silently selects a different item on right-click.

## Multiple selection

The Hierarchy is a [multiselectable TreeView](tree-list.md#select-several-items). The application keeps a `CollectionSelection` (the selected keys and the anchor) and the shown item, the one the Inspector edits, which is always one of the selected keys. Every row still requests its own `select.<key>` command; the host attaches a gesture and applies it with `select_items` over `selectable_keys` of the Hierarchy items.

| Input on a Hierarchy row | Selection | Shown item |
| --- | --- | --- |
| Click, Enter, Space | Only the row | The row |
| Control+click, Control+Space | Row added or removed; it becomes the anchor | The row if added; otherwise unchanged, or the first remaining key if it was the row |
| Shift+click, Shift+Space | The rows from the anchor to the row | The row |
| Control+Shift+click | The rows from the anchor to the row, added | The row |
| Shift+Up/Down/Home/End | Focus moves as without Shift, then the rows from the anchor to the new row | The new row |
| Control+A | Every visible row added (`hierarchy.select_all`) | Unchanged |

Plain arrows still move focus without selecting. A click takes its modifiers from the release, which is when the row activates. Removing the only selected row with Control does nothing, because the Inspector always shows an item. Semantic activation and Replay use the plain gesture. Collapsing Scene keeps selected children, and Control+A keeps them too; a plain selection drops them.

Every selected row has the [raised selection appearance](#selection-elevation); a following selected row covers the previous one's shadow. The Selected button reads `Selected: <id> +<n>` when n other items are selected. It now requests `selection.show`, which changes nothing, so activating it keeps the selection. The property card, Rename, clipboard and the name and number editors work on the shown item only. Two commands apply to every selected item, each as one [history](#undo-and-redo) entry:

- `item.enabled` (the Enabled checkbox and Toggle enabled) sets every selected item to the opposite of the shown item's value, so a mixed selection becomes uniform.
- `item.reset` (the Reset tool) restores every selected item as Reset restores one.

A history entry also records the selection it applied to, and Undo and Redo restore that selection with the shown item. Dragging moves only the dragged row.

The window-free smoke checks the `multiselectable` tree, every row of the table above, the selected rows' shared appearance and the Selected label, bulk Enabled and Reset with one entry each, Undo restoring both items and selection, the Selected button keeping the selection, the kept last row, and collapsed children kept by Control+A and dropped by a plain selection. The [`multi-select` snapshot](#snapshot-bundles) shows a Shift range plus a Control+click row. The sample Windows host sends Control+Space and Shift+Space outside editors. The injected native smoke does not cover multiple selection, because the host reads modifiers from the keyboard state rather than from the injected messages.

## Reorder the Hierarchy

The Hierarchy's order is host data: Scene is the root and every other item is its child, in the order of the application's item list. A [`CollectionDragDispatcher`](tree-list.md#drag-items-to-reorder) for `hierarchy`, routed after the popup dispatchers and before the splitter, reports where a dragged row would land. Every move goes through one node-free typed command, `item.move`, resolved with `CommandSnapshot::resolve`:

| Argument | Type | Meaning |
| --- | --- | --- |
| `item` | object ID | The moved Scene child |
| `target` | object ID | The row it lands next to |
| `placement` | enumeration `before` / `after` | Which side of `target` |

The host publishes every item ID as a command object, so stale or unknown IDs fail resolution. Execution moves the item in the candidate state; Scene, a target before Scene and drops that leave the order unchanged reject with `item_move`. After the Scene row means the first child position. Moving keeps each item's data, the selection and the focused row.

- A press on a row still focuses it, and a release without 4 units of travel still selects it. Dragging Scene does nothing special: its release selects Scene as a click would.
- While dragging, the source row is dimmed to 45% opacity and a valid target shows a two-unit blue inset band on its top (`before`) or bottom (`after`) edge. Positions that would not move the item show no band. Wheel scrolling moves the band with the rows under the pointer.
- Releasing over a valid target runs `item.move` once. Releasing elsewhere, Escape, `PointerCancel` and Tab cancel without a command or selection.
- With a Scene child focused, Alt+Up and Alt+Down run the same command one place up or down, keeping focus on the row; at the ends they do nothing.

The window-free smoke checks the typed descriptor and published IDs, the threshold, dimming and insertion bands without a command, no-op positions, one command per drop with data, selection and feedback retained or cleared, the first-child position below Scene, Escape and `PointerCancel`, the immovable Scene, click selection, Alt+Up/Down with retained focus and an empty move at the top, and direct rejection of moving Scene. The injected native smoke drags Light below Camera with Windows mouse messages under OS capture. Automatic scrolling while dragging, reparenting and dragging between panes are not included.

## Undo and Redo

The application keeps its own edit history; Tessera stores none. An entry holds the accepted data of every item before and after one edit. That data is the Hierarchy order, name and notes text with their selections, and Enabled, Amount, Scale, Position, Visibility, Cast shadows, Purpose and Display color. Selection, the shown tab, help topics, Scene disclosure, popups, the pane split, scroll offsets and pending numeric drafts are view state and are never recorded.

The host does not mark commands as edits. After a command runs, the host compares the candidate's item data with the accepted data, and only a change records an entry. Selecting, Rename, a Reset that changes nothing and draft-only typing record nothing. An entry is joined by the next edit only when both have the same merge key: the command, the selected item and the input that caused it.

- Typed text (committed characters, Backspace/Delete, Enter in Notes and a committed IME composition) uses one key per field and item. Typing stays in one entry until focus moves to another control, another edit is recorded, or Undo/Redo runs. Paste and cut are always their own entries.
- Any other edit uses the input gesture that caused it. A pointer press starts a new gesture. Moves, the release and keys pressed while a pointer is down continue it, and any other event starts a new one. A Scale/Position scrub, an Amount drag or a Display color drag is therefore one entry. A gesture that returns to its start value, such as a scrub cancelled with Escape, removes its entry.
- A new entry after Undo discards the redo branch. History keeps the latest 100 entries.

`edit.undo` and `edit.redo` are ordinary commands. Their enabled state comes from the candidate's history, published with each generation as `CommandState` with "Nothing to undo." / "Nothing to redo." reasons. They are shown as Undo (`tool-undo`) and Redo (`tool-redo`) in the header toolbar, after a second VerticalSeparator (`history-separator`). Disabled tools are skipped by toolbar arrows. Windows narrower than 640 units leave out the `heading` text, which the window title repeats, so the whole toolbar fits the 492-unit minimum. Before this change, Reset was partly clipped at that width. When Undo runs out of entries while focused, focus moves to Redo, and Redo hands focus back the same way. Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z resolve the same node-free commands and repeat while held. They are routed before text editing, so they also work inside Display name, Notes and the numeric fields. While a pointer is pressed they do nothing, so an open gesture is never split.

Undo and Redo first check that the accepted data still equals the entry's expected side, and reject with `history_mismatch` if it does not. They then reapply the Hierarchy order and each changed value, restore the entry's selection and shown item, show its tab, close any popup and close the open merge. Restored text gets a new revision with the recorded selection, so requests against the replaced revision reject. A restored number replaces that field's draft with its canonical text. A field whose value is unchanged keeps its pending draft. A presentation rejection keeps the accepted data and history position and reports `Undo could not finish. Try again.` (or `Redo ...`). A later success clears the notice.

The window-free smoke checks: disabled tools and no-op shortcuts at the start; the tools inside the header at 960 and 492 units; one entry for typing, with the caret, Hierarchy label and stale-revision rejection restored; Ctrl+Y and Ctrl+Shift+Z; new entries after Redo and after a focus change; paste between typed text; no entries from navigation or presentation; returning to the item and Display tab; Reset as one entry with a pending draft kept; replaced drafts for applied numbers; scrub, cancelled scrub and Slider drags; shortcuts waiting for a pressed pointer; reorder; toolbar keyboard and pointer activation with focus hand-off; rejected presentation; and the 100-entry limit. The injected native smoke reaches Undo with Right after Reset and traverses with Control+Y/Control+Z. History is in memory only; it is not persisted, shown as a list, labelled per entry or shared between windows.

## Application appearance

The ordinary form now uses a larger heading, a white rounded Hierarchy panel, muted property labels and compact multiline help. Hierarchy rows have a 30-unit minimum height; the controlled selection has a blue fill and white text, while keyboard focus has a separate inside border and smooth outside outline. Buttons, name editing, the enabled value, rounded Slider parts and padded item menus share the blue-gray palette. The property card retains its radial highlight and outer shadow; the name input has a subtle inset shadow.

The application-local stylesheet targets public author IDs and classes, including the documented generated TreeView, Slider and Menu IDs. Hover, active and focus rules change paint only: their borders already reserve space in the base styles. The host resolves feedback against the accepted tree and layout, then prepares paint and inspection together before replacing either. Stationary hover survives ordinary view replacements through the existing compatible pointer policy; pointer release/cancellation clears active feedback, including the custom Slider drag. Hover never edits item data or publishes a command generation.

The Inspector uses a 72-unit label column and a control column with a 202-unit minimum. The 200-unit Slider track has no extra horizontal padding, so its full travel fits the existing 320-unit pane minimum, including card borders and pane/card padding. Both pane minima fit a 492-unit window including the application padding; smaller windows still clip the overflowing minima. Focus reveal and wheel scrolling retain their ordinary behavior.

The window-free smoke checks distinct hover/press/selection/focus pixels at scales 1, 1.25, 1.5 and 2, inherited selected-label color, stable row geometry, cancellation and once-only selection, stationary Slider feedback, minimum-width track containment and retained item data. Pixel comparisons omit glyph commands to isolate the changed decorations; the injected Windows smoke still presents the complete form with the existing GDI glyph adapter. Base appearance remains application styling, not a new widget skin API or text shaper; the focus decoration uses the built-in stroke below.

## Property sections

The property card below the selected-item button is an ordinary [Tabs](tabs-toolbar.md) element with stable ID `sections`, named Property sections. The General tab shows Display name, Enabled, Amount and Scale in the `properties` Grid; the Display tab shows Visibility, Cast shadows, Purpose and Color in a four-row Grid with the same ID and columns. The host stores the shown section once for the application, not per item, so it survives item selection. Each tab requests its own `section.<key>` command; selection-follows-focus Left/Right and pointer presses share that path, and the controls of the hidden section are not composed. Tabs show the generated blue bottom indicator; the stylesheet adds muted text, a blue selected label, hover/press backgrounds and a solid focus outline without replacing the indicator.

Rename, from the item menus or the toolbar, first shows General and then focuses the name editor. Host checks that compare focus with the name or Scale editor treat an absent control as unfocused, so the Display section disables text, clipboard and IME routing rather than matching a missing node. The window-free smoke checks the `tab_list`/`tab` roles, names and selected state, Tab entry at the selected tab and on into the panel, Shift+Tab return, one command per Right/Left with wrapping and panel replacement, the indicator, Down into the panel without selecting, pointer selection, retention across item selection and Rename returning to General. The injected native smoke selects Display with a Right arrow message before reaching Visibility, and checks that context Rename shows General again.

## Item tools

The header's Item actions button now belongs to an ordinary [Toolbar](tabs-toolbar.md) with stable ID `item-tools`, named Item tools, followed by Rename (`tool-rename`) and Reset (`tool-reset`) buttons and then [Undo and Redo](#undo-and-redo) in the `control` class. The toolbar is one Tab stop at Item actions, so the existing sequential order is unchanged; Left/Right/Home/End move among the enabled tools without running a command, and Enter/Space or a press activates the focused tool. Item actions still opens its Menu. Rename shares the menu's `item.rename` command. Reset requests the new `item.reset` command, which restores the selected item's Enabled, Amount (50), Scale (1, replacing any pending draft with a new revision), Visibility (Inherited), Cast shadows, Purpose (Default) and Display color (50% gray) values while keeping its name; other items are unchanged.

The window-free smoke checks the toolbar role and name, single-stop entry/exit, wrapping movement without commands, Home/End, Reset through one command after changing every property, pointer Reset limited to the selected item, toolbar Rename and the retained menu trigger. The injected native smoke reaches the toolbar with Tab, moves to Reset with Right arrow messages without changing the item and resets with Space.

An ordinary [Spacer](../architecture/ui-model.md#implemented-c-form-authoring) with ID `header-space` sits between the title and the toolbar, so the tools stay at the trailing edge of the header as the window widens. It is transparent to the pointer and adds no Tab stop or semantic entry.

A [VerticalSeparator](../architecture/ui-model.md#implemented-c-form-authoring) with ID `tools-separator` divides the Item actions menu trigger from the Rename and Reset commands. The toolbar sets `align: stretch` so the 1-unit line spans the tools' height; the equal-height buttons keep their size. Below the property card, a HorizontalSeparator with ID `help-separator` divides the card from the help text across the Inspector column. Both expose an unnamed `separator` entry without actions or focus and pass presses through, so toolbar movement, Tab order and the help's content are unchanged. The window-free smoke checks their geometry, roles, toolbar parentage, pointer transparency and the absence of `missing_name` warnings.

## Help topics

Below the help Separator, the help text is an [Accordion](accordion.md) with stable ID `help` and two topics: Items (`items`: per-item edits and reordering) and Text editing (`editing`: clipboard and caret keys), each line a muted Text. The host stores the open topics once for the application in a set that starts with Items, so they survive item selection, and each header requests its own `help.<key>` command, which opens or closes only that topic. Headers reuse the Hierarchy row feedback through application rules on their generated IDs, with muted markers and an 8-unit indent for open topics. They are ordinary Tab stops after the property card; a press focuses the header and toggles it, and Enter/Space toggle the focused one.

The window-free smoke checks the headers' `button` role, names and `expanded` state, panels composed only while open, Tab from one header to the next, keyboard and pointer toggling through one command each with focus kept on the header, independent topics and retention across item selection.

## Property captions

Each property caption in the label column is a [Label](../architecture/ui-model.md#implemented-c-form-authoring) naming the control beside it: `name-caption`, `enabled-caption`, `amount-caption`, `scale-caption`, `position-caption`, `notes-caption` on General and `visibility-caption`, `shadows-caption`, `purpose-caption`, `color-caption` on Display. A primary press on a caption focuses its control through [label press focus](../architecture/input.md#implemented-label-press-focus), as a press on the control's own field would, without running a command, opening a popup or revealing it. Position's caption names its X component, since the group itself takes no focus, and Visibility's names the checked choice, where Tab also enters. The captions keep their muted style and `text` role, and the controls keep their own names.

Both sections compose the `properties` Grid with a [PropertyGrid](grid.md#compose-property-rows) of InspectorRows: a 72-unit-maximum caption column, a control column with a 202-unit minimum, a 12-unit row gap and the `muted` caption class. Captioned rows generate their Labels, Position, Visibility and Color set their caption IDs, Enabled passes its status-mark Row as an authored caption and the scale hint is a wide row. The generated Grid has the same children, IDs, tracks and geometry as before, so the existing caption, spanning-hint and narrow-window checks of the window-free smoke cover it unchanged.

The window-free smoke checks the header Spacer's geometry against the application content box and its pointer transparency, focus after a press on every caption in both sections, no command or item change and unchanged control names.

## Scale hint and Modified badge

Below Scale and Position, the General section's `properties` Grid has a sixth row holding the `scale-hint` text, "Enter applies, Esc reverts, Up/Down or drag step.", with `grid_column_span` 2. It starts in the label column and ends with the control column, so it uses the full card width instead of wrapping inside the 72-unit label column, and its row keeps the shared 12-unit row gap. The bottom help no longer repeats it.

When the selected item differs from the values Reset restores (Enabled on, Amount 50, Scale 1 and Position 0, 0, 0 with no pending drafts, Visibility Inherited, Cast shadows on, Purpose Default, Display color 50% gray), the card's last child is a `modified-badge` Text with `position: absolute`, `top: -9` and `right: 12`. It straddles the card's top border against the card's padding box, painted after and therefore above the card's own content, without changing the card's size or moving Selected, the tabs or the Grid. It is static text: it requests no command and carries no focus stop. The badge follows the selected item's own values and survives section changes.

The window-free smoke checks the spanning hint's bounds against the label and control columns at the 960 and 492-unit windows, the badge's absence at reset values, its exact padding-box offsets, unchanged card/Selected/tab geometry, CPU pixels where it covers the card border, hit testing to the badge, per-item retention across selection and removal by Reset.

## Scale number input

The fourth General row is an ordinary [NumberInput](text-number-input.md) with stable ID `scale`, initially `1` for every item. Each item keeps an accepted `scale` number and a separate `scale_draft` `TextEditState`; its owner differs from every name owner. Everything goes through the one `item.scale` command, carrying a host request:

- Committed text, Ctrl+A, Left/Right/Home/End with optional Shift and Backspace/Delete edit only the draft.
- Unmodified Enter parses the draft with `parse_number` over [0, 10] and accepts it. Escape restores the accepted text.
- Up/Down and primary presses on `scale-decrease`/`scale-increase` parse the draft, step by 0.1 with `step_number` and accept the result. A press on a step part focuses Scale; a click on the field only focuses it.
- Accepting replaces the draft with `format_number` text, places the caret at its end and advances its revision.

An invalid draft, such as `2,5` or `12`, rejects apply and step without publishing a view. The draft keeps its text, the accepted number is unchanged and the feedback reads `Scale could not finish. Enter a number from 0 to 10.`; the Windows host shows it in the title. While the draft differs from the accepted text, the muted grid label reads `Scale *`. Focus traversal and item selection keep the pending draft without applying it. Scale is application data only, like Visibility.

The window-free smoke checks the `number_input` role, name and value, Tab arrival after Amount, steps without binary noise, draft editing, apply/revert, rejected syntax and range for both apply and step, step-part presses, field focus without a command, clamping and per-item retention of the accepted value and pending draft. The injected native smoke types characters, applies with Return, steps with Up, rejects an invalid draft with caption feedback, reverts with Escape and presses a step part. On Windows the field takes committed `WM_CHAR` text and Return like the name editor, but the IME attaches only to the name editor.

## Scrub numeric fields

Scale and each Position component can be scrubbed with the pointer through one [`NumberDragDispatcher`](text-number-input.md#scrub-with-the-pointer), routed after the popup dispatchers and before the splitter. A primary press on an input's label (`Scale`, `X`, `Y`, `Z`), field or padding focuses it without a command, as a click did before; the Scale step parts keep stepping once per press.

Once the pointer has moved 3 units sideways, every 4 units from the press is one 0.1 step. The host parses the draft when the scrub starts and runs the field's own `item.scale` or `item.position.<axis>` command with a `NumberScrub` request whenever the step count reaches a new value, accepting `step_number(range, start, steps)` and replacing the draft with canonical text. Clamped steps that repeat the accepted value run nothing. The pressed input keeps the `control` active background while captured, and Windows shows the horizontal resize cursor over scrubbable inputs and during the scrub.

- Escape while scrubbing restores the start value through one more command; release, `PointerCancel` and Tab keep the last value. Tab and other keys then continue with their usual meaning.
- A valid pending draft, such as typed `2.5`, is the start value. An invalid draft, such as `2,5`, rejects the scrub with the usual `Scale could not finish...` feedback and stays as typed; Escape still reverts it.
- Each changed value is a separate accepted command, like the Display color drag; [history](#undo-and-redo) merges the whole scrub into one entry.

The window-free smoke checks the cursor targets, focus and active feedback without a command, the threshold, one command per changed step, clamping without repeated commands, return through the start value, Escape restoration without a later release command, release steps, a click, a pending draft as the start, `PointerCancel`, invalid-draft rejection, a Position component through its own command, Tab ending the scrub, unchanged step parts and per-item retention. The injected native smoke drags the X label with Windows mouse messages under OS capture and checks the cursor request.

## Position vector input

The fifth General row is an ordinary [VectorInput](vector-input.md) with stable ID `position` and components `position-x`, `position-y` and `position-z`, initially `0` for every item. Each item keeps an accepted `position` array and one `TextEditState` draft per component; draft owners differ from every name and Scale owner. The host treats Scale and the three components as one table of numeric fields, each with its own `item.scale` or `item.position.<axis>` command, range and reset value, and routes committed text, editing keys, Enter, Escape and Up/Down to whichever field holds focus. Position uses the range -1000 to 1000 with step 0.1 and has no step parts.

The stylesheet hides the generated `position-label` because the muted grid label already names the row, and gives each component the white rounded control decoration with hover, press and focus feedback and a muted axis label. The components share the 202-unit minimum control column equally. While any component draft differs from its accepted text, the grid label reads `Position *`. Rejection feedback names the field and its own bounds, such as `Position could not finish. Enter a number from -1000 to 1000.` Reset restores 0, 0, 0 and replaces each draft with a new revision; the Modified badge follows Position too.

The window-free smoke checks the `group` role and name, three `number_input` components named X, Y and Z inside it, Tab and Shift+Tab through the components after Scale, per-component steps and apply through one command each, pending captions, rejected syntax with Position feedback, Escape, pointer focus without a command, equal widths at 960 and 492 units, per-item retention, the badge and Reset. The injected native smoke types into Y, applies with Return and steps with Down.

## Notes text editor

The last General row is an ordinary [TextEditor](text-editor.md) with stable ID `notes`, after the scale hint and labelled by the muted `notes-caption`. Each item keeps a `TextLines::multiple` `TextEditState` whose owner differs from every name and numeric draft owner; Cube and Scene start with short documentation and the other items start empty. The editor uses the `control` decoration, wraps to the control column and grows with its rows, so the card, not the editor, scrolls. Notes are documentation like the display name: Reset keeps them and they never show the Modified badge.

Everything goes through the `item.notes` command, carrying a host request:

- Committed text, Ctrl+A, Left/Right, Backspace/Delete and their Control word forms map as for Display name, and Enter or Shift+Enter inserts LF. Tab moves focus.
- A primary press on the editor focuses it and places the caret at the nearest stop of the presented rows; Shift+press extends, and dragging selects across rows. Each changed selection is one `item.notes` request, and the Windows host shows an I-beam over both text editors.
- While focused, the editor paints its selection under the text and its caret above it; during composition the preedit is underlined instead.
- Home/End move to the start or end of the caret's visual row, and Control+Home/End to the buffer ends; Shift extends.
- Up/Down move to the nearest stop on the adjacent row, computed with `layout_text_carets` from the accepted `notes-value` layout, and keep the column where vertical movement began until another edit. Up on the first row and Down on the last stay on that row.
- Copy, cut and paste share the name editor's synchronous clipboard path. Paste normalizes CRLF and CR to LF before inserting; the Windows adapter writes LF as CRLF.
- The Windows IME attaches to Notes like Display name, and the candidate window follows the caret's row.

Rejected input, such as typing beyond 4096 bytes, keeps the accepted text and reads `Notes edit could not finish. Text must fit within 4096 UTF-8 bytes.`; Windows shows it in the title.

The window-free smoke checks the `text_editor` role, name, value and command, Tab arrival after Position, typing with Enter and Shift+Enter, one row per line, Up/Down with a retained column, Home/End, Control+Home/End, deletion across lines, word moves and erasure, press placement, drag and Shift+press selection with one command per change, painted selection and caret only while focused, the I-beam targets, wrapped rows in the 492-unit window, copy and normalized paste, composition with candidate placement on the second row and Tab cancellation, Enter leaving the single-line name unchanged, rejection feedback, per-item retention and Reset. The injected native smoke reaches Notes with Tab, checks that the IME attaches, and inserts a line with Return, characters and Up; it also checks that the window requests the I-beam over Display name and not over a scrubbable field.

## Visibility choice

The first Display row is an ordinary [RadioGroup](radio-group.md) with stable ID `visibility` and keys `inherited`, `visible` and `hidden`, initially `inherited` for every item. Each option requests its own `visibility.<key>` command; the host stores the accepted key on the selected item, so selection-follows-focus arrows and pointer presses share one command path. The muted grid label names the row visually; the stylesheet hides the generated `visibility-title`, which still names the semantic `radio_group`. Options use the same rounded hover/press/focus decoration as other rows, and the checked option's label is blue. The group is a `flex_wrap: wrap` row with a 4-unit gap: the three choices share one line in a wide Inspector and wrap Hidden onto a second line in the narrow control column of a 492-unit window, where the Shadows row moves below both lines.

The value is host-owned application data only: it does not hide Hierarchy rows or affect other properties. The window-free smoke selects Display, then checks semantic roles and checked state, Tab entry from the Display tab at the checked option, wrapping arrows with one command per move, Shift+Tab exit back to the tab, pointer selection, the checked-label emphasis and independent per-item retention. It also checks one line at the default width, and at 492 units the wrapped lines inside the control column, Shadows below them, a pointer press on the wrapped Hidden choice and unchanged declared arrow order. The Purpose smoke checks outside dismissal with a press on a Hierarchy row, which neither selects it nor clicks through. The injected native smoke reaches the group with Tab after selecting Display and selects with arrow messages. Because the card is now taller, the native smoke wheels the Inspector back to its top before pressing the selected-item button.

## Shadows switch

The second Display row is an ordinary [Switch](checkbox-switch.md) with stable ID `shadows`, labeled Cast shadows and initially on for every item. It requests the `item.shadows` command, which inverts the selected item's host-owned value; Enter/Space, gamepad Activate and a pointer press on any part share that command. The row uses the `control` class like Enabled; its generated track is blue with the thumb at the end while on, and gray with the thumb at the start while off.

Enabled remains a [Checkbox](checkbox-switch.md) and Display name a [TextInput](text-number-input.md), which projects a `text_input` named Name with the edited text as its value. The toggles project `checkbox`/`switch` roles named by their labels, with checked state in `selected`, instead of buttons whose names include `[x]`. Like Visibility, the shadows value is application data only: nothing in this example renders shadows from it.

The window-free smoke selects Display, then checks the switch role, name and state, Tab arrival after Visibility, Activate through one command, thumb/track presentation relative to the unchanged track size, that arrows do not toggle, track pointer activation and independent per-item retention. The injected native smoke reaches the switch with Tab and toggles it with Space.

## Purpose ComboBox

The third Display row is an ordinary [ComboBox](combobox.md) with stable ID `purpose`, choosing the item's USD-style imageable purpose: `default`, `render`, `proxy` or `guide`, initially `default` for every item. The trigger uses the `control` class with a muted `v` marker; the `purpose-list` popup reuses the menus' `popup` class, is at least as wide as the trigger and is composed under the root only while the Display tab is shown. Options use the rounded row decoration and the selected option's label is blue.

`purpose.toggle` opens or closes the list. The host keeps one open popup at a time, so the list shares `State::menu` with the item menus and the Display color panel. Each option requests `purpose.<key>`, which stores the key on the selected item and closes the list; the dispatcher's refresh then returns focus to the trigger. Alt+Down on the focused trigger runs the same toggle command. Escape, Alt+Up and outside presses close without choosing or clicking through; Tab closes and continues from the trigger. Purpose is application data only: nothing in this example filters rendering by it.

The window-free smoke selects Display, then checks the closed `combobox` role, name, value and collapsed state, Tab arrival after Cast shadows, that plain Down neither opens nor changes the value, Alt+Down opening through one command at the selected option, the list's placement and width, arrow movement without commands, Enter choosing and returning focus, the Modified badge, reopening at the new choice, Escape and Tab dismissal, pointer open/hover/choice, an outside press without click-through, the selected-label emphasis, per-item retention and Reset. The injected native smoke reaches Purpose with Tab, opens it with Space and chooses Render with Down and Return.

## Display color picker

The fourth Display row is an ordinary [ColorPicker](color-picker.md) with stable ID `display-color`, labelled Display color. Each item keeps an accepted `HsvColor`, initially USD's fallback display color, 50% gray (`#808080`, hue 0). Keeping HSV rather than an sRGB color retains the hue while the color is gray or black. The trigger uses the `control` class; the `display-color-panel` popup reuses the `popup` class, whose padding holds the thumbs, and is composed under the root only while the Display tab is shown. The area and hue strip take the solid focus outline.

`color.toggle` opens or closes the panel, which shares `State::menu` with the item menus and the Purpose list. The host routes input through `ColorPickerDispatcher` after the Purpose dispatcher, passing the selected item's accepted color. A requested color runs `color.area` or `color.hue`, which stores it on the selected item and keeps the panel open; plain activation of either control, such as a semantic action, carries no color and does nothing. Each accepted adjustment is a separate command; closing does not revert. Display color is application data only: nothing in this example renders items with it.

The window-free smoke selects Display, then checks the closed `color_picker` role, name, `#808080` value and collapsed state, Tab arrival after Purpose, Alt+Down opening at the area, the panel's placement, area arrows adjusting saturation and value through one command each without closing, the swatch fill, hex value and Modified badge, Tab to the strip and its hue step, Tab closing and continuing past the trigger, pointer opening, a press and drag on the area with one command per changed color, an unchanged release, thumb placement, a hue press, an outside press without click-through, Escape and Enter closing while keeping the color, per-item retention and Reset. The injected native smoke reaches Color with Tab, opens it with Space, adjusts saturation and value with Right and Up, moves to the strip with Tab, steps hue with Right and closes with Return.

## Copy a Display color by dragging

The Display color swatch is a [typed drag source](../architecture/input.md#implemented-typed-drag-and-drop) and every Hierarchy row is a drop target. A `DragDropDispatcher`, routed after the Display color dispatcher and before scrubbing, receives a scope with one source and one target per item, all with payload type `application/x-hsv-color` and the copy operation. The swatch is a source only while the Display tab is shown and its panel is closed. The payload value is the shown item's accepted `HsvColor`, which the host keeps. A drop runs one node-free typed command, `item.color`, resolved with `CommandSnapshot::resolve`:

| Argument | Type | Meaning |
| --- | --- | --- |
| `item` | object ID | The item whose Display color is replaced |
| `from` | object ID | The item whose Display color is copied, the shown item for a drop |

Stale or unknown IDs fail resolution, and copying an item onto itself rejects with `item_color`. The command changes only Display color, so the selection, shown item, focus and other values stay as they were.

- A press on the swatch still focuses it, and a release without 4 units of travel still opens the panel.
- While dragging, a 20-unit swatch of the dragged color follows 14 units below and right of the pointer without taking pointer input, and an accepting row shows a two-unit dashed blue ring. The shown item's own row and rows whose color already matches show no ring. Shift requests a move and Control+Shift a link, which this drop does not offer, so they show no ring either; Control requests the copy it does offer.
- Releasing over a ringed row runs `item.color` once, which records one [history](#undo-and-redo) entry. Releasing elsewhere, Escape, `PointerCancel` and Tab cancel without a command, and the release does not open the panel.

The window-free smoke checks the typed descriptor and published IDs, the threshold, the preview's color, position and pointer transparency, the ring without a command, unchanged and outside positions, Shift and Control, one command per drop with selection kept and the panel closed, Undo and Redo, Escape, `PointerCancel`, an unchanged drop, Scene as a target, a release outside, click opening the panel, no source on the General tab and direct rejection of copying onto itself. The [`color-drag` snapshot](#snapshot-bundles) holds a tinted color over the Light row. Keyboard and semantic operation, drag scrolling, a Windows drop cursor and native injected coverage are not included.

## Analytic backgrounds

The Enabled label includes an eight-unit circular status mark, green when the selected item is enabled and gray otherwise. It is an ordinary borderless Column, not an image or special widget:

```cpp
.background_shape = FillShape::ellipse,
.background_antialias = true
```

Equal width/height makes a circle; unequal dimensions make an ellipse. The property card also opts into smooth fill-boundary coverage for its radial gradient. Both use the [analytic fill contract](../architecture/rendering.md#implemented-analytic-fills), with fixed 4x4 physical-pixel samples on CPU and Vulkan. Background shape does not change borders, shadows, outlines, descendant clips, rectangular input or pane allocation; the status mark has no action and Enabled retains its existing Checkbox command.

The window-free smoke compares actual smooth/hard decoration pixels at scales 1, 1.25, 1.5 and 2, checks that the indicator follows an accepted enabled edit and keeps its geometry, and retains the existing edit/selection/scroll/resize checks. The injected native smoke presents the complete CPU-rendered form. GPU owner checks validate the built-in shapes, not this application's glyphs.

## Focus outlines

Focused controls use a solid 1.5-unit blue outside outline with a one-unit gap. Focused Hierarchy and menu rows use the same rounded geometry with four-unit dashes and two-unit gaps:

```cpp
.outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}, 4.f, 2.f}
```

This ordinary optional style emits the [smooth stroke command](../architecture/rendering.md#implemented-outlines-and-dashed-strokes); CPU and Vulkan reference/batch share the built-in behavior. Dash phase continues around the corners, rather than restarting per edge. The existing inside focus border and selected fill remain, so selection and focus are still distinct. Blur removes the old outline through ordinary style resolution.

Outlines occupy existing outside space without changing control geometry, input bounds, editing, selection, scrolling or pane minima. Ancestor clips trim them; focus reveal still follows the layout box rather than expanding scroll allocation for decoration. The window-free smoke checks resolved solid/dashed styles, actual partially covered edge and opaque interior pixels at scales 1, 1.25, 1.5 and 2, stable geometry/data and removal on focus movement. The injected native smoke presents the full CPU-rendered form; GPU owner comparisons cover the primitive, not this application's glyphs.

## Selection elevation

Each selected Hierarchy row is raised with ordinary styles:

```cpp
{.background = Color{.2f, .34f, .67f, 1}, .color = Color{1, 1, 1, 1}, .z_index = 1,
 .box_shadow = BoxShadow{{0, 2}, 6, {.12f, .22f, .46f, .35f}}}
```

The shadow reaches four units past the 4-unit row gap into the next row. Without stacking, the next row paints later, so its hover fill would hide that part of the shadow. With [`z_index`](../architecture/layout.md#implemented-stacking-order) 1, the selected row paints after its siblings and the shadow stays visible over the hover fill. The focus rule shared by Hierarchy and menu rows adds `z_index` 2, so a focused row below the selection draws its dashed ring over the shadow in the gap. Rows keep their layout, Tab/arrow order and semantics. Pointer input still reaches the hovered row under the shadow, because only border boxes hit.

The window-free smoke checks the resolved values and the 4-unit gap. It compares actual CPU pixels with the same view painted with every `z_index` cleared. While Sphere is hovered under Cube's shadow, the overlap band differs and Sphere's interior does not; the hit still reaches Sphere and the layout is unchanged. With Sphere focused, its ring differs in the gap. Selecting Sphere moves the elevation to it.

## Gradient property card

The selected-item button and property Grid share a padded, rounded card with an off-center pale-blue/cyan-to-lilac radial highlight and an inside border. The form uses the ordinary optional `StyleDeclarations::background_gradient` with a nonuniform interior stop:

```cpp
ui::Column(children, {
    .id = "item-card",
    .style = {
        .corner_radius = 12.f,
        .background_gradient = RadialGradient{
            .center = {.25f, .2f}, .radius = {.85f, .95f},
            .stops = {{0, {.96f, .99f, 1, 1}},
                      {.42f, {.84f, .92f, 1, 1}},
                      {1, {.98f, .9f, .98f, 1}}}},
        .background_antialias = true
    }
});
```

Center and positive ellipse radii are normalized to the card's border box. The stops interpolate in premultiplied linear light, keep rounded corners and precede the existing inside border; CPU and Vulkan implement the same [radial fill contract](../architecture/rendering.md#implemented-radial-gradients). An empty `stops` vector selects `start_color` at the center and `end_color` at/outside the ellipse. Explicit positions must be nondecreasing in [0, 1]; duplicate positions make hard color transitions. The same property also accepts the existing `LinearGradient`; either replaces the other, while an explicit empty `optional<Gradient>` clears the fill.

The native app uses the CPU implementation. This adds no special widget, authored image resource or interaction path. The window-free smoke checks that the authored radial card changes actual pixels compared with both its two-color endpoints and a linear substitute, with rounded coverage at scales 1, 1.25, 1.5 and 2; ordinary application Replay preserves the resolved geometry and stops while edits, scrolling and resizing retain their existing behavior.

## Card shadow

The same card now casts a subtle blue-gray shadow below its radial highlight:

```cpp
.box_shadow = BoxShadow{{0, 4}, 8, {.18f, .22f, .38f, .18f}}
```

The offset and softness are logical units. This single [distance-based outer shadow](../architecture/rendering.md#implemented-box-shadows) has finite smooth falloff, not a Gaussian blur. It follows the card's 12-unit corner radius, paints before the fill/border and uses the ordinary pane/window clip. The existing layout and hit-test/scroll bounds stay unchanged, so the pane edges trim the halo and its lower portion occupies the existing gap beneath the card.

An explicit empty `optional<BoxShadow>` clears the style or theme effect. CPU and Vulkan reference/batch render the same contract without an image or offscreen pass. The window-free smoke compares actual shadow/no-shadow pixels at scales 1, 1.25, 1.5 and 2 and checks that the opaque radial interior stays unchanged; ordinary Replay reproduces the shadow together with retained edits, scrolling and resizing.

## Name input inset shadow

Display name uses the same ordinary shadow property with `inset` enabled:

```cpp
.box_shadow = BoxShadow{{0, 1}, 4, {.18f, .22f, .38f, .2f}, true}
```

The one-unit downward cutout offset emphasizes the top edge; four-unit finite falloff gives the white input a recessed appearance. The [distance-based inset contract](../architecture/rendering.md#implemented-box-shadows) clips the shadow to the input's six-unit rounded border box and paints it above the fill, below the focus border and text. It is not Gaussian blur or CSS padding-box shadow placement. It does not change padding, layout, hit testing, caret/selection, editing or pane minima, and hover/focus retain the effect.

The window-free smoke checks actual interior darkening at scales 1, 1.25, 1.5 and 2 with unchanged outside and center pixels, plus paint order, authored parameters and stable focus/hover geometry. Existing edit/select/return, scroll/resize, rejection, clipboard and Replay checks remain in the same consumer; the injected Windows smoke presents the full CPU-rendered form. GPU owner fixtures validate the built-in effect, not this application's white-filled pixels or glyphs.

## Japanese IME composition

Display name and Notes accept Windows IME composition. While either is focused and eligible, the host attaches the IME and hides the default composition window; elsewhere the IME is detached, so keys keep their navigation meaning. Each platform session becomes ordinary revision-addressed [composition steps](../architecture/input.md#implemented-ime-composition-state) through the focused editor's `item.name` or `item.notes` command:

1. Start captures the current selection; typing with a selection replaces it on commit.
2. Updates change only the presented preedit and its caret. The Hierarchy and committed name keep the accepted bytes, and the field's third line reads `Composition <begin>:<end> caret <caret>`.
3. The IME result commits once; Escape, or ending without a result, cancels and keeps the selection.

Keys and characters during a session stay with the IME, so Tab, Enter and arrows do not navigate. Pressing elsewhere first asks the IME to complete, then moves focus; Tab, selection changes and window deactivation cancel instead. A late step for a closed or replaced composition, or for another item, is rejected without editing. A commit beyond the 4096-byte limit closes the composition, keeps the committed text and shows caption feedback.

The application text service gives common wide East Asian scalars two 0.5 em placeholder cells, and the native GDI adapter draws them with MS Gothic (Segoe UI Emoji for emoji) so Japanese names and preedit stay legible. This is an [application-local cell policy](../../examples/inspector-hierarchy/cell_text.hpp), not Unicode width conformance or real shaping. The candidate window follows a logical caret rectangle from [`layout_text_carets`](text-editor.md#caret-geometry) over that layout, on the caret's row for Notes.

The window-free smoke covers inline preedit, interior carets across two-cell glyphs, commit/selection replacement/cancel, late steps, Tab and pointer cancellation without redirecting to the new item, sessions outside the editor, an over-limit commit and presentation rejection/retry. The injected native smoke substitutes composition strings for an IME and checks UTF-16 conversion, a cursor inside a surrogate pair, malformed results, key suppression during a session, candidate placement, end/focus-loss cancellation, press completion and IME attachment following focus.

## Snapshot bundles

`--snapshot <scenario> <directory>` runs one named scenario without a window and writes a [snapshot bundle](../architecture/inspection.md#implemented-snapshot-bundle-tooling) of the final screen. Use it to see a change without the full smoke or a desktop session:

```powershell
cmake --build build --config Release --target tessera_inspector_hierarchy
& .\build\Release\tessera_inspector_hierarchy.exe --snapshot edit-name build\snapshots\edit-name
```

An unknown name prints the scenario list. The scenarios are:

| Scenario | Final screen |
| --- | --- |
| `initial` | Launch state |
| `edit-name` | Sphere selected from the Hierarchy and renamed `Edited sphere`, with the name editor focused |
| `ime` | Japanese preedit `にほんご` open at the end of the name editor |
| `multi-select` | Cube through Light selected with Shift+click, then Extra 1 added with Control+click and shown as `Selected: extra1 +3` |
| `display` | Display tab with the Visibility choices |
| `color-drag` | Cube's tinted Display color dragged from its swatch and held over the Light row, with the ring and preview |
| `narrow` | The 492×360 minimum window: header tools without the heading and the narrow Inspector pane |

Start with `g<n>/tree.txt` for roles, IDs, names, geometry and state, then open `g<n>/frame.png` for the pixels; `manifest.json` says which generation `<n>` is.

To see what a change does to a scenario, write its bundle before and after the change and [compare them](../architecture/inspection.md#implemented-snapshot-bundle-tooling):

```powershell
cmake --build build --config Release --target tessera_inspector_hierarchy tessera_snapshot_cli
& .\build\Release\tessera_inspector_hierarchy.exe --snapshot edit-name build\snapshots\before\edit-name
# Make the change and rebuild, then:
& .\build\Release\tessera_inspector_hierarchy.exe --snapshot edit-name build\snapshots\after\edit-name
& .\build\Release\tessera_snapshot.exe diff build\snapshots\before\edit-name build\snapshots\after\edit-name --out build\snapshots\edit-name.diff.json
```

The exit status is 0 when nothing changed and 1 when something did. The one-line summary names the first level that changed, and the report lists the changes. Semantic changes come first, such as names, values, selection, focus and actions. Layout changes come next, as border boxes, clips and scroll geometry by author ID, followed by requested actions, paint counts and the pixel count with its bounds. Different scenarios, such as `initial` and `narrow`, usually differ in viewport, so `diff` reports `incompatible` (exit status 3) instead of judging them. `tessera_snapshot verify <directory>` checks a single bundle. Each scenario drives the ordinary `App`, then records its accepted host view and host focus through a `ReplaySession`. The bundle's offscreen run therefore plays the same document and styles with the application's cell text service, declared as `inspector-cell-text`. On Windows, frames use the native host's GDI fixed-cell glyphs (`win32-gdi-fixed-cell`), so text is readable but depends on the installed Consolas and MS Gothic fonts. Other platforms use synthetic marks. Scroll offsets and pointer state reach the bundle only through the recorded document and styles. A Release scenario takes under a second; each is registered as an `inspector_hierarchy_snapshot.<scenario>` smoke.

## Capability manifest

`--capabilities <scenario> <file>` runs the same scenario and writes a [capability manifest](../architecture/inspection.md#implemented-capability-manifest) of its final generation as `tessera-capabilities` JSON. Use it to find which commands the Inspector has, whether each is enabled and what arguments it takes, and which controls an agent could focus or activate, without reading `main.cpp`:

```powershell
& .\build\Release\tessera_inspector_hierarchy.exe --capabilities edit-name build\snapshots\edit-name.capabilities.json
```

- `operations` lists every operation. `inspect_tree` and `capture_frame` are supported through `--snapshot`, and `compare_snapshots` through `tessera_snapshot diff`. The others, including `invoke_command` and `semantic_action`, are `not_declared`: this window-free adapter only runs the named scenarios.
- `commands` lists every production command, such as `edit.undo` with `"enabled": false` and `"disabled_reason": "Nothing to undo."` at launch, and the typed `item.move` with its `item` and `target` object IDs and `before`/`after` placement. `objects` lists the item IDs those arguments accept.
- `targets` lists each control that can be focused or activated, with its preorder node, ID, role, name and the command its action requests, for example `hierarchy-item-sphere` selecting with `select.sphere`. Disabled controls, such as Redo with no history, are not targets.

The manifest's `generation` is the Inspector's command generation, not a bundle generation index. Each `inspector_hierarchy_capabilities.<scenario>` smoke writes one.

## Semantic diff

`--semantic-diff <scenario> <file>` captures the launch state and the scenario's final generation and writes what the scenario changed in meaning as a [semantic diff](../architecture/inspection.md#implemented-semantic-diff) (`tessera-semantic-diff` JSON). It also prints one line per change. Use it to check what an interaction did without comparing screenshots or whole trees:

```powershell
& .\build\Release\tessera_inspector_hierarchy.exe --semantic-diff edit-name build\snapshots\edit-name.semantic-diff.json
```

For `edit-name` it reports ten changes from generation 2 to 7. Focus moves to `name`. `tool-undo` becomes enabled and focusable and gains its `edit.undo` action. `hierarchy-item-cube` is deselected and `hierarchy-item-sphere` is selected and renamed `Edited sphere`. The `selected` caption, the `name` value and the `notes` value change. Position, Enabled and the other fields report nothing, so an unintended side effect would show up as an extra change. `initial` reports no changes. `display` reports the tab selection, then the Display section's fields as added and the General section's as removed. `narrow` reports the splitter's new name and the heading that the narrow header leaves out. Generations are the Inspector's command generations. Each `inspector_hierarchy_semantic_diff.<scenario>` smoke writes one.

To compare two bundles semantically, pass their `semantics.json` members to `tessera_snapshot semantic-diff`. Each bundle names its generation directory in `manifest.json`, for example `g1` for `edit-name` and `g2` for `narrow`. Add `--scope <id>` to compare one subtree, such as `hierarchy`, and `--kind value_changed,enabled_changed` to keep only those kinds:

```powershell
& .\build\Release\tessera_snapshot.exe semantic-diff build\snapshots\initial\g1\semantics.json build\snapshots\edit-name\g1\semantics.json --scope hierarchy
```

This reports the Cube and Sphere selection changes and the Sphere rename. It exits 0 when nothing changed and 1 when something did. Unlike `diff`, it does not need equal viewports, so `initial` and `narrow` can be compared. Bundle semantics carry Replay generation indices, so both sides of this example are generation 1.

## Host and observation boundary

The host owns all item data and order, selection, disclosure, scroll offsets, input dispatchers and command sequences. Item actions resolve against the accepted semantic tree and command snapshot. A candidate copies host state, applies the public text transform where needed, and stages lowering, eligibility, styles, layout, paint and inspection. Only a successful command outcome accepts that candidate and publishes the session. A stale/foreign text owner or rejected text measurement leaves the accepted values and inspection unchanged; retry uses the retained edit revision.

The window-free smoke covers select/edit/select-another/return, Visibility choice, Purpose choice, Display color, independent item edits, foreign-owner rejection, presentation rejection/retry, collapse, actual wheel offsets, keyboard/pointer pane resizing, Menu/ContextMenu, and shared selection/property observations. It records accepted host-built view replacements and a selection action through ordinary [Replay](../architecture/replay.md), round-trips Replay JSON, compares playback and checks the final selected name, enabled flag and amount. This reproduces the observed views and action, not application command execution, raw text keys or native messages.

The application's synchronous `clipboard_read` and `clipboard_write` callbacks supply owned UTF-8 read results and located failure diagnostics outside core traversal. Copy preserves owner/revision/selection and publishes no UI update. Cut and paste capture the selected item and edit observation before calling the host, recheck focus, eligibility and identity afterward, then stage the focused editor's `item.name` or `item.notes` command. Cut deletes only after a write returns no diagnostics. Reentrant host changes cannot redirect the result to another item, selection or focused control. A failed clipboard service, invalid replacement or failed presentation retains the accepted state; presentation errors are bounded to the command result limit while retaining their original diagnostic codes. Clipboard writes and UI publication are separate: if cut copies successfully but its target changes or presentation fails, the clipboard keeps the copied bytes while no deletion is accepted. Retrying uses the retained selection and revision. No asynchronous clipboard protocol or clipboard-result replay encoding is introduced.

Name and notes rejection returns to the host instead of throwing out of the message loop. Feedback is application-owned and independent of the UI generation: the native caption can report a failed measurement without trying to rebuild the rejected presentation. It is not part of semantic inspection or Replay, and it does not turn unrelated host failures into recoverable results. Clipboard no-ops with an empty copy/cut selection retain any previous notice. The native host prepares the candidate's glyph atlas before command publication; a frame that exceeds its 4096-glyph occurrence budget rejects while retaining the accepted view. This frame-wide budget counts both the Hierarchy and Inspector names plus other labels, so the text editor's 4096-byte limit does not guarantee every native presentation fits.

Windows hosting reuses the Settings starter's [optional host](../../examples/settings-starter/native_win32.hpp): pointer capture/cancellation, logical coordinate mapping, keyboard/text messages, GDI glyph coverage and CPU presentation. The application-local [clipboard adapter](../../examples/inspector-hierarchy/clipboard_win32.hpp) uses `CF_UNICODETEXT`, strict UTF-16/UTF-8 conversion, bounded reads and explicit Windows memory ownership. Open/format/lock/write failures return diagnostics without throwing through the message loop.

The window-free checks cover rejected committed name text and text/deletion/selection presentation staging, unchanged items/focus/inspection, reversed Unicode selection and a successful retry. Clipboard checks use injected services for reverse Unicode selections, partial replacement/deletion, modifier/repeat/focus guards, retained independent items, service/validation/presentation failures, retry and reentrant target changes. The injected native smoke checks arrow/Activate selection, Hierarchy drag reordering, text/Checkbox/Slider/Scale/Position scrub/Visibility/Switch/Purpose/Display color edits, Display tab selection, toolbar arrows and Reset, pointer select/return after wheeling the Hierarchy back into view (each smoke click requires the pointer to reach its control), right-click Rename, splitter capture and window resize, plus Ctrl+C/X/V routing, suppression of translated control characters/repeats, Unicode cut/paste, rejection recovery and strict Unicode conversion. It also verifies caption feedback and recovery after rejected typing, a full Unicode name buffer, invalid paste and an over-budget ASCII glyph atlas. It substitutes an in-memory clipboard, leaving the user's OS clipboard untouched. Live OS clipboard exchange, physical devices and IME sessions, real text shaping, GPU rendering, native editable accessibility and persistence remain unvalidated. Capability claims and validation limits live in [support](../reference/support-matrix.md).
