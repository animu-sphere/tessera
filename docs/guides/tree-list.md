# TreeView and ListView

The [browser example](../../examples/tree-list/main.cpp) connects folder expansion, single selection, a dependent asset list and detail text through public `tessera::core` APIs. It is a scripted/headless consumer, not a native window.

## Compose controlled collections

```cpp
namespace ui = tessera::authoring;
auto folders = ui::TreeView("Folders", "models", {
    {"workspace", "Workspace", "folder.all", "workspace.toggle", expanded, false, {
        {"models", "Models", "folder.models"},
        {"materials", "Materials", "folder.materials"}}}
}, {.node = {.id = "folders", .style = {.height = Dimension::points(120)}}});

auto assets = ui::ListView("Assets", selected_asset, {
    {"cube", "Cube mesh", "asset.cube"},
    {"sphere", "Sphere mesh", "asset.sphere"}
}, {.node = {.id = "assets", .style = {.height = Dimension::points(100)}}});
```

Register every action in the validation context and command registry. The host owns the selected key, expanded branch flags, data and dependent details. Activation requests selection; disclosure requests the branch's toggle command. Neither lowering nor navigation mutates application state. Collapsing a branch retains a selected descendant. When replacing data, explicitly retain a surviving selected key, choose another or use `std::nullopt`; an unknown key fails lowering rather than silently clearing.

Both collections require a stable nonempty ID and label. Keys must be nonempty and unique across the entire collection, including collapsed descendants. A branch requires `toggle_action`; a leaf cannot supply it or be expanded. `CollectionProps::disabled` disables the collection; a disabled tree item disables its logical descendants. Empty collections with no selection are valid. Invalid keys, state and document/depth limits return diagnostics through `Session::lower`.

Generated IDs are `<id>-title`, `<id>-item-<key>`, `<id>-label-<key>` and, for branches, `<id>-toggle-<key>`. Items are ordinary flat Row Boxes in a scrollable Column, with indentation and separate `*`/`+`/`-` text markers. Label references keep markers out of semantic names. A typed `tree_parent` reference preserves the logical hierarchy independently of row geometry; selected/expanded state reaches [semantics and owned snapshots](../architecture/semantics.md#implemented-prototype-projection), inspection and replay. No widget node kind or renderer dependency is introduced.

## Wire input and scrolling

- Keep ordinary `PointerDispatcher` and `FocusDispatcher`. Click the row/label to select; click its disclosure to toggle. On disclosure press, focus its item row, not the nonfocusable disclosure button. Preserve the accepted press snapshot until release.
- Translate keyboard arrows and gamepad directions to `Navigate`; Enter/Space or confirmation to `Activate`. Up/Down use eligible item preorder within the collection and stop at its ends. Movement changes focus, not selection. Tree Right expands a closed branch or enters its first eligible child; Left collapses an open branch or moves to its parent. List Left/Right do nothing.
- Unmodified `KeyDown{Key::home}`/`KeyDown{Key::end}` reach the first/last eligible collection item. Outside an item they do nothing. Ordinary Tab/Shift-Tab still traverse individual eligible rows and other controls; this is not a one-Tab-stop/roving-tabindex composite. Disclosure buttons do not add Tab stops.
- Refresh focus after publishing a replacement. A collapsed/disabled focused item recovers to the nearest surviving eligible logical ancestor before the ordinary neighbor fallback. Removing a key uses ordinary author-ID/neighbor recovery.
- Constrain the collection's height/width through node styles. Retain offsets by collection ID, route wheel input with `route_scroll` and reveal **changed** focus with `scroll_into_view`, then recompute layout. Clamp stored offsets from the accepted layout; do not let unchanged focus undo intentional wheel scrolling.
- Execute returned requests with `CommandSnapshot::resolve_action` and `CommandExecutor`, then recompose. `apply_eligibility` considers activation and the applicable expansion command; disabled state is per item, not per binding, so an unavailable branch operation disables that item and its logical descendants. Never reuse handles after replacement.

Replay v1/v2 retain collection roles, relationships and state and execute normalized navigation/semantic actions. As before, replay does not record raw key events, including Home/End; use a resolved focus step for those host operations. Controlled expansion/selection changes require explicit host-built replacement views.

## Drag items to reorder

Include [collection_drag.hpp](../../include/tessera/input/collection_drag.hpp) and retain a `CollectionDragDispatcher` constructed with the collection's ID to let users drag rows to a new position. The host owns the order; the dispatcher only reports where a row would land.

1. Dispatch it before ordinary pointer routing and refresh it at every accepted view. A press on a row is not consumed, so selection by click and focus on press work as before. A press that travels 4 logical units (`CollectionDragOptions::threshold`) starts a drag; on `started`, reset the ordinary `PointerDispatcher` so the release selects nothing. Reset the dispatcher instead when the source cannot move.
2. While dragging, each result carries `source` and `target`: a row key with `before` or `after`. Validate it against your own structure, treating positions that leave the order unchanged as no target, and show feedback with ordinary styles. The Inspector dims the source and draws a two-unit inset band on the target row's top or bottom edge.
3. On `drop`, execute your own move command. `canceled` and `ended` clear the feedback. Escape, `Cancel` and `PointerCancel` cancel; Tab and other keys cancel and continue to ordinary routing.

`collection_drop_target(input, id, position)` computes the same insertion point without a dispatcher. Disabled rows remain targets, while disabled rows and disabled collections cannot be dragged. In a TreeView, `after` an expanded branch and `before` its first child are distinct targets; map them to your hierarchy. Offer a keyboard equivalent, such as the Inspector's Alt+Up/Down.

## Build and run

```powershell
cmake -S . -B build
cmake --build build --config Debug --target tessera_collection_tests tessera_tree_list --parallel 4
ctest --test-dir build -C Debug -R '^(collection|tree_list)$' --output-on-failure --no-tests=error
& .\build\Debug\tessera_tree_list.exe
```

The [drag fixture](../../tests/input/collection_drag_tests.cpp) covers insertion points, the threshold, click pass-through, cancellation, replacement and rejection. The [owner fixture](../../tests/input/collection_tests.cpp) covers logical hierarchy, state/actions, disabled ancestry, navigation/recovery, scroll, modal exclusion, rejection and UI/Semantic JSON and Replay v1/v2. The consumer verifies pointer/keyboard/gamepad command parity, dependent list/details, retained hidden selection, resize and focus reveal.

Multiple selection, virtualization, lazy loading, filtering, type-ahead, dragging between collections or into other targets, automatic scrolling while dragging, horizontal trees, automatic selection-on-focus, native accessibility and physical-input validation are not included. The optional [latest-UI Vulkan regression](development.md#latest-ui-gpu-regression) captures expansion, selection, collapse, scroll and resize through this same example. [Support](../reference/support-matrix.md) owns validation scope.
