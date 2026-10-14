# Property reflection

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed property reflection

Extend the implemented descriptors as the schema evolves, without a general reflection framework or editor SDK dependency. Add valid range/enum metadata when a numeric or enumerated property is introduced, and distinguish derived or read-only values if they are exposed. A property can affect multiple stages; do not force style/layout/paint/semantics into a mutually exclusive enum.

Use the same descriptors for Inspector generation and invalidation. Do not maintain a second editor-owned list of types/defaults. This proposal does not add fields to JSON v1. Path-finder consumes this metadata under its [bridge contract](../integrations/path-finder.md).

Separate static schema descriptors from observations of one generation. The schema supplies stable property ID, display name, semantic type, default/absence rules, editability, validation rules, enum/range metadata where applicable, and affected stages. Observations join current, authored, default, inherited, and resolved values as applicable, source origin, component owner, and generation. Missing or inapplicable values are explicit rather than guessed from a rendered result; derived observations are read-only unless they expose a declared edit operation.

The affected-stage set follows [property effects](../architecture/styling.md#property-effects); resource-related effects need an explicit extension when resource properties are introduced. Metadata can support semantic, style, layout, paint, resource, and interaction work without requiring incremental evaluation. Inspector edits validate a typed candidate, enter the ordinary command/transaction or declared runtime edit boundary, and publish one accepted generation. A generic Inspector must discover editors from this schema without hardcoding Box/Text properties, and observe the resulting property/source metadata after an edit.

## Promotion criteria

Promote only the schema/observation fields needed to discover and edit a property in the active Inspector fixture. New property kinds, range/enum fields and derived-value editing need their own consumer requirement.
