# VirtualListView and VirtualTreeView

The [virtual list example](../../examples/virtual-list/main.cpp) browses a 50,000-row event log and a 20,400-item scene outline through public `tessera::core` APIs while realizing about a hundred nodes per frame. It is a scripted/headless consumer, not a native window.

Use these collections when the rows of a [ListView or TreeView](tree-list.md) would exceed the document node limit or make every update proportional to the whole data set. Items, generated IDs, roles, selection, disclosure, commands and keyboard/gamepad navigation are the same; only the realized rows differ.

## Compose a virtual collection

```cpp
namespace ui = tessera::authoring;
ui::VirtualWindow window{.item_extent = 20, .viewport = pane.viewport, .offset = pane.offset, .overscan = 2};
if (pane.focus) window.pinned.push_back(*pane.focus);
auto log = ui::VirtualListView("Event log", selected_event, events,
    {.node = {.id = "log", .style = {.height = Dimension::points(160)}}, .window = window});
auto scene = ui::VirtualTreeView("Scene", selected_object, outline,
    {.node = {.id = "scene", .style = {.height = Dimension::points(200)}}, .window = scene_window});
```

Every row is an `<id>-item-<key>` Row of exactly `item_extent` logical units, clipped to that height. Unrealized rows become the top margin of the next realized row and the bottom margin of the last one, so the scroll extent equals every logical row stacked and a realized row sits where it would in the full list. The collection is named by its display-none `<id>-title` and its padding and gap are zero; style rows through classes on the surrounding layout, not padding on the collection.

The window realizes, in logical order:

- the rows intersecting the viewport, after clamping the offset as layout does, plus `overscan` rows beyond each edge (at most 64);
- the first and last enabled rows, so Home/End reach the logical ends;
- each visible `pinned` key (at most 64) with its nearest enabled row before and after, so arrows and Tab reach the logical neighbors;
- each pinned key under a collapsed branch as a display-none row after that branch, with its hidden ancestors, so focus recovers to the logical parent as in TreeView;
- every tree ancestor of a realized row, at its own logical position, so `tree_parent`, disabled ancestry and Left reach the parent.

At most 1024 rows are realized; a larger window fails lowering with `collection_limit`. The item count times `item_extent` must stay within 2^24 logical units (`virtual_extent`), keeping float layout positions exact for integral extents. An invalid extent, viewport, offset, overscan or pin count fails with `virtual_window`. Keys, selection and branch rules are validated over every logical item, including collapsed descendants, with the [TreeView diagnostics](tree-list.md#compose-controlled-collections).

## Drive the window from the host

Tessera keeps no window state. After each accepted layout, the host:

1. Stores the collection's applied scroll offset and its padding-box height (border box less border) as `offset` and `viewport`.
2. Pins the focused row's key, or none.
3. Reveals **changed** focus with `scroll_into_view` and stores the offset, as for any scroll box.
4. Recomposes when any of these inputs changed and repeats until they settle. The example settles in at most three passes.

Wheel input routes with `route_scroll` across unrealized rows, because the margins carry the full extent. Before the first layout, pass the collection's styled height as an estimate of `viewport`.

To operate on a row that may be unrealized, such as a find or go-to command, set the offset to `virtual_reveal(keys, window, key)`, recompose, then focus or act on the realized row. `virtual_keys` returns the visible logical keys in order: every list key, or the tree keys whose ancestors are all expanded. Before removing the focused row, pin the row that should receive focus: ordinary neighbor recovery then lands on it as in a fully realized list.

Neighbor and Home/End realization use item and collection `disabled`. Mark rows whose commands are unavailable as disabled, so the realized neighbor is the eligible one.

## Keep content in place across data changes

Retain the scroll position as an anchor when data is replaced, for example when history is prepended to a log:

```cpp
const auto anchor = ui::virtual_anchor(ui::virtual_keys(events), 20, pane.offset);
events.insert(events.begin(), history.begin(), history.end());
if (anchor)
    if (const auto offset = ui::virtual_offset(ui::virtual_keys(events), 20, *anchor)) pane.offset = *offset;
```

The anchor is the key of the row at the viewport's leading edge plus the offset within it. Restoring it keeps that row at the same place; a removed anchor key returns `nullopt`, leaving the host to choose a fallback.

## Observe and operate

Only realized rows are semantic entries. The collection reports `item_count`, the number of visible logical rows, and each displayed row its 1-based `item_position`. Both reach [semantics and owned snapshots](../architecture/semantics.md#implemented-prototype-projection), inspection, Semantic JSON v1 and Replay observations, and round trip through UI JSON v1. An agent or test can tell how many rows exist and where each realized row sits, then use a host reveal operation to reach the rest. Semantic actions, replay steps and inspection targets address realized rows only.

## Build and run

```powershell
cmake -S . -B build
cmake --build build --config Debug --target tessera_virtual_collection_tests tessera_virtual_list --parallel 4
ctest --test-dir build -C Debug -R '^(virtual_collection|virtual_list)$' --output-on-failure --no-tests=error
& .\build\Debug\tessera_virtual_list.exe
```

The [owner fixture](../../tests/input/virtual_collection_tests.cpp) covers the bounded realized set beyond the ordinary node limit, window arithmetic, anchors and reveal, CPU pixel and geometry parity with a fully realized collection at several offsets, navigation through pinned neighbors and disabled runs, Home/End, offscreen focus, removal, tree ancestors, collapse recovery, codecs, replay and rejection. The example checks pointer, keyboard, wheel, prepend anchoring, collapse, semantic actions and resize on a real host loop.

Measured or estimated row extents, rows of different heights, recycled row instances, a follow-end policy for growing logs, horizontal or two-dimensional virtualization (grids and tables), drag reordering with `CollectionDragDispatcher`, automatic scrolling while dragging, lazy loading, multiple selection, native accessibility and physical-input validation are not included. [Support](../reference/support-matrix.md) owns validation scope.
