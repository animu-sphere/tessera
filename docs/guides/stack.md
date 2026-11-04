# Overlay a viewport toolbar with Stack

`authoring::Stack` places ordinary children over the same parent content box. It is an in-parent overlay, not a Portal, popup anchor or modal layer. The [layout contract](../architecture/layout.md#implemented-stack-placement) owns sizing/placement and the [form API](../architecture/ui-model.md#implemented-c-form-authoring) owns lowering and lifetime.

## Compose the layers

```cpp
namespace ui = tessera::authoring;
using namespace tessera;

auto viewport = ui::Stack({
    ui::Column({}, {.id = "scene",
        .style = {.background = Color{0.12f, 0.18f, 0.25f, 1}}}),
    ui::Row({
        ui::Button("Zoom in", "view.zoom"),
        ui::Button("Reset", "view.reset")},
        {.id = "toolbar", .style = {.margin = Edges{8, 8, 8, 8},
            .gap = 4.f, .stack_horizontal = Align::end,
            .stack_vertical = Align::start}})},
    {.id = "viewport", .style = {.overflow = Overflow::clip}});
```

Children default to stretching on automatic axes. Give the toolbar `stack_horizontal: end` and `stack_vertical: start` to keep its natural size at the top/right content edge, inset by its own margins. Use `center` on either axis for centered content; explicit width/height and min/max limits remain ordinary style declarations. The root fills the host-supplied logical viewport unless constrained.

Declare background content first and the toolbar last. Later sibling subtrees paint on top and receive hits first. A transparent overlay still participates in hit testing; keep its rectangle content-sized rather than covering the viewport accidentally. `visibility: hidden` retains geometry and suppresses only that node's paint/hits under the existing local-visibility rule; `display: none` removes the entire subtree. An explicitly non-displayed Stack honors that declaration. No layer escapes clipping or changes focus scope.

The toolbar Row's `gap` arranges its buttons normally. A Stack's own gap/direction/justify/align and its children's grow/shrink do not distribute its overlapping layers. Horizontal/vertical Stack placement is independent of each child's internal Flex alignment.

## Run the application operation

After the [core configure procedure](development.md#core-build-workflow), with examples enabled:

```powershell
cmake --build build --config Debug --target tessera_viewport_toolbar --parallel 4 -- /nodeReuse:false
& .\build\Debug\tessera_viewport_toolbar.exe
ctest --test-dir build -C Debug -R '^viewport_toolbar$' --output-on-failure --no-tests=error
```

The [window-free example](../../examples/viewport-toolbar/main.cpp) composes a selectable scene, a bottom-center reactive zoom caption and a top/end toolbar through public C++ APIs. Its synthetic normalized input:

1. Clicks Zoom in above the scene, without selecting the scene underneath.
2. Uses sequential keyboard focus and activation to zoom again.
3. Uses directional gamepad navigation and activation to reset.
4. Resizes the logical viewport and verifies the toolbar's edge alignment and the ordinary reactive caption.
5. Clicks uncovered scene content and verifies it remains interactive.

The host registers action names, owns application state and executes requests through `CommandSnapshot`/`CommandExecutor`. Layout replacement refreshes pointer state and restores focus by author ID. This standalone entry point is a deterministic application smoke, not a native window or physical-input session. The optional [latest-UI Vulkan regression](development.md#latest-ui-gpu-regression) observes this same operation's initial, zoomed and resized paint. Geometry, selected CPU pixels, clip/hit agreement and Replay preservation are covered by the [owner checks](../architecture/layout.md#implemented-stack-placement); actual tested configurations belong to [support](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md#stack-evidence--2026-11-04).
