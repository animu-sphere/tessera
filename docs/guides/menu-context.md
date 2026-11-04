# Menu and ContextMenu

The [asset-actions example](../../examples/menu-context/main.cpp) implements single-level command menus with public `tessera::core` APIs. It is a deterministic scripted/headless example with placeholder text, not a native desktop window.

## Compose controlled menus

```cpp
namespace ui = tessera::authoring;
const std::vector<ui::MenuItem> items{
    {"inspect", "Inspect", "asset.inspect"},
    {"delete", "Delete", "asset.delete", true},
    {"rename", "Rename", "asset.rename"}
};
auto form = ui::Column({
    ui::Button("Actions", "actions.open", {.node = {.id = "actions"}}),
    ui::Button("Asset", "asset.select", {.node = {.id = "asset"}}),
    ui::Menu("Asset actions", "actions", items,
        {.node = {.id = "actions-menu"}, .open = actions_open}),
    ui::ContextMenu("Asset actions", "asset", context_position, items,
        {.node = {.id = "asset-menu"}, .open = context_open})
});
```

Register every action and command as usual, including disabled commands; project command eligibility into each item's `disabled` flag. `MenuItem` shares the `ListItem` key/label/action/disabled record, but commands have no selected/expanded state. The menu label and item labels must be nonempty; keys must be unique and nonempty. The host owns open state and context position. Keep at most one menu open in this operation.

Both menus require a stable `node.id` and are direct root children after ordinary content. Generated IDs are `<id>-title`, `<id>-item-<key>` and `<id>-label-<key>`. Props supply ordinary sizes, styles, classes and source. Menus are scrollable columns by default; constrain their size if needed.

`Menu` uses the ordinary [Popup side/gap and flip/shift rules](popup-tooltip.md). `ContextMenu` instead places its leading corner at the requested **root-logical** point and shifts it inside the root-content/viewport boundary on resize. Its `target` remains the visibility/lifetime anchor, not a strict reference. Missing, hidden, zero-area or fully clipped targets suppress the subtree. Scrolling a still-visible target retains the requested context point; this is not an anchor-relative offset. Root clips and minimum-size floors still apply.

## Wire the host

Include [popup.hpp](../../include/tessera/input/popup.hpp) and retain a `MenuDispatcher` for each menu's stable ID, plus ordinary `PointerDispatcher`/`FocusDispatcher` instances.

1. Lower controlled state, resolve styles and layout, refresh pointer/focus, then refresh both menu dispatchers on every accepted view, including closed ones. Apply their optional `focus` through `FocusDispatcher`. Opening requests the first eligible item once. A surviving eligible focusable anchor receives focus on closure; otherwise use ordinary focus recovery. Apply `dismiss` to host state and publish the closed view before further routing.
2. Dispatch menus before ordinary input, passing current-snapshot focus. For this mutually exclusive pair, offer input to the context menu first, then the ordinary menu if neither consumed nor dismissed it.
3. For ContextMenu, a secondary `PointerDown` whose topmost eligible hit is inside its target returns `open_at` and `consumed`. Nonrepeating Shift+F10 with focus inside that target returns the visible target's lower-left point. Apply `open_at`, close the other menu, publish, and refresh. Neither trigger activates the underlying asset. A gamepad may open the ordinary Menu through its trigger's normal `Activate` command; no new controller event format is introduced.
4. Route inside primary press/release, logical `Navigate` and `Activate` through ordinary dispatch and command execution. Pointer hover requests the eligible menu item's focus. Up/Down follow item order, skip disabled entries and wrap; Home/End reach the first/last item; Left/Right do nothing in a single-level menu. The host translates other keyboard/gamepad input as usual.
5. After an accepted item command, close in host state, publish and refresh. A rejected command must not be presented as a completed choice. Semantic invocation uses the same item action and host closure policy.
6. Outside presses, Cancel and unmodified Escape return consumed dismissal, with eligible anchor focus. Reset/cancel ordinary pointer presses so later releases cannot click through. Tab/Shift-Tab or logical `FocusNext`/`FocusPrevious` return **unconsumed** dismissal: close, publish and refresh first, then forward that one sequential focus command in the closed view. Normalize raw Tab to the logical command once.
7. Route wheel input through `route_scroll`; store offsets and recompute layout. After keyboard/gamepad item movement, use `scroll_into_view` on changed focus and apply its updates to reveal clipped commands. Refresh after resize, scrolling, target replacement or removal.

Do not rebuild between an ordinary primary press/release pair unless intentionally cancelling it: tree replacement resets ordinary pointer state. Refresh returns new-snapshot handles; never retain an old handle across replacement. Dispatch rejects stale focus, malformed events, wrong target kinds and decreasing timestamps without advancing accepted state.

The menus are non-modal, not a general focus trap. Hosts arbitrate other overlays, external focus changes, window-focus loss and owner-free shortcuts. Menu scopes do not block unrelated semantic commands. There is no submenu, menu bar, separator/check/radio item, typeahead, touch long-press, generalized Portal, native accessibility adapter or native/device validation in this slice.

## Build and run

From the repository root with the documented [core toolchain](development.md#core-build-workflow):

```powershell
cmake -S . -B build
cmake --build build --config Debug --target tessera_menu_tests tessera_menu_context --parallel 4
ctest --test-dir build -C Debug -R '^(menu|menu_context)$' --output-on-failure
& .\build\Debug\tessera_menu_context.exe
```

[Owner checks](../../tests/input/menu_tests.cpp) cover placement/pixel/hit agreement, ordered focus/scroll reveal, input/semantic parity, controlled dismissal, disabled/modal eligibility, lifecycle/rejection and UI/Semantic JSON v1 plus Replay v1/v2. [Support](../reference/support-matrix.md) owns validation scope.
