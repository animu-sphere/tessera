# Styling

## Core invariants

- Style resolution produces values independently of input devices and renderer objects.
- Cascade, inheritance and instance overrides use declared deterministic ordering and exact value types.
- Property effects identify every affected consumer; a visual name alone cannot justify skipping layout, input or semantics.

## Implemented resolved-style contract

[resolved_style.hpp](../../include/tessera/style/resolved_style.hpp) defines `ResolvedStyle`, the typed values layout, paint, and interaction consume after resolution. Consumers take one resolved style per node, as described in [layout](layout.md), whether produced by the [prototype resolver](#implemented-prototype-style-resolution) or supplied directly by the host. Default construction yields primitive defaults.

| Group | Field: values (default) |
| --- | --- |
| Layout | `display`: `flex`, `none`, `stack`, `grid` (`flex`); `direction`: `column`, `row` (`column`); `flex_wrap`: `no_wrap`, `wrap` (`no_wrap`), [row wrapping](layout.md#implemented-flex-wrapping); `justify`: `start`, `center`, `end`, `space_between` (`start`); `align`: `start`, `center`, `end`, `stretch` (`stretch`); `overflow`: `visible`, `clip`, `scroll` (`visible`) |
| Stack child placement | `stack_horizontal`, `stack_vertical`: `start`, `center`, `end`, `stretch` (`stretch`); applied only by a `display: stack` parent, independently of the child's own container alignment |
| Grid | `grid_columns`, `grid_rows`: `vector<GridTrack>` (empty); track kind `automatic`, `points`, `fraction`, value (0), minimum (0), maximum (+infinity); `column_gap`/`row_gap` (0); child `grid_horizontal`/`grid_vertical`: `Align` (`stretch`); child `grid_column_span`/`grid_row_span`: `uint16_t` in 1–256 (1); [layout](layout.md#implemented-grid-placement) owns explicit shape/allocation |
| Absolute placement | `position`: `flow`, `absolute` (`flow`); `left`, `top`, `right`, `bottom`: optional finite `float`, negative allowed (absent, automatic); [layout](layout.md#implemented-absolute-positioning) owns placement against the parent's padding box |
| Stacking | `z_index`: `int32_t` (0); orders a box among its displayed siblings for paint and hit testing only, scoping its descendants; root layers ignore it; [layout](layout.md#implemented-stacking-order) owns the order |
| Sizing | `width`/`height`: `Dimension` `automatic` or `points(n)` (`automatic`); `min_width`/`min_height` (0); `max_width`/`max_height` (+infinity) |
| Spacing | `margin`, `border`, `padding`: `Edges` (all 0); `gap` (0); `grow` (0); `shrink` (0) |
| Paint | `visibility`: `visible`, `hidden` (`visible`); `opacity` in [0, 1] (1); `background`, `border_color` (transparent); `corner_radius` (0); `background_gradient`: optional `Gradient` (absent), a multi-stop `LinearGradient` or `RadialGradient` replacing `background` under the [rendering contract](rendering.md#implemented-linear-gradients) |
| Text | `text`: [`TextStyle`](text.md) (host default font, 16 units, shaper line height, weight 400); `color` (opaque black) |
| Shadow | `box_shadow`: optional [`BoxShadow`](rendering.md#implemented-box-shadows) (absent), with logical offset, nonnegative softness, color and `inset` (false); paint-only, not inherited |
| Outline | `outline`: optional [`Outline`](rendering.md#implemented-outlines-and-dashed-strokes) (absent), with width, outside offset, color and dash lengths/phase; paint-only, not inherited |
| Background geometry | `background_shape`: [`FillShape`](rendering.md#implemented-analytic-fills) `rectangle`/`ellipse` (`rectangle`); `background_antialias`: bool (false); paint-only, not inherited, independent of borders/shadows/outlines |
| Procedural fill | `background_procedural`: optional [`ProceduralFill`](rendering.md#implemented-procedural-fills) (absent) with logical shader identity, typed parameters, time phase, opacity and time rate (evaluated against paint's host time); drawn inside the background shape where a backend registers the shader, with `background`/`background_gradient` as fallback; paint-only, not inherited |

A column direction and zero shrink are the defaults so undersized content overflows predictably instead of compressing implicitly. These are not CSS defaults. `Color` ([color.hpp](../../include/tessera/style/color.hpp)) holds sRGB-encoded components in [0, 1] with straight alpha.

`validate(ResolvedStyle, path)` reports each invalid value once at `path/<field>`: non-finite numbers are `invalid_number`; negative lengths and opacity/color components outside [0, 1] are `out_of_range`; a NaN or negative maximum is rejected while +infinity is accepted; a minimum above its maximum is `conflicting_constraints`; an enumeration outside its declared values is `unknown_value`. Text style errors use the [text](text.md) rules under `path/text`.

## Implemented prototype style resolution

[style_sheet.hpp](../../include/tessera/style/style_sheet.hpp) defines an in-process, full-tree resolver over a host-built C++ `StyleSheet`. It has no authored or serialized syntax and does not extend JSON v1; node `classes` and author IDs come from the [UI model](ui-model.md).

- **Declarations.** `StyleDeclarations` holds an optional value for every `ResolvedStyle` field, with the same names; `text` holds optional `font`, `size`, `line_height`, and `weight`. A `line_height` set to an empty value selects the shaper default. `apply` copies declared values only. `validate(StyleDeclarations, path)` checks each declared value by the resolved-style rules at the same `path/<field>`; a min/max pair is compared only when both are declared in one place. A rule's `variables` supplies [theme bindings](#implemented-theme-variable-boundary) after its literal declarations.
- **Optional fills.** `StyleDeclarations::background_gradient` is an `optional<optional<Gradient>>`, where `Gradient` is `variant<LinearGradient, RadialGradient>`: an absent outer optional leaves the value unchanged, a declared gradient replaces either kind, and a declared empty inner optional clears it. Solid `background` declarations do not implicitly clear a gradient; clear the gradient explicitly when switching a rule to solid. Gradients never inherit.
- **Optional shadows.** `StyleDeclarations::box_shadow` is an `optional<optional<BoxShadow>>`: absent leaves the current effect unchanged, a value replaces it including outer/inset mode, and an empty inner optional clears it. Background declarations do not clear shadows. Offset, softness and color validate under `box_shadow/offset`, `box_shadow/softness` and `box_shadow/color` through the [rendering contract](rendering.md#implemented-box-shadows).
- **Optional outlines.** `StyleDeclarations::outline` is an `optional<optional<Outline>>` with the same preserve/replace/clear behavior. It does not reserve border space or inherit; `corner_radius` supplies the box shape. Width, outside offset, color and dash geometry validate under `outline` through the [stroke contract](rendering.md#implemented-outlines-and-dashed-strokes).
- **Background geometry.** `background_shape` and `background_antialias` set shape and coverage for both solid and gradient fills through [analytic fills](rendering.md#implemented-analytic-fills). Absent declarations leave values unchanged; explicit `FillShape::rectangle` and `false` restore the defaults without clearing colors or gradients. Neither field changes layout, input or the independent box-decoration geometry.
- **Optional procedural fills.** `StyleDeclarations::background_procedural` is an `optional<optional<ProceduralFill>>` with the same preserve/replace/clear behavior. It does not clear `background` or `background_gradient`, which remain its declared fallback. Identity, parameters, time, opacity and time rate validate under `background_procedural` through the [procedural fill contract](rendering.md#implemented-procedural-fills). It is not a theme variable type in this slice.
- **Selectors.** A `StyleSelector` has exactly one subject — a node type (`of_type`), one class (`of_class`), or an author ID (`of_id`) — plus `StyleStates` (`hover`, `active`, `focus`, `disabled`), every one of which must match. A class matches when it appears anywhere in the node's class list. There are no combinators, multi-class selectors, or negation.
- **Rule order.** All matching rules share one layer without specificity. Base rules (no states) apply in sheet order, then state rules in sheet order; a later rule replaces earlier values for the fields it declares. Type, class, and ID subjects therefore tie by order only, and the order of a node's class list has no effect.
- **Resolution.** `resolve_styles(StyleInput)` visits nodes in tree preorder and returns one `ResolvedStyle` per node by `NodeHandle::index`: primitive defaults; the parent's resolved `text` and `color` (the whole inheritance whitelist); the optional theme's base and state rules; the explicit sheet's base and state rules; then the node's instance override when `overrides` is supplied. Layout, spacing, paint, `visibility`, and `opacity` never inherit, so visibility stays local as [layout](layout.md#implemented-prototype-algorithm) and [hit testing](input.md#implemented-rectangular-hit-testing) require. The resolver has no component-local layer from the proposed order below.
- **Interaction states.** The host supplies `InteractionState` from [input](input.md): hovered targets and active binding owners (for example from `PointerState`), and the focused node (for example from `FocusDispatchResult`). `:hover` and `:active` match each supplied node and its ancestors, so a hovered label makes its button match; `:focus` matches the focused node only. `:disabled` matches a node whose `disabled` property is true on itself or an ancestor, the [targeting](input.md#implemented-rectangular-hit-testing) rule, and a disabled node matches no other state even when the supplied state names it. The resolver does not poll devices or dispatchers.
- **Diagnostics.** `validate(StyleInput)` reports a missing tree or sheet (`missing_input` at `/tree`, `/sheet`); sheet errors under `/sheet/rules/<i>`: an unknown selector kind or type (`unknown_value` at `selector/kind`, `selector/type`), an empty or control-character class/ID name (`invalid_identifier`) or invalid UTF-8 (`invalid_utf8`) at `selector/name`, and declaration errors under `declarations`; state handles from another tree or out of range (`stale_target` at `/state/hovered/<i>`, `/state/active/<i>`, `/state/focused`); an override span that is neither empty nor one per node (`override_count` at `/overrides`); and override value errors under `/overrides/<i>`. Values that are valid separately but conflict once combined, such as a rule's `min_height` above another rule's `max_height`, fail with the resolved-style rules under `/nodes/<index>`. Rejection returns no styles.

The resolver does not invalidate anything: after a state or sheet change the host resolves again and, when styles differ, recomputes layout and paint and refreshes pointer and focus dispatch before mapping later input. Full-tree resolution remains the reference for any later dirty tracking. [Component style bindings](ui-model.md#implemented-component-style-bindings) supply settled typed instance overrides to this same resolver; they introduce no extra cascade layer or serialized style syntax.

## Property vocabulary

The [proposed authoring surface](../proposals/cpp-authoring.md#proposed-widgets-and-styles) can attach reusable style objects/classes, theme variables and interaction styles. It lowers to the same declarations, overrides and resolver; widget-specific styling engines are excluded. Convenience names must preserve exact value types, cascade order and affected-stage metadata.

| Group | Initial candidates |
| --- | --- |
| Layout | width/height, min/max width/height, margin, padding, gap, display, flex-direction, justify-content, align-items; explicit grid rows/columns and row/column gaps |
| Visual | background, border, border-radius, opacity, visibility |
| Text | font-family, font-size, font-weight, line-height, text-align, color |
| Interaction | cursor, pointer-events, focusable |

Resolved values, units, defaults, and enumerations are fixed by the contract above. Authored property names, shorthand expansion, and authored invalid-value behavior remain open decisions. Initial layout can use explicit local/resolved properties; the full stylesheet engine arrives later.

## Selectors and cascade

Initial selector candidates are type, `.class`, `#id`, and `:hover`, `:active`, `:focus`, `:disabled`; the [prototype](#implemented-prototype-style-resolution) implements them as one subject plus required states. Checked/selected states follow controls that define them. Do not implement full CSS selector grammar early. Multi-class selectors and combinators require a separate scope decision. Selectors choose applicable rules; they do not introduce browser-style specificity.

Proposed resolution order, of which the prototype implements steps 1, 2, 3, 4, and 6:

1. Primitive defaults.
2. Inherited values for a small explicitly listed set of properties.
3. Theme-provided defaults/styles.
4. Explicit style classes and their matching state rules.
5. Component-local style.
6. Instance overrides.

Use explicit layers and stable source order for ties within a layer. The prototype's composition of classes and base/state rules, its class-list meaning, and its `text`/`color` inheritance whitelist are the candidates to confirm before freezing an authored syntax. IDs identify matches without silently outranking instance overrides. An authored theme syntax requires a separate format contract. Layout/spacing values should not inherit by accident. Unsupported selectors or properties require useful diagnostics rather than implied browser behavior.

## Interaction states

[Input](input.md) owns hover, active, focus, and disabled interaction state. Style resolution reads these values; it does not poll devices. The [prototype](#implemented-prototype-style-resolution) defines ancestor matching and disabled propagation. Capture follows the pointer dispatcher's `active` owner, which is cleared while the pointer is outside it. A state change may alter layout and must invalidate the appropriate stages; automatic invalidation is not defined.

The effects of display/visibility on geometry are defined by [layout](layout.md#implemented-prototype-algorithm), alpha/paint eligibility by [paint generation](rendering.md#implemented-paint-generation), and target eligibility by [hit testing](input.md#implemented-rectangular-hit-testing). Style resolution supplies values consistently with these contracts.

## Implemented theme variable boundary

[style_sheet.hpp](../../include/tessera/style/style_sheet.hpp) defines host-built `Theme`, `ThemeValue`, `ThemeReference`, and `ThemeBinding`; [theme.cpp](../../src/style/theme.cpp) materializes variable declarations before ordinary full-tree resolution. `StyleInput::theme` borrows one optional theme for the call. Names share one table across the tree and both rule layers; there is no global mutable theme, subtree provider, reactive subscription, expression language, or serialized theme syntax. Instance and component style arguments remain concrete `StyleDeclarations`.

- **Values and aliases.** An ordered `Theme::variables` array declares unique, case-sensitive UTF-8 names and either a literal `ThemeValue` or a `ThemeReference` alias. Values have the exact C++ types used by resolved fields: `float`, `Color`, `Dimension`, `Edges`, `FontId`, `optional<float>` for line height or an absolute offset (empty selects the default or automatic placement), `uint16_t` for weight or a Grid span, `Position` for `position`, `FlexWrap` for `flex_wrap`, `int32_t` for `z_index`, the layout/visibility enumerations, `vector<GridTrack>` for either Grid axis, `optional<Gradient>` for `background_gradient` (empty clears either fill kind), `optional<BoxShadow>` for `box_shadow`, `optional<Outline>` for `outline` (empty clears the decoration), `FillShape` for `background_shape`, and `bool` for `background_antialias`. No coercion occurs. Declaration order does not affect alias lookup. Every variable validates even when unused. Float tokens permit nonnegative values and +infinity, and `optional<float>` tokens any finite value; a consuming field applies its stricter finite/range rules. Other literals validate by their ordinary style type, including unused literal fallbacks.
- **References and fallback.** A reference names one variable and may supply a literal fallback, used only if that name is absent. An alias may therefore supply its own fallback. An existing variable's invalid dependency, a cycle, or a destination type mismatch rejects the candidate; the consumer's fallback cannot conceal it. Alias evaluation caches within this call and detects cycles before traversing the UI tree.
- **Bindings.** Each rule may bind any resolved field using its name, with `text/font`, `text/size`, `text/line_height`, and `text/weight` for nested fields. Bindings replace that rule's literals and each field has at most one binding per rule. Exact destination type and ordinary declaration constraints apply before matching selectors, so an unmatched or later-overridden rule is validated too. A fallback must satisfy its destination's type and constraints even when a variable exists. An empty `optional<float>` resets line height to the shaper default.
- **Layers.** `Theme::defaults` is an ordinary `StyleSheet` in a lower layer. Its base rules precede its state rules, then all explicit sheet base/state rules follow. Thus an explicit base rule outranks a theme state rule. Selectors, disabled propagation, inheritance, and final instance overrides use the ordinary resolver contract. All rules in either layer refer to the same supplied variable table. A missing theme is an empty table; references with valid literal fallbacks can resolve without it.
- **Bounds and diagnostics.** There are at most 256 variables, 128 UTF-8 bytes per variable/reference name, 1,024 theme default rules, and 4,096 variable bindings shared by theme and explicit sheet. Bound failures precede alias traversal and declaration copies. Declaration errors locate `/theme/variables/<i>/name` or `/value`; alias errors use `/value/name`. `theme_duplicate_variable`, `theme_missing`, `theme_cycle`, and `theme_unavailable` distinguish duplicate declarations, missing names, closing cycle edges, and failed dependencies. Rule bindings locate `/theme/defaults/rules/<i>/variables/<j>` or `/sheet/rules/<i>/variables/<j>`; `theme_property`, `theme_duplicate_binding`, and `theme_type` identify unknown fields, multiple writers, and exact-type failures. Resolved binding constraints use the rule's `/declarations/<field>`; combined cross-rule/layer conflicts use `/nodes/<index>/<field>`. Malformed names use `invalid_utf8` or `invalid_identifier`; size limits use `theme_name_bound`, `theme_variable_bound`, `theme_rule_bound`, and `theme_binding_bound`. `validate(StyleSheet)` checks binding syntax/fallbacks without a table; `validate(Theme)` checks the complete theme under `/theme`; `validate(StyleInput)` checks both layers against one table.

The host accepts owned resolved styles only after successful resolution, then recomputes coherent layout/semantics/paint and refreshes input before publication. A rejected theme returns no styles and does not modify prior owned styles, geometry, component owners, or local state. A theme change requires full-tree evaluation; no automatic invalidation or publication occurs.

## Proposed theme scopes

Subtree providers and reactive theme bindings follow [context ownership](ui-model.md#proposed-context) and [presentation integration](../proposals/incremental-evaluation.md#proposed-presentation-integration). Define nested scope lookup, reload identity and replay inputs before adopting an authored theme syntax. These extensions must preserve exact types, missing-value fallback, cycle rejection, and the ordinary cascade.

## Property effects

[Property descriptors](ui-model.md#implemented-property-metadata) identify a set of affected stages: style resolution, layout, paint, input targeting, and semantics, or none. Input targeting covers pointer eligibility derived from a snapshot, such as inherited `disabled`. Interaction state can affect more than one stage through pseudo states. Text/font changes affect measurement and paint; semantic label/state changes affect semantic projection. A dependency table must account for inherited values and ancestor/sibling geometry before incremental updates use it. Full-tree recomputation remains the correctness reference.

### Proposed stage invalidation

Affected stages form a set, and inherited/pseudo-state dependencies must be resolved before work is skipped. Full-tree style resolution is the correctness reference.

Possible mechanisms: [incremental evaluation](../proposals/incremental-evaluation.md#proposed-stage-invalidation). This direction is not an implementation task; only the active roadmap can require a bounded part.

## Proposed animation

Animation uses explicit host-clock/replay inputs and the same affected-stage rules as ordinary property updates. A visual property is not automatically compositor-only work.

Possible mechanisms: [animation system](../proposals/animation.md#proposed-animation). This direction is not an implementation task; only the active roadmap can require a bounded part.

## Verification

Prioritize selector matching, layer/source-order ties, inheritance boundaries, component/instance overrides, state changes, variable lookup/type/fallback/cycles, and invalid properties. Animation invalidation requires separate fixtures. Numeric resolved-style tests should prove these semantics without rendering a widget gallery.

[Style sheet checks](../../tests/style/style_sheet_tests.cpp) verify type/class/ID matching, sheet-order ties, base-before-state ordering, class-list order independence, the inheritance whitelist and non-inherited fields, ancestor hover/active and node-only focus, inherited disabled suppressing other states, multi-state rules and multiple pointers, overrides after ID and state rules, declaration/selector/state/override diagnostics, and combined conflicts located at a node. [Theme checks](../../tests/style/theme_tests.cpp) verify exact destinations, aliases/fallbacks, complete-candidate rejection, shared bounds, layer precedence, inheritance, keyed component/focus preservation, and full-tree style/layout/input/semantic/paint parity through a theme switch. Component-local layers and authored syntax require separate fixtures.
