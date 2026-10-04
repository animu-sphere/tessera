# Styling

## Implemented resolved-style contract

[resolved_style.hpp](../../include/tessera/style/resolved_style.hpp) defines `ResolvedStyle`, the typed values layout, paint, and interaction consume after resolution. No resolver exists yet: callers supply one resolved style per node, as described in [layout](layout.md). Default construction yields primitive defaults.

| Group | Field: values (default) |
| --- | --- |
| Layout | `display`: `flex`, `none` (`flex`); `direction`: `column`, `row` (`column`); `justify`: `start`, `center`, `end`, `space_between` (`start`); `align`: `start`, `center`, `end`, `stretch` (`stretch`) |
| Sizing | `width`/`height`: `Dimension` `automatic` or `points(n)` (`automatic`); `min_width`/`min_height` (0); `max_width`/`max_height` (+infinity) |
| Spacing | `margin`, `border`, `padding`: `Edges` (all 0); `gap` (0); `grow` (0); `shrink` (0) |
| Paint | `visibility`: `visible`, `hidden` (`visible`); `opacity` in [0, 1] (1); `background`, `border_color` (transparent); `corner_radius` (0) |
| Text | `text`: [`TextStyle`](text.md) (host default font, 16 units, shaper line height, weight 400); `color` (opaque black) |

A column direction and zero shrink are the defaults so undersized content overflows predictably instead of compressing implicitly. These are not CSS defaults. `Color` ([color.hpp](../../include/tessera/style/color.hpp)) holds sRGB-encoded components in [0, 1] with straight alpha.

`validate(ResolvedStyle, path)` reports each invalid value once at `path/<field>`: non-finite numbers are `invalid_number`; negative lengths and opacity/color components outside [0, 1] are `out_of_range`; a NaN or negative maximum is rejected while +infinity is accepted; a minimum above its maximum is `conflicting_constraints`; an enumeration outside its declared values is `unknown_value`. Text style errors use the [text](text.md) rules under `path/text`.

## Property vocabulary

| Group | Initial candidates |
| --- | --- |
| Layout | width/height, min/max width/height, margin, padding, gap, display, flex-direction, justify-content, align-items; grid rows/columns later |
| Visual | background, border, border-radius, opacity, visibility |
| Text | font-family, font-size, font-weight, line-height, text-align, color |
| Interaction | cursor, pointer-events, focusable |

Resolved values, units, defaults, and enumerations are fixed by the contract above. Authored property names, shorthand expansion, and authored invalid-value behavior remain open decisions. Initial layout can use explicit local/resolved properties; the full stylesheet engine arrives later.

## Selectors and cascade

Initial selector candidates are type, `.class`, `#id`, and `:hover`, `:active`, `:focus`, `:disabled`. Do not implement full CSS selector grammar early. Compound selectors and combinators require a separate scope decision.

Proposed resolution order:

1. Primitive defaults.
2. Inherited values for a small explicitly listed set of properties.
3. Matching stylesheet rules ordered by specificity, then source order.
4. Explicit node-local style overrides.

Proposed specificity is ID above class/pseudo state above type. Equal-specificity rules use stable source order. Finalize the inheritance whitelist and precedence with fixtures before exposing a format contract. Layout/spacing values should not inherit by accident. Unsupported selectors or properties require useful diagnostics rather than implied browser behavior.

## Interaction states

[Input](input.md) owns hover, active, focus, and disabled interaction state. Style resolution reads these values; it does not poll devices. Define whether ancestors match hover, how capture affects active state, and how disabled state propagates. A state change may alter layout and must invalidate the appropriate stages.

The effects of display/visibility on geometry are defined by [layout](layout.md#implemented-prototype-algorithm), alpha/paint eligibility by [paint generation](rendering.md#implemented-paint-generation), and target eligibility by [hit testing](input.md#implemented-rectangular-hit-testing). Style resolution supplies values consistently with these contracts.

## Proposed themes

Theme variables are planned after basic stylesheet resolution. Specify variable scope, fallback, missing-value diagnostics, and cycle detection before adopting a syntax.

## Property effects

[Property descriptors](ui-model.md#proposed-property-reflection) identify a set of affected stages: style resolution, layout, paint, and semantics, or none. Interaction state can affect more than one stage through pseudo states. Text/font changes affect measurement and paint; semantic label/state changes affect semantic projection. A dependency table must account for inherited values and ancestor/sibling geometry before incremental updates use it. Full-tree recomputation remains the correctness reference.

## Proposed animation

After baseline UI is correct, animate resolved opacity, transforms, color, size, and scroll offset through an injected host `AnimationClock`. Replay supplies that clock's values; no hidden wall clock or CSS animation compatibility is required. Geometry changes invalidate layout; paint-only changes need not. Interruption, easing, authored-versus-animated precedence, and scroll interaction are open decisions. Complex game animation remains host-owned.

## Verification

Prioritize selector matching, specificity ties, inheritance boundaries, local overrides, state changes, and invalid properties. Later add variable cycles and animation invalidation. Numeric resolved-style tests should prove these semantics without rendering a widget gallery.
