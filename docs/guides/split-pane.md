# Resize two panes through public C++ APIs

Use [authoring.hpp](../../include/tessera/ui/authoring.hpp) and [split_pane.hpp](../../include/tessera/input/split_pane.hpp) with the build prerequisites from [Getting Started](getting-started.md). The complete [Settings starter](settings-starter.md#resize-the-sidebar) demonstrates command execution, staged replacement and optional Windows capture.

## Compose

```cpp
namespace ui = tessera::authoring;
ui::Element Inspector(float sidebar_size) {
    return ui::HorizontalSplitPane(
        ui::ScrollView({ui::Text("Categories")}, {.id = "categories"}),
        ui::ScrollView({ui::Text("Editor")}, {.id = "editor"}),
        sidebar_size, "inspector.resize",
        {.node = {.id = "inspector", .style = {.height = tessera::Dimension::points(240)}},
         .limits = {96, 240, 8}, .label = "Resize sidebar"});
}
```

Register `inspector.resize` in the ordinary validation context, command registry and snapshot eligibility. Start the host-owned `sidebar_size` at 140 logical units. Horizontal means left/right; `VerticalSplitPane` has the same signature and means top/bottom. Nest either component as a pane to compose more regions.

The first size is a controlled logical border-box size, not a percentage. Minima and the handle size participate in existing Flex allocation. Constrain the SplitPane through ordinary parent/root dimensions; leave pane cross dimensions automatic when they should stretch. Root padding reduces available travel. If both minima cannot fit, they win and overflowing content is clipped; adjustment locks at the first minimum. The [authoring contract](../architecture/ui-model.md#implemented-c-form-authoring) owns validation and generated-part styling.

Reserve `inspector-first`, `inspector-handle`, `inspector-handle-label` and `inspector-second`. The hidden label names the separator and its current logical size. No new widget node kind, numeric accessibility range or native accessibility adapter is introduced.

## Route and accept

Create one host-owned `tessera::SplitPaneDispatcher splitter("inspector", 8)`. For each normalized event, pass the current coherent `HitTestInput` and refreshed eligible focus:

```cpp
auto routed = splitter.dispatch(hit, event, focused);
```

Check the `Result` and report its diagnostics before using its value. A handled press supplies `focus`, which the host sends to its ordinary `FocusDispatcher`. A changed drag/key value supplies `size` and the handle's `action`. Carry that owned size to the ordinary host resize command; resolve/execute the action against the current semantic input, command generation and execution sequence. Stage all presentation steps before accepting the new size. Do not also route a handled event to pointer/focus dispatch, and do not execute a payload-free pointer release as another resize command.

At every accepted replacement, including handle removal:

```cpp
auto settled = splitter.refresh(hit);
```

Again report diagnostics. Store the returned resolved `size` when present before composing the next candidate. If layout clamps a requested size, recompose the label at that resolved value before publication, as the starter does. This retains the logical width after shrink/grow rather than restoring an obsolete request. On rejected staging, retain accepted size/presentation and report the failure; input ownership and publication stay with the host.

The [input contract](../architecture/input.md#implemented-splitpane-adjustment) owns press/move/release/cancel and compatible capture retention. Axis arrows and logical gamepad Navigate adjust by the step; Home/End reach limits and Activate proposes one positive step. A primary press preserves the grab offset, avoiding a jump. Cancellation retains the last accepted value.

Native hosts supply OS pointer capture, `PointerCancel` on capture loss and a resize cursor from `split_pane_geometry`/ordinary hit testing. Client pixels must be normalized into the same logical coordinates as layout. Core owns neither a window nor an OS cursor. [Owner tests](../../tests/input/split_pane_tests.cpp) and the Settings consumer cover both orientations, nested/padded/scrolled geometry, clamping, cancellation and retained independent application state.
