# Popup and Tooltip

The [dedicated example](../../examples/popup-tooltip/main.cpp) implements a theme selection Popup and explanatory Tooltip using public `tessera::core` APIs. It is a deterministic scripted/headless example, not a desktop window. [UI model](../architecture/ui-model.md#implemented-c-form-authoring), [layout](../architecture/layout.md#implemented-anchored-popups) and [input](../architecture/input.md#implemented-popup-and-tooltip-interaction) own the contracts; [support](../reference/support-matrix.md#popup-and-tooltip-evidence--2026-11-04) owns validation limits.

## Compose at the root

```cpp
namespace ui = tessera::authoring;
auto form = ui::Column({
    ui::ScrollView({
        ui::Button("Theme", "theme.open", {.node = {.id = "theme"}})
    }, {.style = {.height = tessera::Dimension::points(72)}}),
    ui::Popup("theme", {
        ui::Button("Dark", "theme.dark", {.node = {.id = "dark"}}),
        ui::Button("Light", "theme.light", {.node = {.id = "light"}})
    }, {.node = {.id = "theme-popup"}, .open = popup_open}),
    ui::Tooltip("theme", "Choose the editor color theme",
        {.node = {.id = "theme-help"}, .open = tooltip_visible})
}, {.style = {.overflow = tessera::Overflow::clip}});
```

Declare registered host actions as usual. Put all popup/tooltip nodes directly under the document root, after ordinary content, even when the anchor is nested. This explicit composition avoids the anchor's scroll clip without Portal or changed ownership. Give the popup and trigger stable IDs. Content keeps ordinary identities and semantic buttons/text; no menu/tooltip accessibility role or description relationship is inferred.

`PopupProps` controls `open`, preferred `side` and `gap`; node props supply sizes, styles, classes and source. Auto dimensions use content measurement, including width-wrapped text. Placement flips when the preferred side lacks space and the opposite has more, then shifts into the root-content/viewport boundary. Minima can exceed that boundary; use suitable limits and clip/scroll policies. Overflow defaults to clip; a tall choice list can explicitly request scroll and use the ordinary host scroll routing. `Tooltip` contains nonfocusable text and is pointer-transparent without being semantically disabled.

## Wire the host

1. Retain `PopupDispatcher("theme-popup")`, `TooltipDispatcher("theme")`, ordinary pointer/focus dispatchers and controlled application values.
2. Resolve a coherent tree/styles/layout view. Refresh pointer and focus, then refresh Popup; apply its optional focus target through `FocusDispatcher`. Refresh Tooltip with that current focus and publish its `visible` value, typically gated off while the Popup is open.
3. Before ordinary event routing, dispatch Popup. Apply `dismiss` to the controlled open value. On `consumed`, do not route the event again and reset/cancel ordinary pointer presses to prevent click-through on release.
4. Route inside pointer events and keyboard/gamepad navigation/activation through existing dispatchers, action resolution and command execution. An open command changes only host state; a selection command updates the selected value and closes the Popup.
5. Dispatch Tooltip with normalized pointer events and current focus; refresh after focus changes. Help appears immediately on hover or focus. Cancel/Escape and presses suppress it until both hover and focus leave. Optional delay timers belong to the host.
6. Recompose and refresh after accepted updates, scroll offsets or viewport changes. A hidden, unmounted or fully clipped anchor removes popup geometry; honor Popup's dismissal request. Focus recovers only to a surviving eligible trigger.

Ordinary pointer dispatch cancels presses when a new `UiTree` identity replaces its snapshot. The scripted example stages transient hover/help changes across a press/release pair rather than rebuilding between them; a native host must preserve its own accepted press/capture policy or intentionally cancel on replacement. Do not retain runtime handles across tree replacement: use stable IDs or each dispatcher's recovery result.

Refresh closed views too, so selection, external closure and reopening have distinct transitions. Tooltip is noninteractive and does not consume Cancel itself; the host arbitrates overlapping transient UI. Popup is non-modal: Tab/directional movement uses ordinary global focus candidates, with no trap or newly introduced focus scope.

## Build and run

From the repository root with the documented [core toolchain](development.md#core-build-workflow):

```powershell
cmake -S . -B build
cmake --build build --config Debug --target tessera_popup_tooltip tessera_popup_tests --parallel 4
ctest --test-dir build -C Debug -R '^(popup|popup_tooltip)$' --output-on-failure
& .\build\Debug\tessera_popup_tooltip.exe
```

The example checks pointer selection, keyboard opening, gamepad navigation/selection/Cancel, outside-click consumption, scroll/resize placement and anchor removal. The owner fixture additionally checks four sides, flip/shift, CPU background pixels against hit order, Tooltip transparency, suppression, stale input and serialized property validation.

No native window/device, real-font capture, automatic Tooltip timer, modal, Portal, nested popup anchoring or controller-specific replay event format is included. Dedicated [Menu/ContextMenu compositions](menu-context.md) add menu roles, ordered navigation and context triggers separately; ordinary Popup buttons do not acquire that policy implicitly.
