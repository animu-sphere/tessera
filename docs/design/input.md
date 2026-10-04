# Input, focus, and navigation

Status: Normalized event types and the action-request contract implemented. No hit testing, dispatch, or focus runtime is implemented.

## Host boundary

The host polls platform devices and translates their data into normalized Tessera events. Tessera does not poll OS input APIs.

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

Hit testing consumes layout geometry, effective transforms/clips, paint order, visibility, and pointer policy. Proposed rule: select the topmost eligible node under the pointer, with deterministic behavior at box edges. Specify whether borders and rounded corners affect the hit region. Scrolled content must use the same effective coordinates for paint and targeting.

The initial propagation model can be target then bubble. Capture can be introduced later if required. Keep propagation cancellation, default-action prevention, and handled state distinct and specify their exact semantics before implementation.

Proposed pointer lifecycle: a press records its target; activation requires a compatible release according to a declared capture/click policy. Specify dragging outside, pointer cancellation, lost window focus, disabling/removing the target, and multiple pointers. Callback mutation must follow the frame/traversal rules in [architecture](architecture.md).

### Action registration and lifetime

Host action registration is explicit. The foundation stores `activate`/`cancel` action names and rejects names absent from the caller's `ValidationContext.actions`; see [UI model](ui-model.md).

Implemented contract: Tessera never stores host callbacks. Dispatch will produce `ActionRequest` values (binding name, action name, target `NodeHandle`) and return them to the host. The host runs its own action implementations after dispatch returns, and applies resulting mutations at its next update point, so no callback runs during traversal and callback lifetime is entirely host-owned. A request's target handle is valid only while the producing tree snapshot is alive; after a tree is replaced, `UiTree::get` rejects stale targets, so no separate cleanup is needed when nodes disappear. The dispatcher itself is planned.

## Interaction state

Input owns hover, active, focus, and disabled behavior. Styling reads these values through [pseudo states](styling.md). Changes must schedule style/layout/paint updates as needed. Disabled nodes should not activate; define focus and descendant behavior consistently rather than treating disabled as only a visual property.

## Focus and navigation

Keyboard and gamepad operation are architectural requirements even though pointer interaction is delivered first. Model focusable nodes, traversal order, directional navigation, activation, and cancel/back from the beginning.

Proposed baseline: tree-ordered sequential traversal and explicit overrides; directional movement uses deterministic geometry-based selection with a tie-break rule. Finalize eligible-node filtering, explicit neighbors, focus scopes, wrapping, modal behavior, and focus recovery when the focused node is hidden, disabled, or removed.

Map gamepad input into logical navigation/activation/cancel actions at the host boundary. Dead zones and repeat policy must have an explicit owner. Do not put device-specific polling in buttons.

The first public candidate includes pointer hit testing. Keyboard/gamepad focus and navigation are required for the next menu milestone; this staging does not remove them from the design.

## Accessibility metadata

The node model should support roles, accessible names, values/states, and relationships independent of graphics. Exact schema fields remain open. Native accessibility adapters can consume this metadata later. Metadata availability is separate from operating-system assistive technology integration, and neither is implemented today.

## Verification

Test overlapping nodes, edge coordinates, clips/transforms, scrolling, pointer cancellation, click eligibility, disabled behavior, propagation, action cleanup, focus traversal/recovery, directional ties, and activation/cancel. Use synthetic normalized events without OS devices. A small interactive menu smoke demonstrates host translation and end-to-end dispatch.
