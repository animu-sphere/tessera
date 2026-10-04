# Styling

Status: Draft design. CSS-inspired semantics; no CSS compatibility claim.

## Property vocabulary

| Group | Initial candidates |
| --- | --- |
| Layout | width/height, min/max width/height, margin, padding, gap, display, flex-direction, justify-content, align-items; grid rows/columns later |
| Visual | background, border, border-radius, opacity, visibility |
| Text | font-family, font-size, font-weight, line-height, text-align, color |
| Interaction | cursor, pointer-events, focusable |

Resolved styles must contain typed values, independently consumable by layout, paint, and interaction. Property units, defaults, enum values, shorthand expansion, and invalid-value behavior remain open decisions. Initial layout can use explicit local/resolved properties; the full stylesheet engine arrives later.

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

Proposed visibility distinction: a non-displayed node does not participate in layout or interaction; a hidden node retains layout but does not paint or receive interaction. Zero opacity alone should not implicitly decide pointer behavior. Exact value names and focus policy remain to be specified.

## Themes and animation

Theme variables are planned after basic stylesheet resolution. Specify variable scope, fallback, missing-value diagnostics, and cycle detection before adopting a syntax.

UI animation operates on resolved opacity, position/size, transforms, and colors. Transitions may use an explicit duration such as an illustrative `opacity 120ms`. Time comes from the host frame clock. Animations affecting geometry invalidate layout; paint-only changes need not. Interruption, easing, and authored-value versus animated-value precedence are open. Complex game animation remains a host responsibility.

## Verification

Prioritize selector matching, specificity ties, inheritance boundaries, local overrides, state changes, and invalid properties. Later add variable cycles and animation invalidation. Numeric resolved-style tests should prove these semantics without rendering a widget gallery.
