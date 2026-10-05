# Input, focus, and navigation

## Host boundary

The host polls platform devices and translates their data into normalized Tessera events. Tessera does not poll OS input APIs.

The [Win32 example host](rendering.md#implemented-win32-vulkan-example-host) is the first native normalization. The mouse is `PointerId` 1; client pixels map to logical positions by its pixel-center rule, and timestamps come from the monotonic performance counter. Left/right/middle messages become primary/secondary/middle down/up with shift/control/alt/super modifiers. The host captures the mouse while any button is held and releases capture after dispatching the last up. It sends `PointerCancel` on capture loss it did not initiate (`WM_CAPTURECHANGED`), `WM_CANCELMODE`, `WM_KILLFOCUS`, and `WM_MOUSELEAVE` with no button held (clearing hover). Input before the first layout snapshot is ignored. Returned action requests run after dispatch; the example maps `start-game` to a title change and `quit-game` to orderly shutdown.

The same host translates `WM_KEYDOWN` into [focus dispatch](#implemented-prototype-focus-dispatch) commands, following the baseline below: a handled key press becomes one logical command and never also a `KeyDown`. Tab is `FocusNext` (Shift+Tab `FocusPrevious`), arrow keys are `Navigate` with `repeat` taken from the message's previous-key-state bit, Enter and Space are `Activate`, and Escape is `Cancel`. The OS auto-repeat drives traversal and navigation; repeated Enter/Space/Escape messages are dropped, so one press activates once. Keys pressed with Ctrl or Alt and unmapped keys go to the system. Host press policy: a primary `PointerDown` whose dispatch records a press owner also calls `focus` on that owner, so keyboard navigation continues from the button last pressed; the dispatcher's own rule that pointer dispatch never moves focus is unchanged. The focus dispatcher is refreshed with the pointer dispatcher at each update point. Focus has no resolved style yet; the host shows it by changing only the focused button's border color.

Gamepad input reaches the same commands; the host owns polling, dead zones, and repeat. It reads the first connected XInput controller only while its window is in the foreground and rescans empty slots at most once per second. A missing sample, as on disconnection or focus loss, resets the policy. The device-independent [gamepad policy](../../examples/vulkan-menu/gamepad.hpp) maps each sample. The D-pad takes precedence over the left stick, whose radial dead zone engages a direction above half deflection and releases it at XInput's recommended left-stick dead zone (7849/32767). The dominant stick axis selects the direction, vertical on ties, as is diagonal D-pad input. Each change of held direction is one `Navigate`; holding it repeats with `repeat` set after 400 ms and then every 120 ms, at most once per sample. A and B press edges are `Activate` and `Cancel`, emitted after navigation. Input already held when samples begin is ignored until released, so connecting a controller or refocusing the window never acts.

`WM_MOUSEWHEEL` and `WM_MOUSEHWHEEL` become one `Scroll` each at the message's screen position converted to client pixels and then to logical coordinates. One `WHEEL_DELTA` is 40 logical units and partial deltas scale proportionally; a forward vertical wheel reveals content above (negative Y), and a rightward tilt content to the right. The host passes the event to [scroll routing](#implemented-scroll-routing), stores the returned offsets, recomputes layout, and refreshes both dispatchers at that update point. Host reveal policy: when a logical command moves focus to another node, the host calls `scroll_into_view` for it and applies the result the same way. Pointer press-to-focus does not reveal, so a click never moves the pressed button out from under the cursor.

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
- A box with a recorded [layout clip](layout.md#implemented-prototype-algorithm) hits only inside that half-open rectangle, the one paint pushes; its clipped-out part falls through to eligible boxes below. Unclipped overflow can hit outside the parent or viewport. There is no implicit viewport clip or transform, no pointer-events property, and no dependence on draw commands or renderer state. [Scroll offsets](layout.md#implemented-prototype-algorithm) are already applied to recorded geometry and clips, so hit testing needs no separate scroll mapping. Transforms must add shared coordinate semantics before they arrive.

`validate(HitTestInput)` uses the same tree/style/layout snapshot checks as [paint input](rendering.md#implemented-paint-generation), without a shaper requirement. Missing inputs, style count/values, displayed topology, foreign handles, parent indices, recorded edges/visibility, and numeric geometry are rejected with the same located codes. `hit_test` also validates the point under `/position`. Callers must recompute layout after geometry/text changes; validation does not detect every stale dimension or text metric.

### Implemented pointer dispatch

`PointerDispatcher` is a host-owned, single-stream object. `dispatch(snapshot, event)` accepts only `PointerMove`, `PointerDown`, `PointerUp`, and `PointerCancel`; other normalized events return `unsupported_event` at `/event`. It validates the event and snapshot before changing state. Timestamps must be non-decreasing across accepted events (`event_order` at `/timestamp` for backwards time), including after a tree replacement; equal times are allowed. Invalid calls leave the stream clock and pointer states unchanged. `reset()` clears both.

The owned `PointerDispatchResult` contains the current event's hit target, action requests, and pointer states sorted by `PointerId`. Each `PointerState` exposes position, the deepest/topmost `hovered` target, the `pressed` activation owner, `active`, and `primary_down`. Only values/handles are retained, never borrowed tree/style/layout pointers or callbacks.

- **Binding lookup.** From the hit target, walk toward the root and select the first eligible node with an `activate` binding. Hidden ancestors are skipped. This lets a Text label activate its containing button and lets release on the same button's padding match the press. At most one binding is selected; full event propagation, cancellation/default prevention, capture phases, and arbitrary callbacks are not implemented.
- **Press/release.** Primary down records that binding owner, without requesting an action. Primary up requests one activation only when a primary press exists and the release's binding owner is still the recorded owner, then clears press/active state. An outside down, a release without down, or release over another binding cannot activate. Repeated primary down while held does not replace the original press.
- **Move.** Moving out retains the press but clears `active`; returning to the same binding restores `active`, allowing a drag-out/back release to activate. Secondary/middle events update position/hover but neither start nor release the primary press.
- **Cancel.** `PointerCancel` erases only that pointer, produces no action, and makes a later release harmless. The host must send cancellation on focus/capture/device loss. Multiple pointers retain independent presses; there is no OS capture or device polling in the dispatcher.
- **Snapshot changes.** Each accepted dispatch refreshes all remembered positions against the supplied layout. `refresh(snapshot)` does the same without an event, timestamp change, or action, so stationary pointers can recover after geometry/style changes. An absent, hidden, or disabled press owner is cancelled and cannot revive before another press. A different tree identity clears all pointers before accepting new events; author IDs do not restore a press across replacement.

Hover is stored only on the hit target, and active only on its selected binding owner. Ancestor pseudo-state matching belongs to [style resolution](styling.md#implemented-prototype-style-resolution); stylesheet invalidation is not defined. Logical `Activate`/`Cancel` and `cancel` bindings belong to [focus dispatch](#implemented-prototype-focus-dispatch).

### Implemented scroll routing

[scroll.hpp](../../include/tessera/input/scroll.hpp) defines two stateless operations over the same `HitTestInput` snapshot and validation as [hit testing](#implemented-rectangular-hit-testing). Both return `ScrollUpdate` values (scroll box handle and new requested offset), innermost box first and only for boxes whose offset changes. Tessera keeps no scroll state: the host stores each offset in its [`LayoutInput::scroll_offsets`](layout.md#implemented-foundation-contract) span and recomputes layout at its next update point, then refreshes pointer and focus dispatch as for any snapshot change. Neither operation requests an action.

- **Routing.** `route_scroll(snapshot, scroll)` hit-tests the event position with the ordinary rules and returns that target, the updates, and the `remaining` delta. Starting at the target and walking toward the root, each eligible scroll box (locally visible and not disabled; hidden scroll boxes are skipped as hidden binding owners are) moves each axis by the remaining delta within `[0, scroll_limit()]` and passes what it could not consume to the next. Axes chain independently. A position that hits nothing, including one over a disabled subtree with nothing eligible behind it, scrolls nothing. Non-finite positions or deltas are `invalid_number` at `/position` or `/delta`.
- **Reveal.** `scroll_into_view(snapshot, target)` moves each scroll box on the target's ancestry, innermost first, as little as possible so the target's border box lies within its viewport, carrying the adjusted position outward. A box larger than a viewport aligns its leading edge. Visibility, disabled state, and focusability do not matter; a handle from another tree is `stale_target` and a display-none node `hidden_target`, at `/target`. Results are float arithmetic, so a revealed trailing edge can land within rounding of the scroll limit rather than exactly on it.

Scroll anchoring, smooth or animated scrolling, keyboard page/home/end scrolling, and latching an in-progress wheel gesture to one scroll box are not defined.

### Action registration and lifetime

Host action registration is explicit. The foundation stores `activate`/`cancel` action names and rejects names absent from the caller's `ValidationContext.actions`; see [UI model](ui-model.md).

Implemented contract: Tessera never stores host callbacks. Pointer and focus dispatch produce `ActionRequest` values (binding name, owned action name, target binding-owner `NodeHandle`) and returns them to the host. The host runs its own action implementations after dispatch returns, and applies resulting mutations at its next update point, so no callback runs during traversal and callback lifetime is entirely host-owned. A request's target handle is valid only while the producing tree snapshot is alive; after replacement, `UiTree::get` rejects stale targets. Pointer states are cleared on replacement as specified above.

## Event-to-action boundary

Normalized device events describe interaction; application-facing actions describe intent. Pointer, keyboard, gamepad, touch, accessibility, and external automation should reach the same declared action path after eligibility checks. Components do not directly call host functions. The implemented `ActionRequest` contract above is the baseline. Prototype [focus activation](#implemented-prototype-focus-dispatch) and [semantic activation](semantics.md#implemented-prototype-projection) return the same request as pointer activation under the same disabled/hidden eligibility; value-setting/open/back payloads and generation-aware external invocation are proposals requiring validation and lifetime rules before extending it.

[Semantics](semantics.md) owns exposed roles/names/action availability. It consumes input state and refers to actions here, rather than duplicating focus or dispatch policy.

[Inspection operations](inspection.md#proposed-target-resolution-and-operations) resolve targets before entering this path. Semantic activate/set-value and raw pointer/key injection are distinct operations with the same eligibility constraints. Automation must not turn unsupported text editing or value actions into direct property mutation.

## Interaction state

Input owns interaction behavior. Pointer hover/active, inherited disabled filtering, and prototype focus are implemented in this page. The host passes the resulting states to the [prototype style resolver](styling.md#implemented-prototype-style-resolution) for `:hover`, `:active`, `:focus`, and `:disabled` rules. Host updates must resolve styles/layout and call `refresh` or dispatch with the new snapshot. Disabled focus policy and automatic style/layout/paint invalidation remain to be defined.

## Focus and navigation

Keyboard and gamepad operation are architectural requirements even though pointer interaction is delivered first. Model focusable nodes, traversal order, directional navigation, activation, and cancel/back from the beginning.

### Implemented prototype focus dispatch

[focus.hpp](../../include/tessera/input/focus.hpp) defines `FocusDispatcher`, a host-owned, single-stream prototype over the same `HitTestInput` snapshot and validation as [hit testing](#implemented-rectangular-hit-testing). It stores only the focused handle, that node's author ID, the tree identity, and the stream clock. It does not extend JSON v1.

- **Eligibility.** A node can take focus when it is displayed, locally visible, and not disabled by itself or an ancestor (the hit-testing rules), and its `focusable` property is true; absence means false. Opacity, zero area, and clipping do not affect eligibility, so a fully clipped node stays focusable until scrolling can reveal it.
- **Commands.** `dispatch` accepts `FocusNext`, `FocusPrevious`, `Navigate`, `Activate`, and `Cancel`; other events are `unsupported_event` at `/event`. Hosts translate keys and gamepad buttons into these commands; the dispatcher does not interpret `KeyDown`. Event validation and the non-decreasing timestamp rule (`event_order` at `/timestamp`) match pointer dispatch, and rejected calls leave state unchanged. The owned `FocusDispatchResult` holds the focused handle and action requests. `reset()` clears focus and the clock.
- **Sequential traversal.** Eligible nodes are ordered by tree preorder. `FocusNext` moves to the first eligible node after the focused one and wraps to the first; `FocusPrevious` mirrors it. Without focus, they select the first or last eligible node.
- **Directional movement.** `Navigate` compares border boxes in root logical coordinates. A candidate must lie entirely beyond the focused box's edge in that direction; touching is allowed, overlap is not. The lowest of (orthogonal projections do not overlap, edge distance, orthogonal gap, preorder) wins, so an aligned box beats a nearer unaligned one and equal candidates resolve to the earlier node. Without a candidate, focus stays; there is no wrapping. Without focus, every direction selects the first eligible node. `repeat` does not change selection.
- **Activation and cancel.** `Activate` walks from the focused node toward the root to the first eligible node with an `activate` binding, the [pointer binding lookup](#implemented-pointer-dispatch), so it requests exactly what a click on that node requests and a focused label activates its button. `Cancel` walks the same way for a `cancel` binding, starting at the root when nothing is focused. Neither moves focus; finding no binding requests nothing.
- **Recovery.** `refresh(snapshot)` and `dispatch` first reconcile the stored focus with the supplied snapshot. In the same tree, a focused node that became ineligible moves focus to the nearest following eligible node in preorder, else the nearest preceding one, else none. Under a different tree identity, focus is restored to the node with the same author ID, followed by the same rule. A focused node without an author ID, or an ID absent from the new tree, clears focus. When `dispatch` moves or clears focus this way, the command is consumed: the result reports the recovered focus without further movement or action, so input aimed at a stale view cannot activate another node.
- **Programmatic focus.** `focus(snapshot, target)` focuses an eligible node, for example under a host's pointer-press policy. At `/target` it rejects a handle from another tree (`stale_target`), display-none or hidden nodes (`hidden_target`), disabled nodes (`disabled_target`), and non-focusable nodes (`not_focusable`). Pointer dispatch never changes focus.

Explicit neighbor overrides and focus scopes need authored vocabulary beyond JSON v1. Modal scopes follow the [overlay proposal](#proposed-overlay-interaction). Focus dispatch does not scroll; hosts reveal a newly focused node through [scroll routing](#implemented-scroll-routing). [Semantic projection](semantics.md#implemented-prototype-projection) reports the focus a host supplies from this dispatcher; it does not store or recover focus. [Replay](replay.md#implemented-prototype-playback) plays logical commands through this dispatcher. Directional wrapping options remain open.

Map gamepad input into logical navigation/activation/cancel actions at the host boundary. Dead zones and repeat policy must have an explicit owner, as in the [Win32 host](#host-boundary). Do not put device-specific polling in buttons.

Delivery scope is owned by [current](../roadmap/current.md) and [backlog](../roadmap/backlog.md).

## Proposed overlay interaction

Use the [portal model](ui-model.md#proposed-overlays-and-portals) and shared [overlay geometry](layout.md#scrolling-and-overlay-geometry) for targeting. Define modal input capture, focus scope/restoration, dismissal/back, and hit order consistently with presentation order. Tooltip lifetime belongs to interaction state. Specify behavior when anchors or owners disappear; no implicit OS pointer capture is introduced by a portal.

## Proposed editing, IME, and clipboard boundary

Keep committed `TextInput` separate from composition start/update/commit/cancel. A text-editing contract needs replacement ranges, selection, caret, and copy/cut/paste requests. Declare index units and validate ranges against the document revision; UTF-8 bytes, graphemes, and glyph indices are distinct under [text](text.md).

The host normalizes platform IME events, owns clipboard access, and receives caret/selection geometry for candidate-window placement. Core stores editing state without platform types or implicit clipboard calls. Composition cancellation, focus loss, asynchronous paste, read-only/disabled policy, and stale replacement ranges need explicit fixtures. Japanese composition is a first-class requirement; boundary design can precede platform adapters and editable TextInput implementation.

The [Web editing adapter](web-host.md#proposed-text-editing-adapter) may use browser-native input facilities alongside GPU visuals. It translates index units, revisions, focus, and composition into this same contract without exposing browser-specific controls in the UI model.

## Verification

[Pointer checks](../../tests/input/pointer_tests.cpp) cover overlap/preorder, half-open edges, overflow, clipped targets and activation, zero-area boxes, display/visibility/opacity, disabled subtrees, label-to-button binding lookup, press/release/drag/cancel, secondary buttons, multiple pointers, snapshot replacement/refresh, timestamps, and rejected calls preserving state. [Focus checks](../../tests/input/focus_tests.cpp) cover eligibility, preorder traversal and wrapping, directional alignment/distance/exclusion/tie rules, activation equal to a pointer click, label-to-button and ancestor cancel lookup, same-tree and replacement recovery, consumed commands, programmatic focus rejection, and rejected calls preserving state. [Scroll checks](../../tests/input/scroll_tests.cpp) cover per-axis chaining with remainders, hidden scroll boxes, misses, minimal/nested/oversized reveal, rejected input, and agreement between the topmost painted background and the hit target over sampled points at three offset combinations. The core-only [pointer-menu example](../../examples/pointer-menu/main.cpp) creates a document, lays it out, generates paint, and delivers synthetic clicks and then logical navigate/activate commands, which produce the same host action requests.

Transforms, full propagation, and focus scopes/modal behavior need later fixtures. The Win32 host smoke exercises OS-message normalization, capture-loss/cancel-mode delivery, press-to-focus, key-to-command translation including repeat handling, wheel scrolling and focus reveal, GPU integration, and paint/hit agreement at fractional clip edges of a scrolled list through posted window messages. It checks the gamepad policy against scripted samples with synthetic times, then drives the menu through the same polling path with scripted samples in place of XInput. None of this is physical-device evidence.
