# C++ authoring and widget composition

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed public authoring surface

Ordinary application authors construct UI through a small typed C++ surface and a component/widget library. The surface should minimize boilerplate, support ordinary C++ composition and predictable ownership, and lower into the [common representation](../architecture/ui-model.md#common-representation). It must not introduce an execution, reconciliation or styling model of its own.

The source-backed form vocabulary, session state/lifetime and lowering rules are owned by [Implemented C++ form authoring](../architecture/ui-model.md#implemented-c-form-authoring). The broader mechanisms and syntax below are Proposed, independent of that bounded contract.

Illustrative syntax, not a public API contract:

```cpp
auto app = Column({
    Text("Settings"),
    Row({Text("Username"), TextInput(username)}),
    Checkbox("Enable feature", enabled),
    Button("Apply", commands::apply),
});
```

The minimum conceptual coverage is primitive nodes, ordered children, typed props and defaults, state and computed values, event/command bindings, conditional and repeated UI, reusable components, style attachment and stable identity where required. Exact names, fluent syntax, builder representation and binding deduction are open decisions to test against the [Settings consumer](../integrations/settings-app.md).

## Proposed state and component authoring

A small public mental model such as `state()`, `computed()` and `bind()` should wrap ordinary [sources, calculations and settled reads](../architecture/reactive-runtime.md). State ownership, equality, batch/publication timing and stale-owner rejection follow that contract. Application data, external execution and edit history remain host-owned.

Component authors should define typed props/defaults, children or named slots where needed, local and derived state, commands/events, style and optional async observations through composition. Stable identity, testability and source/inspection visibility are requirements. Graph node classes, owner registration, explicit binding objects, mounting records, cleanup wiring, effect tables and async detachment plumbing belong behind the normal authoring path; lower-level APIs can expose deliberate advanced control.

Conveniences must retain weak lifetime checks and once-only cleanup rather than concealing unsafe captures. Conditional removal, keyed reorder and incompatible replacement use the [UI model's identity rules](../architecture/ui-model.md#state-and-reconciliation). The facade does not grant arbitrary callback execution during traversal or bypass host-controlled publication.

## Proposed widgets and styles

Widgets normally compose ordinary runtime primitives and components. Promote a capability into core only when several widgets need the same low-level behavior and ordinary composition cannot express it correctly. A public widget name does not imply a new stored node kind. Candidate inventories and scheduling belong in [backlog](../roadmap/backlog.md).

Style objects/classes, theme variables and interaction styles lower into the [shared style model](../architecture/styling.md). Convenient typography, spacing, borders, backgrounds, dimensions and alignment must preserve its validation, cascade and affected-stage rules. A public pressed style maps to the existing active interaction concept; a second widget-specific style resolver is excluded. CSS syntax or compatibility is not required.

## Verification

Verify application authoring without manual graph/mounting records, typed prop/default rejection, reusable children, command eligibility, coherent derived state, keyed reorder/removal and owner cleanup. Compare accepted semantic, geometry and paint observations with the ordinary full-tree path. Preserve source-located diagnostics and reject invalid candidates before publication under the [testing strategy](../guides/testing.md).

## Promotion criteria

Promote only the facade or widget behavior needed by a named reference-application operation, with its ownership, lowering, input and observable acceptance defined first. Stop when that operation and the affected existing contracts pass. Syntax stabilization, new lifecycle abstractions, language bindings and generalized widget frameworks need separate consumer evidence.
