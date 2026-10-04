# Input, focus, and navigation

Status: Draft design. No event runtime is implemented.

## Host boundary

The host polls platform devices and translates their data into normalized Tessera events. Tessera does not poll OS input APIs. Candidate events:

```text
PointerMove / PointerDown / PointerUp / Scroll
KeyDown / KeyUp / TextInput
GamepadNavigate / FocusNext / FocusPrevious
Activate / Cancel
```

Define pointer identity, button/key vocabulary, coordinate units, modifiers, repeat handling, timestamps, scroll units, and text encoding before exposing these types. Convert host coordinates into the same logical space used by [layout](layout.md).

Key events and text input are separate operations. The presence of `TextInput` does not imply a text editor or full IME composition support; composition, clipboard, and editable-text semantics need a later contract.

## Hit testing and propagation

Hit testing consumes layout geometry, effective transforms/clips, paint order, visibility, and pointer policy. Proposed rule: select the topmost eligible node under the pointer, with deterministic behavior at box edges. Specify whether borders and rounded corners affect the hit region. Scrolled content must use the same effective coordinates for paint and targeting.

The initial propagation model can be target then bubble. Capture can be introduced later if required. Keep propagation cancellation, default-action prevention, and handled state distinct and specify their exact semantics before implementation.

Proposed pointer lifecycle: a press records its target; activation requires a compatible release according to a declared capture/click policy. Specify dragging outside, pointer cancellation, lost window focus, disabling/removing the target, and multiple pointers. Callback mutation must follow the frame/traversal rules in [architecture](architecture.md).

Host action registration is explicit. Serialized event bindings name registered actions; they do not embed executable code. Binding lifetime, unknown-action diagnostics, and cleanup when nodes disappear are open decisions shared with the [UI model](ui-model.md).

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
