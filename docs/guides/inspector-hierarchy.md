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

- Select Cube or another row in the [TreeView](tree-list.md). Arrows move focus, Enter/Space selects, and Left/Right collapses/expands Scene. Disclosure never changes controlled selection.
- Tab to the [Grid Inspector](grid.md). Edit Display name with committed text, exact Ctrl+A, arrows, Shift selection, Home/End and Backspace/Delete. Text editing consumes supported keys before tree navigation.
- With Display name focused, exact non-repeating Ctrl+C copies the selected committed bytes, Ctrl+X copies and then deletes the selection, and Ctrl+V replaces the selection through the ordinary name-edit command. Windows interactive mode exchanges Unicode text with the OS clipboard. Empty selection copy/cut and empty text paste do nothing. Paste rejects CR/LF, embedded NUL, malformed Unicode and results above 4096 UTF-8 bytes, preserving the accepted name and selection; diagnostics print to stderr and the window remains usable.
- Rejected name input, including typing beyond the 4096-byte limit, keeps the accepted text, selection and focus. Windows shows name-edit and clipboard failures in the window title, with a short reason for text validation failures. A successful name edit or clipboard operation clears the notice; navigation alone retains it. Stderr records the operation, original target item and located diagnostics. Presentation rejection also preserves the accepted inspection and view, so retry can use the retained revision.
- Activate Enabled or adjust Amount with Left/Down, Right/Up, Home/End or a pointer drag. Amount has integer stops from 0 to 100 in steps of 5.
- Select another item and return. Every item retains its own text owner, revision, UTF-8 byte selection, enabled flag and amount. The edited display name also appears in the Hierarchy.
- Wheel either pane or drag the [SplitPane](split-pane.md) boundary. Focus navigation reveals controls; scrolling and pane/window resizing do not change item values. Pane minima are 140/320 logical units plus the 8-unit handle; smaller windows clip overflowing minima.
- Open Item actions, or right-click the selected-item button at the top of the Inspector. Shift+F10 opens the same context actions from that button. Rename focuses the retained name editor; Toggle enabled shares the Checkbox command. Escape/Cancel or an outside press dismisses the menu. These operations need no destructive confirmation, so no Dialog is added.

The context target is the selected-item button, not arbitrary unselected hierarchy rows. No command silently selects a different item on right-click.

## Application appearance

The ordinary form now uses a larger heading, a white rounded Hierarchy panel, muted property labels and compact multiline help. Hierarchy rows have a 30-unit minimum height; the controlled selection has a blue fill and white text, while keyboard focus has a separate inside border and smooth outside outline. Buttons, name editing, the enabled value, rounded Slider parts and padded item menus share the blue-gray palette. The property card retains its radial highlight and outer shadow; the name input has a subtle inset shadow.

The application-local stylesheet targets public author IDs and classes, including the documented generated TreeView, Slider and Menu IDs. Hover, active and focus rules change paint only: their borders already reserve space in the base styles. The host resolves feedback against the accepted tree and layout, then prepares paint and inspection together before replacing either. Stationary hover survives ordinary view replacements through the existing compatible pointer policy; pointer release/cancellation clears active feedback, including the custom Slider drag. Hover never edits item data or publishes a command generation.

The Inspector uses a 72-unit label column and a control column with a 202-unit minimum. The 200-unit Slider track has no extra horizontal padding, so its full travel fits the existing 320-unit pane minimum, including card borders and pane/card padding. Both pane minima fit a 492-unit window including the application padding; smaller windows still clip the overflowing minima. Focus reveal and wheel scrolling retain their ordinary behavior.

The window-free smoke checks distinct hover/press/selection/focus pixels at scales 1, 1.25, 1.5 and 2, inherited selected-label color, stable row geometry, cancellation and once-only selection, stationary Slider feedback, minimum-width track containment and retained item data. Pixel comparisons omit glyph commands to isolate the changed decorations; the injected Windows smoke still presents the complete form with the existing GDI glyph adapter. Base appearance remains application styling, not a new widget skin API or text shaper; the focus decoration uses the built-in stroke below.

## Focus outlines

Focused controls use a solid 1.5-unit blue outside outline with a one-unit gap. Focused Hierarchy and menu rows use the same rounded geometry with four-unit dashes and two-unit gaps:

```cpp
.outline = Outline{1.5f, 1.f, {.2f, .4f, .85f, 1}, 4.f, 2.f}
```

