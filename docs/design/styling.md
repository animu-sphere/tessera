# Styling

## Implemented resolved-style contract

[resolved_style.hpp](../../include/tessera/style/resolved_style.hpp) defines `ResolvedStyle`, the typed values layout, paint, and interaction consume after resolution. Consumers take one resolved style per node, as described in [layout](layout.md), whether produced by the [prototype resolver](#implemented-prototype-style-resolution) or supplied directly by the host. Default construction yields primitive defaults.

| Group | Field: values (default) |
| --- | --- |
| Layout | `display`: `flex`, `none` (`flex`); `direction`: `column`, `row` (`column`); `justify`: `start`, `center`, `end`, `space_between` (`start`); `align`: `start`, `center`, `end`, `stretch` (`stretch`); `overflow`: `visible`, `clip` (`visible`) |
| Sizing | `width`/`height`: `Dimension` `automatic` or `points(n)` (`automatic`); `min_width`/`min_height` (0); `max_width`/`max_height` (+infinity) |
| Spacing | `margin`, `border`, `padding`: `Edges` (all 0); `gap` (0); `grow` (0); `shrink` (0) |
| Paint | `visibility`: `visible`, `hidden` (`visible`); `opacity` in [0, 1] (1); `background`, `border_color` (transparent); `corner_radius` (0) |
| Text | `text`: [`TextStyle`](text.md) (host default font, 16 units, shaper line height, weight 400); `color` (opaque black) |

A column direction and zero shrink are the defaults so undersized content overflows predictably instead of compressing implicitly. These are not CSS defaults. `Color` ([color.hpp](../../include/tessera/style/color.hpp)) holds sRGB-encoded components in [0, 1] with straight alpha.

`validate(ResolvedStyle, path)` reports each invalid value once at `path/<field>`: non-finite numbers are `invalid_number`; negative lengths and opacity/color components outside [0, 1] are `out_of_range`; a NaN or negative maximum is rejected while +infinity is accepted; a minimum above its maximum is `conflicting_constraints`; an enumeration outside its declared values is `unknown_value`. Text style errors use the [text](text.md) rules under `path/text`.

## Implemented prototype style resolution

[style_sheet.hpp](../../include/tessera/style/style_sheet.hpp) defines an in-process, full-tree resolver over a host-built C++ `StyleSheet`. It has no authored or serialized syntax and does not extend JSON v1; node `classes` and author IDs come from the [UI model](ui-model.md).

- **Declarations.** `StyleDeclarations` holds an optional value for every `ResolvedStyle` field, with the same names; `text` holds optional `font`, `size`, `line_height`, and `weight`. A `line_height` set to an empty value selects the shaper default. `apply` copies declared values only. `validate(StyleDeclarations, path)` checks each declared value by the resolved-style rules at the same `path/<field>`; a min/max pair is compared only when both are declared in one place.
- **Selectors.** A `StyleSelector` has exactly one subject — a node type (`of_type`), one class (`of_class`), or an author ID (`of_id`) — plus `StyleStates` (`hover`, `active`, `focus`, `disabled`), every one of which must match. A class matches when it appears anywhere in the node's class list. There are no combinators, multi-class selectors, or negation.
- **Rule order.** All matching rules share one layer without specificity. Base rules (no states) apply in sheet order, then state rules in sheet order; a later rule replaces earlier values for the fields it declares. Type, class, and ID subjects therefore tie by order only, and the order of a node's class list has no effect.
- **Resolution.** `resolve_styles(StyleInput)` visits nodes in tree preorder and returns one `ResolvedStyle` per node by `NodeHandle::index`: primitive defaults; the parent's resolved `text` and `color` (the whole inheritance whitelist); base and state rules; then the node's instance override when `overrides` is supplied. Layout, spacing, paint, `visibility`, and `opacity` never inherit, so visibility stays local as [layout](layout.md#implemented-prototype-algorithm) and [hit testing](input.md#implemented-rectangular-hit-testing) require. Theme and component-local layers from the proposed order below do not exist yet.
- **Interaction states.** The host supplies `InteractionState` from [input](input.md): hovered targets and active binding owners (for example from `PointerState`), and the focused node (for example from `FocusDispatchResult`). `:hover` and `:active` match each supplied node and its ancestors, so a hovered label makes its button match; `:focus` matches the focused node only. `:disabled` matches a node whose `disabled` property is true on itself or an ancestor, the [targeting](input.md#implemented-rectangular-hit-testing) rule, and a disabled node matches no other state even when the supplied state names it. The resolver does not poll devices or dispatchers.
- **Diagnostics.** `validate(StyleInput)` reports a missing tree or sheet (`missing_input` at `/tree`, `/sheet`); sheet errors under `/sheet/rules/<i>`: an unknown selector kind or type (`unknown_value` at `selector/kind`, `selector/type`), an empty or control-character class/ID name (`invalid_identifier`) or invalid UTF-8 (`invalid_utf8`) at `selector/name`, and declaration errors under `declarations`; state handles from another tree or out of range (`stale_target` at `/state/hovered/<i>`, `/state/active/<i>`, `/state/focused`); an override span that is neither empty nor one per node (`override_count` at `/overrides`); and override value errors under `/overrides/<i>`. Values that are valid separately but conflict once combined, such as a rule's `min_height` above another rule's `max_height`, fail with the resolved-style rules under `/nodes/<index>`. Rejection returns no styles.

The resolver does not invalidate anything: after a state or sheet change the host resolves again and, when styles differ, recomputes layout and paint and refreshes pointer and focus dispatch before mapping later input. Full-tree resolution remains the reference for any later dirty tracking.

## Property vocabulary

| Group | Initial candidates |
| --- | --- |
| Layout | width/height, min/max width/height, margin, padding, gap, display, flex-direction, justify-content, align-items; grid rows/columns later |
| Visual | background, border, border-radius, opacity, visibility |
| Text | font-family, font-size, font-weight, line-height, text-align, color |
| Interaction | cursor, pointer-events, focusable |

Resolved values, units, defaults, and enumerations are fixed by the contract above. Authored property names, shorthand expansion, and authored invalid-value behavior remain open decisions. Initial layout can use explicit local/resolved properties; the full stylesheet engine arrives later.

## Selectors and cascade

Initial selector candidates are type, `.class`, `#id`, and `:hover`, `:active`, `:focus`, `:disabled`; the [prototype](#implemented-prototype-style-resolution) implements them as one subject plus required states. Checked/selected states follow controls that define them. Do not implement full CSS selector grammar early. Multi-class selectors and combinators require a separate scope decision. Selectors choose applicable rules; they do not introduce browser-style specificity.

Proposed resolution order, of which the prototype implements steps 1, 2, 4, and 6:

1. Primitive defaults.
2. Inherited values for a small explicitly listed set of properties.
3. Theme-provided defaults/styles.
4. Explicit style classes and their matching state rules.
5. Component-local style.
6. Instance overrides.

Use explicit layers and stable source order for ties within a layer. The prototype's composition of classes and base/state rules, its class-list meaning, and its `text`/`color` inheritance whitelist are the candidates to confirm before freezing an authored syntax. IDs identify matches without silently outranking instance overrides. Finalize theme-variable behavior with fixtures before exposing a format contract. Layout/spacing values should not inherit by accident. Unsupported selectors or properties require useful diagnostics rather than implied browser behavior.

## Interaction states

[Input](input.md) owns hover, active, focus, and disabled interaction state. Style resolution reads these values; it does not poll devices. The [prototype](#implemented-prototype-style-resolution) defines ancestor matching and disabled propagation. Capture follows the pointer dispatcher's `active` owner, which is cleared while the pointer is outside it. A state change may alter layout and must invalidate the appropriate stages; automatic invalidation is not defined.

The effects of display/visibility on geometry are defined by [layout](layout.md#implemented-prototype-algorithm), alpha/paint eligibility by [paint generation](rendering.md#implemented-paint-generation), and target eligibility by [hit testing](input.md#implemented-rectangular-hit-testing). Style resolution supplies values consistently with these contracts.

## Proposed themes

Theme variables are planned after basic stylesheet resolution. Specify variable scope, fallback, missing-value diagnostics, and cycle detection before adopting a syntax.

## Property effects

[Property descriptors](ui-model.md#implemented-property-metadata) identify a set of affected stages: style resolution, layout, paint, input targeting, and semantics, or none. Input targeting covers pointer eligibility derived from a snapshot, such as inherited `disabled`. Interaction state can affect more than one stage through pseudo states. Text/font changes affect measurement and paint; semantic label/state changes affect semantic projection. A dependency table must account for inherited values and ancestor/sibling geometry before incremental updates use it. Full-tree recomputation remains the correctness reference.

## Proposed animation

After baseline UI is correct, animate resolved opacity, transforms, color, size, and scroll offset through an injected host `AnimationClock`. Replay supplies that clock's values; no hidden wall clock or CSS animation compatibility is required. Geometry changes invalidate layout; paint-only changes need not. Interruption, easing, authored-versus-animated precedence, and scroll interaction are open decisions. Complex game animation remains host-owned.

Opacity/transform and other paint-only changes may avoid layout, but compositor-only execution needs a concrete retained-render contract and evidence. Color can require repaint/upload; clip/transform changes may also affect input or semantic bounds. Do not classify all visual animations as GPU-only or skip those consumers.

## Verification

Prioritize selector matching, layer/source-order ties, inheritance boundaries, component/instance overrides, state changes, and invalid properties. Later add variable cycles and animation invalidation. Numeric resolved-style tests should prove these semantics without rendering a widget gallery.

[Style sheet checks](../../tests/style/style_sheet_tests.cpp) cover type/class/ID matching, sheet-order ties, base-before-state ordering, class-list order independence, the inheritance whitelist and non-inherited fields, ancestor hover/active and node-only focus, inherited disabled suppressing other states, multi-state rules and multiple pointers, overrides after ID and state rules, declaration/selector/state/override diagnostics, and combined conflicts located at a node. Component-local styles, themes, and authored syntax still need fixtures.
