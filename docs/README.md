# Documentation

Repository documents are the only maintained plan and contract. Owner-provided direction documents are design inputs: when one is adopted, its content is merged into the owners below (vision and principles into architecture, contracts into subsystem pages, scheduling into current or backlog) and the intake is recorded in the [changelog](../CHANGELOG.md). Do not keep a summary of adopted direction here or link to files outside the repository. Illustrative CLI/build switches, environment variables, DSL syntax, API sketches, package names, directory layouts, speedup estimates, scale targets, and phase numbering in such inputs are not contracts.

## Reading order

1. [Architecture](design/architecture.md): purpose, principles, ownership, dependency direction, and frame lifecycle.
2. [UI model](design/ui-model.md): common representation beneath authoring frontends.
3. [Reactive runtime](design/reactive-runtime.md): proposed dependency, ownership, batching, and effect semantics beneath components.
4. [Current work](roadmap/current.md): active scope and exit criteria.
5. The relevant subsystem contract and [testing strategy](guides/testing.md).

## Canonical owners

| Document | Owns | Does not own |
| --- | --- | --- |
| [Architecture](design/architecture.md) | Purpose and differentiation, core scope, principles, ecosystem boundaries, determinism, update/snapshot lifecycle | Capability status, subsystem rules, or directory inventory |
| [Reactive runtime](design/reactive-runtime.md) | Dependency graph, revisions/equality, reactive owner lifetimes, update batches, evaluation/effect scheduling | Application data/history, component keys, layout/text algorithms, renderer resources, or inspection schema |
| [UI model](design/ui-model.md) | Tree/identity, semantic properties, reflection metadata, components, reconciliation, async state, error boundaries, context, portal ownership, frontend lowering | Encoded fields, editor transport, interaction policy |
| [JSON v1](../formats/tessera-ui/README.md) | Serialized fields, formatting, metadata preservation, rejection and bounds | Future schema proposals |
| [Layout](design/layout.md) | Geometry, logical coordinates/edges and inline direction, sizing, scrolling, positioning, virtualization foundation | Physical-pixel conversion, navigation, or item identity |
| [Styling](design/styling.md) | Resolved values, cascade, inheritance, themes, property effects, animation | Device input or progress tracking |
| [Rendering](design/rendering.md) | Paint vocabulary/order, primitive semantics, backend/resource contract, backend selection, CPU reference backend, pixel conversion, shaders, batching, custom paint, asset boundary | Layout or application state |
| [Text](design/text.md) | Font sources/selection, Unicode segmentation/BiDi, shaping abstraction, line breaking, indices, metrics, fallback, glyph raster strategy and caching, deterministic font profile | Message resolution, editing events, clipboard operations, or font licensing policy |
| [Localization](design/localization.md) | Locale identity, semantic message values, catalogs/fallback, formatting, missing-translation policy, catalog revision/reload | Provider lifetime, Unicode/text algorithms, layout direction, inspection schema, or implementation status |
| [Input](design/input.md) | Normalized events, hit tests, focus/navigation, event-to-action translation, editing/IME/clipboard boundary, typed drag and drop | Semantic schema, command resolution, or OS adapters |
| [Commands](design/commands.md) | Command registry and descriptors, action-to-command resolution, command eligibility, command palette, transaction boundary | Event translation, application implementations, or undo history |
| [Semantics](design/semantics.md) | Semantic projection and owned snapshot schema, roles/names/state/actions, accessibility/automation projection boundary | Focus/dispatch algorithms, inspection protocols, browser mapping, or host action implementation |
| [Replay](design/replay.md) | Versioned recording inputs, deterministic playback and observable outputs | Test procedure or CLI support claims |
| [Inspection and tooling](design/inspection.md) | Shared observation records, source mapping, target resolution, declared state injection, snapshot bundles/diffs, diagnostics envelope, performance metrics, UI invariants, agent capability manifest, generation history, tooling/production boundaries | Semantic schema, dispatch policy, command descriptors, replay inputs, or live tool availability |
| [Web host](design/web-host.md) | WASM/browser ownership, semantic DOM, browser editing and JS bridge, optional worker integration | Core semantics/input algorithms, renderer commands, or browser support claims |
| [Graph editor](design/graph-editor.md) | Graph model/view split, canvas layers, graph coordinate spaces, culling/LOD, port/edge hit rules, graph editing and automation identities | Domain graph schemas, general custom-paint vocabulary, or transform hit-test rules |
| [Conversational UI](design/conversational-ui.md) | Conversational/agent workspace workload, structured content model, streaming/async boundaries, provider independence, reference fixtures | Virtualization geometry, text/editing contracts, or provider adapters |
| [Path-finder integration](design/path-finder-integration.md) | Shared-schema editing, diagnostics, reload transaction, preview bridge | A second property/semantic schema or runtime |
| [Development](guides/development.md) | Build/run commands and workflow | Test-result history or subsystem contracts |
| [Testing](guides/testing.md) | Verification methods and documentation checks | Test-target inventory or capability status |
| [Dependencies](reference/dependencies.md) | Adopted/candidate choices, acquisition and licensing policy, including font licensing | Validated configuration claims |
| [Support matrix](reference/support-matrix.md) | **Only live implementation/validation status**, configurations and evidence | Work scheduling, contract definitions, or rows for capabilities without implementation |
| [Roadmap index](roadmap/README.md) | Planning conventions and links | A second milestone/status table |
| [Current](roadmap/current.md) | **Only active milestone**, remaining work and exit criteria | Completed checklists or delivery history |
| [Backlog](roadmap/backlog.md) | Inactive candidates, dependencies, deferred scope | Active tasks or implementation status |
| [Changelog](../CHANGELOG.md) | Dated delivery history and release records | Live capability status |

