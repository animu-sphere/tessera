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

- Select Cube or another row in the [TreeView](tree-list.md). Arrows move focus, Enter/Space selects, and Left/Right collapses/expands Scene. Disclosure never changes controlled selection. The selected row casts a small shadow onto the next row, even while that row is hovered; see [Selection elevation](#selection-elevation).
- Tab to the General/Display [tabs](tabs-toolbar.md) above the properties; Left/Right switch sections. General holds Display name, Enabled, Amount, Scale and Position; Display holds Visibility, Cast shadows and Purpose. See [Property sections](#property-sections).
- Tab into the General [Grid Inspector](grid.md). Edit Display name with committed text, exact Ctrl+A, arrows, Shift selection, Home/End and Backspace/Delete. Text editing consumes supported keys before tree navigation.
- With Display name focused, exact non-repeating Ctrl+C copies the selected committed bytes, Ctrl+X copies and then deletes the selection, and Ctrl+V replaces the selection through the ordinary name-edit command. Windows interactive mode exchanges Unicode text with the OS clipboard. Empty selection copy/cut and empty text paste do nothing. Paste rejects CR/LF, embedded NUL, malformed Unicode and results above 4096 UTF-8 bytes, preserving the accepted name and selection; diagnostics print to stderr and the window remains usable.
- With Display name focused, type Japanese through the Windows IME. Preedit appears inline in the editor, the candidate window opens at the preedit caret, Enter commits and Escape cancels; see [Japanese IME composition](#japanese-ime-composition).
- Rejected name input, including typing beyond the 4096-byte limit, keeps the accepted text, selection and focus. Windows shows name-edit and clipboard failures in the window title, with a short reason for text validation failures. A successful name edit or clipboard operation clears the notice; navigation alone retains it. Stderr records the operation, original target item and located diagnostics. Presentation rejection also preserves the accepted inspection and view, so retry can use the retained revision.
- Activate Enabled or adjust Amount with Left/Down, Right/Up, Home/End or a pointer drag. Amount has integer stops from 0 to 100 in steps of 5.
- Tab to Scale, a [NumberInput](text-number-input.md) from 0 to 10. Type a decimal and press Enter to apply it, or Escape to restore the accepted value; Up/Down or the `-`/`+` parts step by 0.1. See [Scale number input](#scale-number-input).
- Tab on to Position, a [VectorInput](vector-input.md) whose X, Y and Z are separate Tab stops from -1000 to 1000. Each edits, applies, reverts and steps by 0.1 like Scale. See [Position vector input](#position-vector-input).
- On the Display tab, Tab to Visibility, which enters at the checked choice. Arrows move between Inherited, Visible and Hidden, wrapping, and select the focused choice; a pointer press selects directly. See [Visibility choice](#visibility-choice).
- Tab to Cast shadows and press Enter/Space, or click its track, thumb or label, to toggle the [Switch](checkbox-switch.md). See [Shadows switch](#shadows-switch).
- Tab to Purpose, a [ComboBox](combobox.md). Enter/Space, Alt+Down or a click opens its list at the current choice; Up/Down/Home/End move, Enter or a click chooses, and Escape, Alt+Up, Tab or an outside press closes without choosing. See [Purpose ComboBox](#purpose-combobox).
- Change any property and a blue Modified badge appears on the property card's top edge; Reset, or a property returning to its reset value, removes it. See [Scale hint and Modified badge](#scale-hint-and-modified-badge).
- Select another item and return. Every item retains its own text owner, revision, UTF-8 byte selection, enabled flag, amount, accepted scale and pending scale draft, visibility, shadows value and purpose. The edited display name also appears in the Hierarchy.
- Wheel either pane or drag the [SplitPane](split-pane.md) boundary. Focus navigation reveals controls; scrolling and pane/window resizing do not change item values. Pane minima are 140/320 logical units plus the 8-unit handle; smaller windows clip overflowing minima.
- Use the header [toolbar](tabs-toolbar.md): Tab enters at Item actions, and Left/Right/Home/End move to Rename and Reset without running them. Reset restores the selected item's properties except its name. See [Item tools](#item-tools).
- Open Item actions, or right-click the selected-item button at the top of the Inspector. Shift+F10 opens the same context actions from that button. Rename, from the menu or the toolbar, shows General and focuses the retained name editor; Toggle enabled shares the Checkbox command. Escape/Cancel or an outside press dismisses the menu. These operations need no destructive confirmation, so no Dialog is added.

The context target is the selected-item button, not arbitrary unselected hierarchy rows. No command silently selects a different item on right-click.

## Application appearance

The ordinary form now uses a larger heading, a white rounded Hierarchy panel, muted property labels and compact multiline help. Hierarchy rows have a 30-unit minimum height; the controlled selection has a blue fill and white text, while keyboard focus has a separate inside border and smooth outside outline. Buttons, name editing, the enabled value, rounded Slider parts and padded item menus share the blue-gray palette. The property card retains its radial highlight and outer shadow; the name input has a subtle inset shadow.

The application-local stylesheet targets public author IDs and classes, including the documented generated TreeView, Slider and Menu IDs. Hover, active and focus rules change paint only: their borders already reserve space in the base styles. The host resolves feedback against the accepted tree and layout, then prepares paint and inspection together before replacing either. Stationary hover survives ordinary view replacements through the existing compatible pointer policy; pointer release/cancellation clears active feedback, including the custom Slider drag. Hover never edits item data or publishes a command generation.

The Inspector uses a 72-unit label column and a control column with a 202-unit minimum. The 200-unit Slider track has no extra horizontal padding, so its full travel fits the existing 320-unit pane minimum, including card borders and pane/card padding. Both pane minima fit a 492-unit window including the application padding; smaller windows still clip the overflowing minima. Focus reveal and wheel scrolling retain their ordinary behavior.

The window-free smoke checks distinct hover/press/selection/focus pixels at scales 1, 1.25, 1.5 and 2, inherited selected-label color, stable row geometry, cancellation and once-only selection, stationary Slider feedback, minimum-width track containment and retained item data. Pixel comparisons omit glyph commands to isolate the changed decorations; the injected Windows smoke still presents the complete form with the existing GDI glyph adapter. Base appearance remains application styling, not a new widget skin API or text shaper; the focus decoration uses the built-in stroke below.

## Property sections

The property card below the selected-item button is an ordinary [Tabs](tabs-toolbar.md) element with stable ID `sections`, named Property sections. The General tab shows Display name, Enabled, Amount and Scale in the `properties` Grid; the Display tab shows Visibility, Cast shadows and Purpose in a three-row Grid with the same ID and columns. The host stores the shown section once for the application, not per item, so it survives item selection. Each tab requests its own `section.<key>` command; selection-follows-focus Left/Right and pointer presses share that path, and the controls of the hidden section are not composed. Tabs show the generated blue bottom indicator; the stylesheet adds muted text, a blue selected label, hover/press backgrounds and a solid focus outline without replacing the indicator.

Rename, from the item menus or the toolbar, first shows General and then focuses the name editor. Host checks that compare focus with the name or Scale editor treat an absent control as unfocused, so the Display section disables text, clipboard and IME routing rather than matching a missing node. The window-free smoke checks the `tab_list`/`tab` roles, names and selected state, Tab entry at the selected tab and on into the panel, Shift+Tab return, one command per Right/Left with wrapping and panel replacement, the indicator, Down into the panel without selecting, pointer selection, retention across item selection and Rename returning to General. The injected native smoke selects Display with a Right arrow message before reaching Visibility, and checks that context Rename shows General again.

## Item tools

The header's Item actions button now belongs to an ordinary [Toolbar](tabs-toolbar.md) with stable ID `item-tools`, named Item tools, followed by Rename (`tool-rename`) and Reset (`tool-reset`) buttons in the `control` class. The toolbar is one Tab stop at Item actions, so the existing sequential order is unchanged; Left/Right/Home/End move among the three tools without running a command, and Enter/Space or a press activates the focused tool. Item actions still opens its Menu. Rename shares the menu's `item.rename` command. Reset requests the new `item.reset` command, which restores the selected item's Enabled, Amount (50), Scale (1, replacing any pending draft with a new revision), Visibility (Inherited), Cast shadows and Purpose (Default) values while keeping its name; other items are unchanged.

The window-free smoke checks the toolbar role and name, single-stop entry/exit, wrapping movement without commands, Home/End, Reset through one command after changing every property, pointer Reset limited to the selected item, toolbar Rename and the retained menu trigger. The injected native smoke reaches the toolbar with Tab, moves to Reset with Right arrow messages without changing the item and resets with Space.

## Scale hint and Modified badge

Below Scale and Position, the General section's `properties` Grid has a sixth row holding the `scale-hint` text, "Enter applies, Esc reverts, Up/Down step.", with `grid_column_span` 2. It starts in the label column and ends with the control column, so it uses the full card width instead of wrapping inside the 72-unit label column, and its row keeps the shared 12-unit row gap. The bottom help no longer repeats it.

When the selected item differs from the values Reset restores (Enabled on, Amount 50, Scale 1 and Position 0, 0, 0 with no pending drafts, Visibility Inherited, Cast shadows on, Purpose Default), the card's last child is a `modified-badge` Text with `position: absolute`, `top: -9` and `right: 12`. It straddles the card's top border against the card's padding box, painted after and therefore above the card's own content, without changing the card's size or moving Selected, the tabs or the Grid. It is static text: it requests no command and carries no focus stop. The badge follows the selected item's own values and survives section changes.

The window-free smoke checks the spanning hint's bounds against the label and control columns at the 960 and 492-unit windows, the badge's absence at reset values, its exact padding-box offsets, unchanged card/Selected/tab geometry, CPU pixels where it covers the card border, hit testing to the badge, per-item retention across selection and removal by Reset.

## Scale number input

The fourth General row is an ordinary [NumberInput](text-number-input.md) with stable ID `scale`, initially `1` for every item. Each item keeps an accepted `scale` number and a separate `scale_draft` `TextEditState`; its owner differs from every name owner. Everything goes through the one `item.scale` command, carrying a host request:

- Committed text, Ctrl+A, Left/Right/Home/End with optional Shift and Backspace/Delete edit only the draft.
- Unmodified Enter parses the draft with `parse_number` over [0, 10] and accepts it. Escape restores the accepted text.
- Up/Down and primary presses on `scale-decrease`/`scale-increase` parse the draft, step by 0.1 with `step_number` and accept the result. A press on a step part focuses Scale; a click on the field only focuses it.
- Accepting replaces the draft with `format_number` text, places the caret at its end and advances its revision.

An invalid draft, such as `2,5` or `12`, rejects apply and step without publishing a view. The draft keeps its text, the accepted number is unchanged and the feedback reads `Scale could not finish. Enter a number from 0 to 10.`; the Windows host shows it in the title. While the draft differs from the accepted text, the muted grid label reads `Scale *`. Focus traversal and item selection keep the pending draft without applying it. Scale is application data only, like Visibility.

The window-free smoke checks the `number_input` role, name and value, Tab arrival after Amount, steps without binary noise, draft editing, apply/revert, rejected syntax and range for both apply and step, step-part presses, field focus without a command, clamping and per-item retention of the accepted value and pending draft. The injected native smoke types characters, applies with Return, steps with Up, rejects an invalid draft with caption feedback, reverts with Escape and presses a step part. On Windows the field takes committed `WM_CHAR` text and Return like the name editor, but the IME attaches only to the name editor.

## Position vector input

The fifth General row is an ordinary [VectorInput](vector-input.md) with stable ID `position` and components `position-x`, `position-y` and `position-z`, initially `0` for every item. Each item keeps an accepted `position` array and one `TextEditState` draft per component; draft owners differ from every name and Scale owner. The host treats Scale and the three components as one table of numeric fields, each with its own `item.scale` or `item.position.<axis>` command, range and reset value, and routes committed text, editing keys, Enter, Escape and Up/Down to whichever field holds focus. Position uses the range -1000 to 1000 with step 0.1 and has no step parts.

The stylesheet hides the generated `position-label` because the muted grid label already names the row, and gives each component the white rounded control decoration with hover, press and focus feedback and a muted axis label. The components share the 202-unit minimum control column equally. While any component draft differs from its accepted text, the grid label reads `Position *`. Rejection feedback names the field and its own bounds, such as `Position could not finish. Enter a number from -1000 to 1000.` Reset restores 0, 0, 0 and replaces each draft with a new revision; the Modified badge follows Position too.

The window-free smoke checks the `group` role and name, three `number_input` components named X, Y and Z inside it, Tab and Shift+Tab through the components after Scale, per-component steps and apply through one command each, pending captions, rejected syntax with Position feedback, Escape, pointer focus without a command, equal widths at 960 and 492 units, per-item retention, the badge and Reset. The injected native smoke types into Y, applies with Return and steps with Down.

## Visibility choice

The first Display row is an ordinary [RadioGroup](radio-group.md) with stable ID `visibility` and keys `inherited`, `visible` and `hidden`, initially `inherited` for every item. Each option requests its own `visibility.<key>` command; the host stores the accepted key on the selected item, so selection-follows-focus arrows and pointer presses share one command path. The muted grid label names the row visually; the stylesheet hides the generated `visibility-title`, which still names the semantic `radio_group`. Options use the same rounded hover/press/focus decoration as other rows, and the checked option's label is blue. The group is a `flex_wrap: wrap` row with a 4-unit gap: the three choices share one line in a wide Inspector and wrap Hidden onto a second line in the narrow control column of a 492-unit window, where the Shadows row moves below both lines.

The value is host-owned application data only: it does not hide Hierarchy rows or affect other properties. The window-free smoke selects Display, then checks semantic roles and checked state, Tab entry from the Display tab at the checked option, wrapping arrows with one command per move, Shift+Tab exit back to the tab, pointer selection, the checked-label emphasis and independent per-item retention. It also checks one line at the default width, and at 492 units the wrapped lines inside the control column, Shadows below them, a pointer press on the wrapped Hidden choice and unchanged declared arrow order. The Purpose smoke checks outside dismissal with a press on a Hierarchy row, which neither selects it nor clicks through. The injected native smoke reaches the group with Tab after selecting Display and selects with arrow messages. Because the card is now taller, the native smoke wheels the Inspector back to its top before pressing the selected-item button.

## Shadows switch

The second Display row is an ordinary [Switch](checkbox-switch.md) with stable ID `shadows`, labeled Cast shadows and initially on for every item. It requests the `item.shadows` command, which inverts the selected item's host-owned value; Enter/Space, gamepad Activate and a pointer press on any part share that command. The row uses the `control` class like Enabled; its generated track is blue with the thumb at the end while on, and gray with the thumb at the start while off.

Enabled remains a [Checkbox](checkbox-switch.md) and Display name a [TextInput](text-number-input.md), which projects a `text_input` named Name with the edited text as its value. The toggles project `checkbox`/`switch` roles named by their labels, with checked state in `selected`, instead of buttons whose names include `[x]`. Like Visibility, the shadows value is application data only: nothing in this example renders shadows from it.

The window-free smoke selects Display, then checks the switch role, name and state, Tab arrival after Visibility, Activate through one command, thumb/track presentation relative to the unchanged track size, that arrows do not toggle, track pointer activation and independent per-item retention. The injected native smoke reaches the switch with Tab and toggles it with Space.

## Purpose ComboBox

The third Display row is an ordinary [ComboBox](combobox.md) with stable ID `purpose`, choosing the item's USD-style imageable purpose: `default`, `render`, `proxy` or `guide`, initially `default` for every item. The trigger uses the `control` class with a muted `v` marker; the `purpose-list` popup reuses the menus' `popup` class, is at least as wide as the trigger and is composed under the root only while the Display tab is shown. Options use the rounded row decoration and the selected option's label is blue.

`purpose.toggle` opens or closes the list. The host keeps one open popup at a time, so the list shares `State::menu` with the item menus. Each option requests `purpose.<key>`, which stores the key on the selected item and closes the list; the dispatcher's refresh then returns focus to the trigger. Alt+Down on the focused trigger runs the same toggle command. Escape, Alt+Up and outside presses close without choosing or clicking through; Tab closes and continues from the trigger. Purpose is application data only: nothing in this example filters rendering by it.

The window-free smoke selects Display, then checks the closed `combobox` role, name, value and collapsed state, Tab arrival after Cast shadows, that plain Down neither opens nor changes the value, Alt+Down opening through one command at the selected option, the list's placement and width, arrow movement without commands, Enter choosing and returning focus, the Modified badge, reopening at the new choice, Escape and Tab dismissal, pointer open/hover/choice, an outside press without click-through, the selected-label emphasis, per-item retention and Reset. The injected native smoke reaches Purpose with Tab, opens it with Space and chooses Render with Down and Return.

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

The selected Hierarchy row is raised with ordinary styles:

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

Display name accepts Windows IME composition. While it is focused and eligible, the host attaches the IME and hides the default composition window; elsewhere the IME is detached, so keys keep their navigation meaning. Each platform session becomes ordinary revision-addressed [composition steps](../architecture/input.md#implemented-ime-composition-state) through the existing `item.name` command:

1. Start captures the current selection; typing with a selection replaces it on commit.
2. Updates change only the presented preedit and its caret. The Hierarchy and committed name keep the accepted bytes, and the field's third line reads `Composition <begin>:<end> caret <caret>`.
3. The IME result commits once; Escape, or ending without a result, cancels and keeps the selection.

Keys and characters during a session stay with the IME, so Tab, Enter and arrows do not navigate. Pressing elsewhere first asks the IME to complete, then moves focus; Tab, selection changes and window deactivation cancel instead. A late step for a closed or replaced composition, or for another item, is rejected without editing. A commit beyond the 4096-byte limit closes the composition, keeps the committed name and shows caption feedback.

The application text service gives common wide East Asian scalars two 0.5 em placeholder cells, and the native GDI adapter draws them with MS Gothic (Segoe UI Emoji for emoji) so Japanese names and preedit stay legible. This is an [application-local cell policy](../../examples/inspector-hierarchy/cell_text.hpp), not Unicode width conformance or real shaping. The candidate window follows a logical caret rectangle derived from that layout.

The window-free smoke covers inline preedit, interior carets across two-cell glyphs, commit/selection replacement/cancel, late steps, Tab and pointer cancellation without redirecting to the new item, sessions outside the editor, an over-limit commit and presentation rejection/retry. The injected native smoke substitutes composition strings for an IME and checks UTF-16 conversion, a cursor inside a surrogate pair, malformed results, key suppression during a session, candidate placement, end/focus-loss cancellation, press completion and IME attachment following focus.

## Host and observation boundary

The host owns all item data, selection, disclosure, scroll offsets, input dispatchers and command sequences. Item actions resolve against the accepted semantic tree and command snapshot. A candidate copies host state, applies the public text transform where needed, and stages lowering, eligibility, styles, layout, paint and inspection. Only a successful command outcome accepts that candidate and publishes the session. A stale/foreign text owner or rejected text measurement leaves the accepted values and inspection unchanged; retry uses the retained edit revision.

The window-free smoke covers select/edit/select-another/return, Visibility choice, Purpose choice, independent item edits, foreign-owner rejection, presentation rejection/retry, collapse, actual wheel offsets, keyboard/pointer pane resizing, Menu/ContextMenu, and shared selection/property observations. It records accepted host-built view replacements and a selection action through ordinary [Replay](../architecture/replay.md), round-trips Replay JSON, compares playback and checks the final selected name, enabled flag and amount. This reproduces the observed views and action, not application command execution, raw text keys or native messages.

The application's synchronous `clipboard_read` and `clipboard_write` callbacks supply owned UTF-8 read results and located failure diagnostics outside core traversal. Copy preserves owner/revision/selection and publishes no UI update. Cut and paste capture the selected item and edit observation before calling the host, recheck focus, eligibility and identity afterward, then stage the existing `item.name` command. Cut deletes only after a write returns no diagnostics. Reentrant host changes cannot redirect the result to another item, selection or focused control. A failed clipboard service, invalid replacement or failed presentation retains the accepted state; presentation errors are bounded to the command result limit while retaining their original diagnostic codes. Clipboard writes and UI publication are separate: if cut copies successfully but its target changes or presentation fails, the clipboard keeps the copied bytes while no deletion is accepted. Retrying uses the retained selection and revision. No asynchronous clipboard protocol or clipboard-result replay encoding is introduced.

Name-input rejection returns to the host instead of throwing out of the message loop. Feedback is application-owned and independent of the UI generation: the native caption can report a failed measurement without trying to rebuild the rejected presentation. It is not part of semantic inspection or Replay, and it does not turn unrelated host failures into recoverable results. Clipboard no-ops with an empty copy/cut selection retain any previous notice. The native host prepares the candidate's glyph atlas before command publication; a frame that exceeds its 4096-glyph occurrence budget rejects while retaining the accepted view. This frame-wide budget counts both the Hierarchy and Inspector names plus other labels, so the text editor's 4096-byte limit does not guarantee every native presentation fits.

Windows hosting reuses the Settings starter's [optional host](../../examples/settings-starter/native_win32.hpp): pointer capture/cancellation, logical coordinate mapping, keyboard/text messages, GDI glyph coverage and CPU presentation. The application-local [clipboard adapter](../../examples/inspector-hierarchy/clipboard_win32.hpp) uses `CF_UNICODETEXT`, strict UTF-16/UTF-8 conversion, bounded reads and explicit Windows memory ownership. Open/format/lock/write failures return diagnostics without throwing through the message loop.

The window-free checks cover rejected committed name text and text/deletion/selection presentation staging, unchanged items/focus/inspection, reversed Unicode selection and a successful retry. Clipboard checks use injected services for reverse Unicode selections, partial replacement/deletion, modifier/repeat/focus guards, retained independent items, service/validation/presentation failures, retry and reentrant target changes. The injected native smoke checks arrow/Activate selection, text/Checkbox/Slider/Scale/Visibility/Switch edits, Display tab selection, toolbar arrows and Reset, pointer select/return after wheeling the Hierarchy back into view (each smoke click requires the pointer to reach its control), right-click Rename, splitter capture and window resize, plus Ctrl+C/X/V routing, suppression of translated control characters/repeats, Unicode cut/paste, rejection recovery and strict Unicode conversion. It also verifies caption feedback and recovery after rejected typing, a full Unicode name buffer, invalid paste and an over-budget ASCII glyph atlas. It substitutes an in-memory clipboard, leaving the user's OS clipboard untouched. Live OS clipboard exchange, physical devices and IME sessions, real text shaping, GPU rendering, native editable accessibility and persistence remain unvalidated. Capability claims and validation limits live in [support](../reference/support-matrix.md).
