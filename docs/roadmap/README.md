# Roadmap

Status: Planning baseline, 2026-10-05. No runtime or public version has been delivered.

[Current](current.md) owns active and immediately next work. [Backlog](backlog.md) owns inactive candidates. [Support matrix](../reference/support-matrix.md) owns validated capabilities; roadmap checkboxes do not substitute for runtime evidence.

## Strategy phase sequence

| Phase | Goal | Deliverable | Current state |
| --- | --- | --- | --- |
| 0 — Foundation | Repository/build foundation, nodes, document/properties, event types, backend contract | Create, inspect, validate, and serialize a tree | Docs prepared; implementation next |
| 1 — Layout prototype | Box, placeholder Text, fixed/stack/flex, margin/padding/gap | Deterministic `LayoutBox` tree without GPU | Planned |
| 2 — Vulkan primitives | Rectangles, colors, clips, transforms, image draw path, batching, Slang | Tree to paint list to native host window | Planned |
| 3 — Input | Pointer, hover/click, keyboard/focus, gamepad navigation | Interactive menu | Planned; pointer subset comes first |
| 4 — Text | Font abstraction, shaping/cache, UTF-8, Latin/Japanese, measurement/wrapping | Declared text coverage with font/image evidence | Planned |
| 5 — Styling | Selectors, pseudo states, inheritance, theme variables | Web-inspired skinning | Planned |
| 6 — Components | Props/state/bindings, conditionals/lists, keyed reconciliation | Dynamic application UI | Planned |
| 7 — Path-finder bridge | Schema/metadata/source locations, diagnostics, live reload, editor bridge | Shared document authoring and preview | Planned |
| 8 — WebGPU | Backend/shader path, native and browser/WASM feasibility | Same document across proven backends | Planned |

Phases establish dependency order, not isolated silos. Define the text interface before real shaping, resolved-style values before stylesheet parsing, and document versioning before the full editor schema. Interfaces can be introduced early without claiming their later capability is implemented.

## Public milestone candidates

| Candidate | Scope |
| --- | --- |
| v0.1.0 | Tree + Box/placeholder Text + stack/flex + background/border + Vulkan + pointer hit testing |
| v0.2.0 | Real text + focus/keyboard/gamepad + scroll container + style classes and hover/focus states |
| v0.3.0 | Component state + keyed lists + image assets + themes + live reload + Path-finder prototype |
| Later | Complete editor bridge, WebGPU feasibility/parity, animation and extensions justified by consumers |

These candidates slice across the strategy phases. Low-level image rendering in Phase 2 does not require a complete application asset/component system before v0.3.0. Keyboard/gamepad semantics are designed early and delivered with the broader input milestone. Dates and version labels can change with integration evidence.

## Tracking conventions

- Use a checked item only for delivered work with identifiable verification.
- Give each active milestone an objective, bounded work list, and observable exit criteria.
- Move completed implementation out of active work into a changelog/delivery record when such records become necessary.
- Update the support matrix only for the capability and configuration actually verified.
- Keep architectural contracts in [design](../README.md), linked from tasks rather than copied into them.
