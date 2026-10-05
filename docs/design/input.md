# Input, focus, and navigation

## Host boundary

The host polls platform devices and translates their data into normalized Tessera events. Tessera does not poll OS input APIs.

The [Win32 example host](rendering.md#implemented-win32-vulkan-example-host) is the first native normalization. The mouse is `PointerId` 1; client pixels map to logical positions by its pixel-center rule, and timestamps come from the monotonic performance counter. Left/right/middle messages become primary/secondary/middle down/up with shift/control/alt/super modifiers. The host captures the mouse while any button is held and releases capture after dispatching the last up. It sends `PointerCancel` on capture loss it did not initiate (`WM_CAPTURECHANGED`), `WM_CANCELMODE`, `WM_KILLFOCUS`, and `WM_MOUSELEAVE` with no button held (clearing hover). Input before the first layout snapshot is ignored. Returned action requests run after dispatch; the example maps `start-game` to a title change and `quit-game` to orderly shutdown.

## Implemented event contract

[event.hpp](../../include/tessera/input/event.hpp) defines `InputEvent`: a host monotonic timestamp in microseconds (non-negative, non-decreasing within one stream) and one of these values:

| Event | Contents and rules |
| --- | --- |
| `PointerMove`, `PointerDown`, `PointerUp` | `PointerId`, position in [layout](layout.md) logical coordinates, `primary`/`secondary`/`middle` button for down/up, `Modifiers` |
| `PointerCancel` | The host abandoned the pointer (lost window focus, capture loss, device removal) |
| `Scroll` | Position and delta in logical units; positive values reveal content further right/down |
| `KeyDown`, `KeyUp` | Logical (layout-mapped) `Key`: navigation/editing keys, `a`-`z`, digits, F1-F12; `repeat` on key down; modifiers |
| `TextInput` | Nonempty committed UTF-8 text; no composition/IME state |
| `Navigate` | `up`/`down`/`left`/`right` after the host applied dead zones and repeat policy; `repeat` flag |
| `FocusNext`, `FocusPrevious`, `Activate`, `Cancel` | Logical commands without payload |

A `PointerId` is host-assigned and stable from press until release or cancel. `Modifiers` holds shift, control, alt, and super flags. Characters never arrive as `Key` values. The host owns gamepad mapping, dead zones, and repeat timing. `validate(InputEvent)` reports non-finite positions/deltas (`invalid_number`), negative timestamps (`out_of_range`), undeclared enumeration values (`unknown_value`), and empty or invalid text (`empty_text`, `invalid_utf8`). Ordering across events is not validated.

Proposed baseline: a host delivers a given key press either as a logical command or as `KeyDown`/`KeyUp`, not both.

Key events and text input are separate operations. The presence of `TextInput` does not imply a text editor or full IME composition support; composition, clipboard, and editable-text semantics need a later contract.

## Hit testing and propagation

### Implemented rectangular hit testing

[pointer.hpp](../../include/tessera/input/pointer.hpp) defines `HitTestInput`, borrowing a coherent `UiTree`, its resolved-style span, and its `LayoutResult` for one query. `hit_test(input, position)` returns a `HitTestResult` with an optional target; a successful query outside all eligible boxes has no target, whereas invalid input has no result value.

- Scan boxes in reverse tree preorder, matching the [paint order](rendering.md#implemented-paint-generation). Every eligible Box/Text can be a hit target, including a label without a binding and a transparent or zero-opacity box.
- Use border-box rectangles, including their border area. Left/top edges are inclusive; right/bottom edges are exclusive. Zero-area boxes never hit. Corner radius does not cut away the rectangular hit region.
- Display-none subtrees are absent. Visibility is local: a hidden box is excluded, but a visible descendant remains eligible. A node's boolean `disabled` property defaults to false and excludes its entire subtree when true; disabled subtrees are skipped, so an eligible box behind them can receive the hit.
- Overflowing children can hit outside the parent or viewport. There is no implicit clipping or transform, no pointer-events property, and no dependence on draw commands or renderer state. Scrolling/transforms must add shared coordinate/clip semantics before those features arrive.

`validate(HitTestInput)` uses the same tree/style/layout snapshot checks as [paint input](rendering.md#implemented-paint-generation), without a shaper requirement. Missing inputs, style count/values, displayed topology, foreign handles, parent indices, recorded edges/visibility, and numeric geometry are rejected with the same located codes. `hit_test` also validates the point under `/position`. Callers must recompute layout after geometry/text changes; validation does not detect every stale dimension or text metric.

### Implemented pointer dispatch

`PointerDispatcher` is a host-owned, single-stream object. `dispatch(snapshot, event)` accepts only `PointerMove`, `PointerDown`, `PointerUp`, and `PointerCancel`; other normalized events return `unsupported_event` at `/event`. It validates the event and snapshot before changing state. Timestamps must be non-decreasing across accepted events (`event_order` at `/timestamp` for backwards time), including after a tree replacement; equal times are allowed. Invalid calls leave the stream clock and pointer states unchanged. `reset()` clears both.

The owned `PointerDispatchResult` contains the current event's hit target, action requests, and pointer states sorted by `PointerId`. Each `PointerState` exposes position, the deepest/topmost `hovered` target, the `pressed` activation owner, `active`, and `primary_down`. Only values/handles are retained, never borrowed tree/style/layout pointers or callbacks.

- **Binding lookup.** From the hit target, walk toward the root and select the first eligible node with an `activate` binding. Hidden ancestors are skipped. This lets a Text label activate its containing button and lets release on the same button's padding match the press. At most one binding is selected; full event propagation, cancellation/default prevention, capture phases, and arbitrary callbacks are not implemented.
- **Press/release.** Primary down records that binding owner, without requesting an action. Primary up requests one activation only when a primary press exists and the release's binding owner is still the recorded owner, then clears press/active state. An outside down, a release without down, or release over another binding cannot activate. Repeated primary down while held does not replace the original press.
- **Move.** Moving out retains the press but clears `active`; returning to the same binding restores `active`, allowing a drag-out/back release to activate. Secondary/middle events update position/hover but neither start nor release the primary press.
- **Cancel.** `PointerCancel` erases only that pointer, produces no action, and makes a later release harmless. The host must send cancellation on focus/capture/device loss. Multiple pointers retain independent presses; there is no OS capture or device polling in the dispatcher.
- **Snapshot changes.** Each accepted dispatch refreshes all remembered positions against the supplied layout. `refresh(snapshot)` does the same without an event, timestamp change, or action, so stationary pointers can recover after geometry/style changes. An absent, hidden, or disabled press owner is cancelled and cannot revive before another press. A different tree identity clears all pointers before accepting new events; author IDs do not restore a press across replacement.

Hover is stored only on the hit target, and active only on its selected binding owner. Ancestor pseudo-state matching and stylesheet invalidation remain for style resolution. `cancel` action bindings and logical `Activate`/`Cancel` await focus/navigation dispatch.

### Action registration and lifetime

Host action registration is explicit. The foundation stores `activate`/`cancel` action names and rejects names absent from the caller's `ValidationContext.actions`; see [UI model](ui-model.md).

Implemented contract: Tessera never stores host callbacks. Pointer dispatch produces `ActionRequest` values (binding name, owned action name, target binding-owner `NodeHandle`) and returns them to the host. The host runs its own action implementations after dispatch returns, and applies resulting mutations at its next update point, so no callback runs during traversal and callback lifetime is entirely host-owned. A request's target handle is valid only while the producing tree snapshot is alive; after replacement, `UiTree::get` rejects stale targets. Pointer states are cleared on replacement as specified above.

## Event-to-action boundary

Normalized device events describe interaction; application-facing actions describe intent. Pointer, keyboard, gamepad, touch, accessibility, and external automation should reach the same declared action path after eligibility checks. Components do not directly call host functions. The implemented `ActionRequest` contract above is the baseline; value-setting/open/back payloads and semantic invocation are proposals requiring validation and lifetime rules before extending it.

[Semantics](semantics.md) owns exposed roles/names/action availability. It consumes input state and refers to actions here, rather than duplicating focus or dispatch policy.

[Inspection operations](inspection.md#proposed-target-resolution-and-operations) resolve targets before entering this path. Semantic activate/set-value and raw pointer/key injection are distinct operations with the same eligibility constraints. Automation must not turn unsupported text editing or value actions into direct property mutation.

## Interaction state

Input owns interaction behavior. Pointer hover/active and inherited disabled filtering are implemented above; focus and stylesheet [pseudo states](styling.md) remain planned. Host updates must resolve styles/layout and call `refresh` or dispatch with the new snapshot. Disabled focus policy and automatic style/layout/paint invalidation remain to be defined.

## Focus and navigation

Keyboard and gamepad operation are architectural requirements even though pointer interaction is delivered first. Model focusable nodes, traversal order, directional navigation, activation, and cancel/back from the beginning.

Proposed baseline: tree-ordered sequential traversal and explicit overrides; directional movement uses deterministic geometry-based selection with a tie-break rule. Finalize eligible-node filtering, explicit neighbors, focus scopes, wrapping, modal behavior, and focus recovery when the focused node is hidden, disabled, or removed.

Map gamepad input into logical navigation/activation/cancel actions at the host boundary. Dead zones and repeat policy must have an explicit owner. Do not put device-specific polling in buttons.

Delivery scope is owned by [current](../roadmap/current.md) and [backlog](../roadmap/backlog.md).

## Proposed overlay interaction

Use the [portal model](ui-model.md#proposed-overlays-and-portals) and shared [overlay geometry](layout.md#scrolling-and-overlay-geometry) for targeting. Define modal input capture, focus scope/restoration, dismissal/back, and hit order consistently with presentation order. Tooltip lifetime belongs to interaction state. Specify behavior when anchors or owners disappear; no implicit OS pointer capture is introduced by a portal.

## Proposed editing, IME, and clipboard boundary

Keep committed `TextInput` separate from composition start/update/commit/cancel. A text-editing contract needs replacement ranges, selection, caret, and copy/cut/paste requests. Declare index units and validate ranges against the document revision; UTF-8 bytes, graphemes, and glyph indices are distinct under [text](text.md).

The host normalizes platform IME events, owns clipboard access, and receives caret/selection geometry for candidate-window placement. Core stores editing state without platform types or implicit clipboard calls. Composition cancellation, focus loss, asynchronous paste, read-only/disabled policy, and stale replacement ranges need explicit fixtures. Japanese composition is a first-class requirement; boundary design can precede platform adapters and editable TextInput implementation.

The [Web editing adapter](web-host.md#proposed-text-editing-adapter) may use browser-native input facilities alongside GPU visuals. It translates index units, revisions, focus, and composition into this same contract without exposing browser-specific controls in the UI model.

## Verification

[Pointer checks](../../tests/input/pointer_tests.cpp) cover overlap/preorder, half-open edges, overflow, zero-area boxes, display/visibility/opacity, disabled subtrees, label-to-button binding lookup, press/release/drag/cancel, secondary buttons, multiple pointers, snapshot replacement/refresh, timestamps, and rejected calls preserving state. The core-only [pointer-menu example](../../examples/pointer-menu/main.cpp) creates a document, lays it out, generates paint, and delivers synthetic clicks as host action requests.

Clips/transforms, scrolling, full propagation, focus traversal/recovery, directional ties, and logical activation/cancel need later fixtures. The Win32 host smoke exercises OS-message normalization, capture-loss/cancel-mode delivery, and GPU integration through posted window messages; it is not physical-device evidence.