This ordinary optional style emits the [smooth stroke command](../architecture/rendering.md#implemented-outlines-and-dashed-strokes); CPU and Vulkan reference/batch share the built-in behavior. Dash phase continues around the corners, rather than restarting per edge. The existing inside focus border and selected fill remain, so selection and focus are still distinct. Blur removes the old outline through ordinary style resolution.

Outlines occupy existing outside space without changing control geometry, input bounds, editing, selection, scrolling or pane minima. Ancestor clips trim them; focus reveal still follows the layout box rather than expanding scroll allocation for decoration. The window-free smoke checks resolved solid/dashed styles, actual partially covered edge and opaque interior pixels at scales 1, 1.25, 1.5 and 2, stable geometry/data and removal on focus movement. The injected native smoke presents the full CPU-rendered form; GPU owner comparisons cover the primitive, not this application's glyphs.

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
                      {1, {.98f, .9f, .98f, 1}}}}
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

## Host and observation boundary

The host owns all item data, selection, disclosure, scroll offsets, input dispatchers and command sequences. Item actions resolve against the accepted semantic tree and command snapshot. A candidate copies host state, applies the public text transform where needed, and stages lowering, eligibility, styles, layout, paint and inspection. Only a successful command outcome accepts that candidate and publishes the session. A stale/foreign text owner or rejected text measurement leaves the accepted values and inspection unchanged; retry uses the retained edit revision.

The window-free smoke covers select/edit/select-another/return, independent item edits, foreign-owner rejection, presentation rejection/retry, collapse, actual wheel offsets, keyboard/pointer pane resizing, Menu/ContextMenu, and shared selection/property observations. It records accepted host-built view replacements and a selection action through ordinary [Replay](../architecture/replay.md), round-trips Replay JSON, compares playback and checks the final selected name, enabled flag and amount. This reproduces the observed views and action, not application command execution, raw text keys or native messages.

The application's synchronous `clipboard_read` and `clipboard_write` callbacks supply owned UTF-8 read results and located failure diagnostics outside core traversal. Copy preserves owner/revision/selection and publishes no UI update. Cut and paste capture the selected item and edit observation before calling the host, recheck focus, eligibility and identity afterward, then stage the existing `item.name` command. Cut deletes only after a write returns no diagnostics. Reentrant host changes cannot redirect the result to another item, selection or focused control. A failed clipboard service, invalid replacement or failed presentation retains the accepted state; presentation errors are bounded to the command result limit while retaining their original diagnostic codes. Clipboard writes and UI publication are separate: if cut copies successfully but its target changes or presentation fails, the clipboard keeps the copied bytes while no deletion is accepted. Retrying uses the retained selection and revision. No asynchronous clipboard protocol or clipboard-result replay encoding is introduced.

Name-input rejection returns to the host instead of throwing out of the message loop. Feedback is application-owned and independent of the UI generation: the native caption can report a failed measurement without trying to rebuild the rejected presentation. It is not part of semantic inspection or Replay, and it does not turn unrelated host failures into recoverable results. Clipboard no-ops with an empty copy/cut selection retain any previous notice. The native host prepares the candidate's glyph atlas before command publication; a frame that exceeds its 4096-glyph occurrence budget rejects while retaining the accepted view. This frame-wide budget counts both the Hierarchy and Inspector names plus other labels, so the text editor's 4096-byte limit does not guarantee every native presentation fits.

Windows hosting reuses the Settings starter's [optional host](../../examples/settings-starter/native_win32.hpp): pointer capture/cancellation, logical coordinate mapping, keyboard/text messages, GDI glyph coverage and CPU presentation. The application-local [clipboard adapter](../../examples/inspector-hierarchy/clipboard_win32.hpp) uses `CF_UNICODETEXT`, strict UTF-16/UTF-8 conversion, bounded reads and explicit Windows memory ownership. Open/format/lock/write failures return diagnostics without throwing through the message loop.

The window-free checks cover rejected committed name text and text/deletion/selection presentation staging, unchanged items/focus/inspection, reversed Unicode selection and a successful retry. Clipboard checks use injected services for reverse Unicode selections, partial replacement/deletion, modifier/repeat/focus guards, retained independent items, service/validation/presentation failures, retry and reentrant target changes. The injected native smoke checks arrow/Activate selection, text/Checkbox/Slider edits, select/return, right-click Rename, splitter capture and window resize, plus Ctrl+C/X/V routing, suppression of translated control characters/repeats, Unicode cut/paste, rejection recovery and strict Unicode conversion. It also verifies caption feedback and recovery after rejected typing, a full Unicode name buffer, invalid paste and an over-budget ASCII glyph atlas. It substitutes an in-memory clipboard, leaving the user's OS clipboard untouched. Live OS clipboard exchange, physical devices, IME composition, real text shaping, GPU rendering, native editable accessibility and persistence remain unvalidated. Capability claims and validation limits live in [support](../reference/support-matrix.md).
