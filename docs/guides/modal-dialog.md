# Modal and Dialog

The [settings-reset example](../../examples/modal-dialog/main.cpp) uses public `tessera::core` APIs to confirm a destructive operation. It is a deterministic scripted/headless consumer, not a native window. [Authoring](../architecture/ui-model.md#implemented-c-form-authoring), [layout](../architecture/layout.md#implemented-modal-placement), [input](../architecture/input.md#implemented-modal-interaction) and [semantics](../architecture/semantics.md#implemented-prototype-projection) own the contracts.

## Compose a controlled confirmation

```cpp
namespace ui = tessera::authoring;
auto form = ui::Column({
    ui::Button("Reset settings...", "settings.open_reset",
        {.node = {.id = "reset"}}),
    ui::Dialog("Reset settings?", {
        ui::Text("Restore defaults?"),
        ui::Button("Cancel", "settings.cancel_reset",
            {.node = {.id = "cancel"}}),
        ui::Button("Reset", "settings.confirm_reset",
            {.node = {.id = "confirm"}})
    }, {.node = {.id = "reset-dialog"}, .open = reset_open})
});
```

Register the three ordinary host actions. Open only changes `reset_open`; Cancel closes without changing settings; confirmation resets settings and closes through the ordinary command registry/executor. Put safe Cancel first so opening chooses it.

Compose **one** Modal or Dialog directly under the document root, after all ordinary content and Popup/Tooltip siblings. Give it a stable ID. `Dialog` generates `<id>-panel` and `<id>-title`, labels its semantic `dialog` entry with that title and accepts ordinary children. `DialogProps::panel` supplies panel styles/classes/source and optionally a replacement panel ID. Default content is centered, maximum width 360, white and scrollable; normal theme/instance styles can change appearance. `Modal(children, ModalProps)` exposes the same full-boundary layer for custom content, without a generated panel/title. Give custom dialogs an appropriate `labelled_by` relationship through the document vocabulary when needed.

Both use controlled `open` (default false). The layer forces Stack/none display and clip overflow, with a default translucent black backdrop. Its border box fills the root-content/viewport intersection rather than using its own dimensions or margins. Its padding controls the content area. Automatic direct-child dimensions fit that area subject to their own minima and border/padding; explicit dimensions or impossible minima may overflow and are clipped. No automatic child scaling occurs.

## Wire the host

1. Retain `ModalDispatcher("reset-dialog")`, `FocusDispatcher`, `PointerDispatcher` and controlled application state.
2. Resolve a coherent accepted tree/styles/layout view. Refresh pointer and focus, then call `modal.refresh(hit, focused)` on **closed views too**, including after pointer/programmatic focus changes. This remembers the opener's stable author ID before opening.
3. Apply returned `focus` with `FocusDispatcher::focus` in that snapshot. Opening focuses the first eligible dialog control; compatible replacements preserve focus. With no controls, ordinary focus recovery clears focus. Tab/Shift-Tab wrap within the Modal; directional gamepad navigation cannot select background controls.
4. On `reset_pointer`, reset/cancel ordinary pointer presses. Dispatch Modal before ordinary input. On `consumed`, do not route the event again. `dismiss` requests controlled closure; publish the closed view and refresh before applying the returned opener focus. Cancel/unmodified Escape closes; backdrop clicks do **not** close or confirm.
5. Route inside pointer events and normalized keyboard/gamepad commands through existing dispatchers, action resolution and command execution. Shared eligibility blocks background hits, focus, wheel bubbling and semantic actions. `active` lets the host arbitrate global shortcuts, world input and other transient UI; core cannot suppress host commands that bypass UI owners.
6. Route panel wheel input through `route_scroll`; store its offsets under a stable panel ID and project them into `LayoutInput::scroll_offsets`. Reveal changed focus through `scroll_into_view`. Recompute before paint and hit testing; keep unchanged focus from undoing intentional wheel scrolling.
7. Recompose/refresh on closure, resize, eligibility changes or unmount. An unavailable open owner requests dismissal. Restore only a surviving eligible opener; otherwise retain ordinary focus recovery. Do not retain node handles across replacement.

The example preserves its accepted press snapshot until release; replacing a `UiTree` intentionally cancels ordinary pointer presses. Opening/closing also requests reset for compatible same-tree changes, preventing deferred activation across the modal boundary.

## Build and run

From the repository root with the documented [core toolchain](development.md#core-build-workflow):

```powershell
cmake -S . -B build
cmake --build build --config Debug --target tessera_modal_dialog tessera_modal_tests --parallel 4
ctest --test-dir build -C Debug -R '^(modal|modal_dialog)$' --output-on-failure
& .\build\Debug\tessera_modal_dialog.exe
```

The consumer checks pointer cancellation/confirmation, keyboard opening, gamepad navigation/confirmation/Cancel, Escape, background blocking, resize, scroll/focus reveal and opener recovery. The [owner fixture](../../tests/input/modal_tests.cpp) adds CPU pixel/hit agreement, empty scope, replacement/unmount/hidden-owner recovery, invalid/stale input and UI/Semantic JSON and Replay v1/v2 preservation.

No nested/stacked modals, modal-anchored Popups, presentation reparenting, native window/device, accessibility adapter, real-font capture, focus animation or automatic host-global command capture is included. The optional [latest-UI Vulkan regression](development.md#latest-ui-gpu-regression) captures this example's opening, confirmation and narrow scrolled dialog. Replay uses ordinary scope eligibility and explicit reload/host updates for controlled visibility; it does not instantiate the host-owned dismissal dispatcher. [Support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#modal-and-dialog-evidence--2026-11-04) owns configuration/evidence limits.
