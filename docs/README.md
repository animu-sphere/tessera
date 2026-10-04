# Documentation

These documents incorporate the owner-provided implementation direction dated 2026-10-05 (`tessera-implementation-roadmap.md`), superseding the earlier strategy draft. The external file is an input, not a second maintained roadmap. Repository documents below own subsequent changes; no local Desktop path is required to use them.

## Reading order

1. [Architecture](design/architecture.md): purpose, ownership, dependency direction, and frame lifecycle.
2. [UI model](design/ui-model.md): common representation beneath authoring frontends.
3. [Current work](roadmap/current.md): active scope and exit criteria.
4. The relevant subsystem contract and [testing strategy](guides/testing.md).

## Canonical owners

| Document | Owns | Does not own |
| --- | --- | --- |
| [Architecture](design/architecture.md) | Ecosystem boundaries, determinism, update/snapshot lifecycle, core constraints | Capability status or directory inventory |
| [UI model](design/ui-model.md) | Tree/identity, semantic properties, reflection metadata, components, reconciliation, portal ownership, frontend lowering | Encoded fields, editor transport, interaction policy |
| [JSON v1](../formats/tessera-ui/README.md) | Serialized fields, formatting, metadata preservation, rejection and bounds | Future schema proposals |
| [Layout](design/layout.md) | Geometry, logical coordinates, sizing, scrolling, positioning, virtualization geometry | Physical-pixel conversion or navigation |
| [Styling](design/styling.md) | Resolved values, cascade, inheritance, themes, property effects, animation | Device input or progress tracking |
| [Rendering](design/rendering.md) | Paint vocabulary/order, backend/resource contract, pixel conversion, shaders, batching, custom paint, asset boundary | Layout or application state |
| [Text](design/text.md) | Font/shaping abstraction, indices, metrics, fallback, glyph caching | Editing events and clipboard operations |
| [Input](design/input.md) | Normalized events, hit tests, focus/navigation, event-to-action translation, editing/IME/clipboard boundary | Semantic schema or OS adapters |
| [Semantics](design/semantics.md) | Semantic projection, roles/names/state/actions, inspection and accessibility/automation adapter boundary | Focus algorithms or host action implementation |
| [Replay](design/replay.md) | Versioned recording inputs, deterministic playback and observable outputs | Test procedure or CLI support claims |
| [Path-finder integration](design/path-finder-integration.md) | Shared-schema editing, diagnostics, reload transaction, preview bridge | A second property/semantic schema or runtime |
| [Development](guides/development.md) | Build/run commands and workflow | Test-result history or subsystem contracts |
| [Testing](guides/testing.md) | Verification methods and documentation checks | Test-target inventory or capability status |
| [Dependencies](reference/dependencies.md) | Adopted/candidate choices, acquisition and licensing policy | Validated configuration claims |
| [Support matrix](reference/support-matrix.md) | **Only live implementation/validation status**, configurations and evidence | Work scheduling or contract definitions |
| [Roadmap index](roadmap/README.md) | Planning conventions and links | A second milestone/status table |
| [Current](roadmap/current.md) | **Only active milestone**, remaining work and exit criteria | Completed checklists or delivery history |
| [Backlog](roadmap/backlog.md) | Inactive candidates, dependencies, deferred scope | Active tasks or implementation status |
| [Changelog](../CHANGELOG.md) | Dated delivery history and release records | Live capability status |

## Contract labels

Sections named **Implemented ... contract/boundary/algorithm** describe identifiable source APIs, with header links. Sections named **Proposed ...** describe designs to validate, not usable APIs. Architectural direction is a constraint; an open decision is unresolved. These labels distinguish contract maturity without turning each page into a progress dashboard. Implementation and validation vocabulary is defined only in the [support matrix](reference/support-matrix.md#evidence-vocabulary).

## Change routing

- Contract change: update its owner and link to it from consumers. Keep exact rules, fields, defaults, and lifetimes in that owner.
- Scope change: edit current or backlog. When a candidate becomes active, move its complete scope to current; leave a link in backlog.
- Delivery: remove finished tasks from current, append history to the changelog, and update affected support rows with source/evidence. Contract pages change only when their contract or maturity changes.
- Validation: update support evidence only. Build instructions change only when the procedure changes.
- Index/README: change for navigation or ownership, not each implementation slice.

Do not copy milestone tables, rolling `Status:` summaries, toolchain results, or delivered checklists into indexes, guides, or design introductions. An API limitation belongs with its contract; the fact that a whole capability is absent belongs in support. Historical evidence is read in its dated context.

Run [documentation checks](guides/testing.md#documentation-verification) after edits. They check links/anchors, code fences, required ownership links, and common forms of duplicate status tracking. Review semantic ownership as well; a checker cannot prove that prose passages express the same contract.
