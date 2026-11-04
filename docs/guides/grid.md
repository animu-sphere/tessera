# Align Inspector properties with Grid

`authoring::Grid` places ordinary children in explicit row-major cells within its content box. It shares a content-sized label column across Inspector rows: independent Rows cannot derive that common width from unequal labels without application-side measurement or duplicated widths. The [layout contract](../architecture/layout.md#implemented-grid-placement) owns sizing and placement; [form authoring](../architecture/ui-model.md#implemented-c-form-authoring) owns lowering and lifetime.

## Compose shared columns

```cpp
namespace ui = tessera::authoring;
using namespace tessera;

auto inspector = ui::Grid({
    ui::Text("Display name"),
    ui::TextInput("Name", name_value, "inspector.name", {.node = {.id = "name"}}),
    ui::Text("Kind"),
    ui::TextInput("Group", category_value, "inspector.category", {.node = {.id = "category"}})},
    {.id = "inspector", .style = {.padding = Edges{8, 8, 8, 8},
        .grid_columns = std::vector<GridTrack>{GridTrack{.maximum = 80}, GridTrack::fraction(1, 32)},
        .grid_rows = std::vector<GridTrack>{GridTrack{}, GridTrack{}},
        .column_gap = 8.f, .row_gap = 8.f}});
```

`name_value` and `category_value` are host/session-owned `TextEditState` observations. Register the two ordinary action names and route committed text through their commands, as in the [example](../../examples/grid-inspector/main.cpp). Grid adds no editing policy or application state.

The first column uses the widest label, capped at 80 logical units; the second receives remaining width with a 32-unit minimum. Shortening the longest label resizes the shared column for every row rather than requiring copied Row widths. For a fixed label column instead, use `GridTrack::points(80)`. Each automatic row uses the tallest child after measuring at the assigned column width, so labels and input content wrap without moving the next row over them. At widths below the content/fixed columns plus flexible minima, gaps and padding, the layout overflows rather than silently shrinking; choose ordinary `overflow: clip` or `scroll` to define the application's policy.

For content-sized tracks use `GridTrack{}`. For weighted flexible space use `GridTrack::fraction(weight, minimum, maximum)`; flexible tracks share positive remaining space above their minima, redistributing after a maximum is reached. This is a bounded Grid contract, not CSS Grid conformance.

Each child can set `grid_horizontal` and `grid_vertical` to `start`, `center`, `end` or `stretch` (default). Fixed child dimensions remain fixed; automatic stretch respects margins, min/max limits and border/padding floors. The parent Grid's Flex direction/alignment/gap controls do not place cells; use `column_gap` and `row_gap`.

Declare both axes, at most 256 tracks each. Children occupy cells in declared order, skipping `display: none`; hidden children retain their cells. Extra children fail with `grid_capacity` instead of creating tracks. There is no spanning, explicit child cell index, implicit placement, subgrid or cell clipping; ordinary ancestor clipping, scroll offsets, paint/hit order and focus traversal apply.

## Run the application operation

After the [core configure procedure](development.md#core-build-workflow), with examples enabled:

```powershell
cmake --build build --config Debug --target tessera_grid_inspector --parallel 4 -- /nodeReuse:false
& .\build\Debug\tessera_grid_inspector.exe
ctest --test-dir build -C Debug -R '^grid_inspector$' --output-on-failure --no-tests=error
```

The window-free [Inspector](../../examples/grid-inspector/main.cpp) verifies:

1. Two properties share label and editable-value columns at a 320-unit viewport width.
2. Keyboard focus and normalized committed text edit the display name without changing Category.
3. Resizing to 168 units preserves edits/focus, increases wrapped row height and keeps input content within its field bounds.
4. Pointer activation survives three fresh immutable presentations while held, emits one current-snapshot command, and directional gamepad navigation reaches the same ordinary command owners.
5. An injected measurement rejection retains accepted fields and presentation with diagnostics; a fresh edit succeeds without the rejected text.
6. Growing back to 320 units keeps Category focus and both accepted edits.

The host opts into `PointerSnapshotPolicy::compatible_author_ids` and refreshes pointer/focus dispatchers after every accepted presentation; stable field IDs and unchanged activation bindings preserve an ordinary click while updates continue. The [pointer contract](../architecture/input.md#implemented-pointer-dispatch) owns eligibility, cancellation and lifetime rules.

The host stages session observations, style resolution and layout before accepting an edit; a returned layout failure restores the observation and retains its prior view. Allocation failures propagate; this is not generalized exception rollback. The fixture uses placeholder text metrics and no native window, physical device, real font, IME or persistence. It does not implement caret drawing or hit-to-caret placement. Selective CPU/clip/hit and Replay coverage belongs to the [owner checks](../../tests/layout/grid_tests.cpp) and [codec checks](../../tests/replay/replay_serialization_tests.cpp); tested configurations are recorded in [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#grid-evidence--2026-11-04).
