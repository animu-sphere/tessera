# Control a vertical ScrollView scrollbar

Use [authoring.hpp](../../include/tessera/ui/authoring.hpp) and [scrollbar.hpp](../../include/tessera/input/scrollbar.hpp). The [Settings starter](settings-starter.md#scroll-the-sidebar-and-form) is a complete host with ordinary command execution, focus, wheel input and staged replacement.

## Compose from accepted geometry

Place `VerticalScrollbar` beside, not inside, its ScrollView:

```cpp
namespace ui = tessera::authoring;
auto inspector = ui::Row({
    ui::ScrollView(rows, {
        .id = "inspector",
        .style = {.width = tessera::Dimension::points(0),
                  .height = tessera::Dimension::points(viewport_height),
                  .grow = 1.f}}),
    ui::VerticalScrollbar("Inspector scroll", viewport_height, scroll_geometry,
                          "inspector.scroll", {.node = {.id = "inspector-scrollbar"}})
});
```

`viewport_height` is the ScrollView's accepted padding-box height and `scroll_geometry` is its `LayoutBox::scroll`. They are observations of the one host-owned offset, not another scroll model. For the first measurement, use a zero offset with extent equal to the intended viewport height; replace those provisional metrics before publication.

Register `inspector.scroll` in the ordinary validation context, registry and command snapshot. Reserve `inspector-scrollbar-before`, `inspector-scrollbar-thumb` and `inspector-scrollbar-label`. The hidden label names the scrollbar and its current/maximum logical offset.

The track reserves 12 logical units by default; `node.style.width` can change its width. Its column direction, height, zero border/padding/gap and non-growing/non-shrinking geometry belong to the composition. Keep generated geometry styles intact. The thumb's size is `min(viewport, max(minimum_thumb, viewport * viewport / extent))`; `minimum_thumb` defaults to 18. Its travel maps the accepted offset over `[0, extent - viewport]`. No overflow hides the thumb while retaining the track's width. A zero viewport or zero travel is noninteractive. Invalid metrics, IDs or minimum sizes produce source-aware diagnostics.

`ScrollbarProps.hovered` and `.focused` select distinct default thumb colors. Derive these from ordinary clipped hit testing and refreshed host focus, and recompose when they change. Disabled state also belongs to the host; propagate application/command eligibility consistently to the scrollbar and content.

## Route a controlled command

Create one host-owned dispatcher:

```cpp
tessera::ScrollbarDispatcher scrollbar("inspector-scrollbar", "inspector", 16);
auto result = scrollbar.dispatch(hit, event, focused);
```

Check and report diagnostics before accessing the result. A handled primary press returns `focus` and captures its pointer without moving the offset. Send that focus through `FocusDispatcher`. Matching moves and primary release return `update` plus the ordinary thumb `action` only when the proposed offset changes. Carry the owned proposed offset into the host command, resolve/execute the action against the current command/semantic generation, and stage the candidate before accepting it. Preserve `update.offset.x`; this control changes only Y.

Do not also send handled events to ordinary pointer/focus dispatch or execute a payload-free release. Track clicks do not page or jump. Wheel input over the ScrollView continues through `route_scroll`; focus reveal continues through `scroll_into_view`. Up/Down, corresponding logical Navigate, Home/End and Activate while the thumb is focused use the same command path; Activate proposes one positive step. The finite positive keyboard step defaults to 16.

Cancellation keeps the last accepted offset. Matching PointerCancel, logical Cancel and unmodified Escape end capture without a deferred command. Other non-pointer input ends capture before ordinary routing, except wheel input, which can publish a compatible offset-only replacement. Foreign pointers and secondary releases cannot steal capture. OS capture and capture-loss normalization are host responsibilities.

## Stage and accept together

For every candidate:

1. Lower and lay out content with requested host offsets and the previous presentation's scrollbar metrics.
2. Apply any ordinary focus-reveal policy, then read the candidate ScrollView's viewport, extent and layout-clamped offset.
3. Recompose the scrollbar from those exact metrics and prepare the final coherent layout, semantics, inspection and paint before publication. Keep the track width reserved in both passes so measuring the thumb cannot change content wrapping.
4. On acceptance, store layout's clamped offsets under stable author IDs and call `scrollbar.refresh(hit)`, including when the scrollbar is removed.

`refresh.update` observes the canonical offset and never contains an action. Compatible immutable replacements preserve capture without retaining old tree handles. Removal, hiding, disabling, full clipping, changed action, viewport, thumb size or scroll range cancel it. Offset-only wheel/reveal replacements remain compatible. Resize and content shrink/grow must retain the most recently clamped canonical offset, rather than revive an obsolete request.

`scrollbar_geometry` joins the generated track/thumb with the supplied scroll owner; mismatched viewport, thumb size or offset is an explicit diagnostic, not a stale-looking successful dispatch. Failed calls leave accepted capture unchanged. Rejected staging must retain the accepted host offset and presentation and report the failure.

## Observation and limits

The thumb has the additive `scrollbar` semantic role and an ordinary activate binding. UI/Semantic JSON v1, inspection/invariants and Replay v1/v2 preserve it; older readers reject this vocabulary. The label carries current/maximum values, but numeric accessibility range/orientation fields, separate increment/decrement actions and a native accessibility adapter are not implemented.

[Owner tests](../../tests/input/scrollbar_tests.cpp) cover logical viewports derived from 640 x 360 at scales 1, 1.25, 1.5 and 2, proportional/minimum geometry, clipping/hit/CPU pixels, wheel/reveal synchronization, canonical clamping, capture lifecycle, focus and semantic/replay observations. Replay records ordinary geometry and action requests, not an arbitrary host command payload or automatic application recomposition. The Settings smoke verifies the actual controlled drag/key command and retained application data. Horizontal scrolling controls, track paging, advanced theming and dedicated native/GPU scrollbar validation remain separate work.