## Contract labels

Sections named **Implemented ... contract/boundary/algorithm** describe identifiable source APIs, with header links. Sections named **Proposed ...** describe designs to validate, not usable APIs. Architectural direction is a constraint; an open decision is unresolved. These labels distinguish contract maturity without turning each page into a progress dashboard. Implementation and validation vocabulary is defined only in the [support matrix](reference/support-matrix.md#evidence-vocabulary).

## Status and wording rules

Status drifts when it is written in more than one place, so it is written only in the support matrix. Everywhere else:

- Maturity is expressed only by section labels (**Implemented** or **Proposed**). Do not add progress words such as "currently", "yet", "not yet", "still needs", "remains a proposal", "is planned", or "no X exists" to design pages, guides, or indexes; a checker rejects the common forms.
- State an implemented API's limits as permanent facts of that contract ("the resolver has no theme layer"), and link the proposal that would extend it. When the limit is lifted, the owning section changes in the same commit as the code.
- Design pages do not name versions, milestones, dates, or test results. Scheduling lives in current/backlog; evidence lives in support.
- **Verification** sections state what must be verified and link the check files. They do not list missing fixtures or report outcomes.
- Roadmap pages describe scope and exit criteria and link contracts; they do not describe how a capability currently behaves.
- Support rows give status, claim scope, and evidence links in one row; details go in dated evidence sections. Capabilities without implementation have no row.

## Change routing

- Contract change: update its owner and link to it from consumers. Keep exact rules, fields, defaults, and lifetimes in that owner.
- Scope change: edit current or backlog. When a candidate becomes active, move its complete scope to current; leave a link in backlog.
- Delivery: remove finished tasks from current, append history to the changelog, and update affected support rows with source/evidence. Contract pages change only when their contract or maturity changes.
- Validation: update support evidence only. Build instructions change only when the procedure changes.
- Index/README: change for navigation or ownership, not each implementation slice.

Do not copy milestone tables, rolling `Status:` summaries, toolchain results, or delivered checklists into indexes, guides, or design introductions. An API limitation belongs with its contract; the fact that a whole capability is absent belongs in support. Historical evidence is read in its dated context.

Run [documentation checks](guides/testing.md#documentation-verification) after edits. They check links/anchors, code fences, required ownership links, and common forms of duplicate status tracking. Review semantic ownership as well; a checker cannot prove that prose passages express the same contract.
