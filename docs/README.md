# Documentation

Repository documents are the maintained plan and contract. When an external direction document is adopted, merge its content into the owners below and record the intake in the [changelog](../CHANGELOG.md); do not link to files outside the repository.

## Consult by change

For application authors, begin with [Getting Started](guides/getting-started.md) and [your first app](guides/first-app.md), then follow the Settings workflow. For repository changes, read [current work](roadmap/current.md) and the code/docs near the change; the rest are references as needed.

- Code changes: the affected subsystem contract and the [testing guide](guides/testing.md#change-selection).
- Structural/dependency changes: [architecture](architecture/overview.md); product intent: [vision](product/vision.md).
- Support claims, evidence or release acceptance: [support matrix](reference/support-matrix.md).
- Documentation moves/classification: the canonical owners below.

## Directories

| Directory | Purpose |
| --- | --- |
| [Product](product/) | Why Tessera exists, principles, intended consumers and non-goals |
| [Roadmap](roadmap/) | Active milestone and inactive candidates |
| [Architecture](architecture/) | System boundaries, invariants and subsystem contracts; Implemented/Proposed labels distinguish maturity |
| [Reference](reference/) | Capability/configuration evidence and dependency policy |
| [Guides](guides/) | Build, test and operational procedures |
| [Proposals](proposals/) | Possible mechanisms and promotion criteria |
| [Integrations](integrations/) | Consumer designs and requirements |

## Canonical owners

| Document | Owns | Does not own |
| --- | --- | --- |
| [Vision](product/vision.md) | Product purpose and differentiation | Feature scheduling or support claims |
| [Principles](product/principles.md) | Durable product principles | Subsystem algorithms or milestone tasks |
| [Use cases](product/use-cases.md) | Intended consumers and their design entry points | Capability support or implicit core requirements |
| [Non-goals](product/non-goals.md) | Product exclusions and limits on inferred commitments | Milestone scope or implementation evidence |
| [Architecture](architecture/overview.md) | Core scope, ecosystem boundaries, dependency direction, determinism, update/snapshot lifecycle | Product intent, capability status, subsystem algorithms, or directory inventory |
| [Reactive runtime](architecture/reactive-runtime.md) | Dependency graph, revisions/equality, reactive owner lifetimes, update batches, evaluation/effect scheduling | Application data/history, component keys, layout/text algorithms, renderer resources, or inspection schema |
| [UI model](architecture/ui-model.md) | Tree/identity, semantic properties and metadata, components, reconciliation, typed async state, context, logical ownership invariants, frontend lowering | Encoded fields, editor transport, interaction policy, or extracted future lifecycle/resource/reflection mechanisms |
| [JSON v1](../formats/tessera-ui/README.md) | Serialized fields, formatting, metadata preservation, rejection and bounds | Future schema proposals |
| [Layout](architecture/layout.md) | Geometry, logical coordinates/edges and inline direction, sizing, scrolling, positioning, full-tree/virtualized geometry invariants | Physical-pixel conversion, navigation, item identity, or future realization/incremental mechanisms |
| [Styling](architecture/styling.md) | Resolved values, cascade, inheritance, themes, property effects | Device input, progress tracking, or future incremental/animation mechanisms |
| [Rendering](architecture/rendering.md) | Paint vocabulary/order, primitive semantics, backend/resource contract, backend selection, CPU reference backend, pixel conversion, shaders, batching, custom paint, asset boundary | Layout, application state, or future retained-update mechanisms |
| [Text](architecture/text.md) | Font sources/selection, Unicode segmentation/BiDi, shaping abstraction, line breaking, indices, metrics, fallback, glyph raster strategy and caching, deterministic font profile | Message resolution, editing events, clipboard operations, font licensing policy, or incremental-stage mechanisms |
| [Localization](architecture/localization.md) | Locale identity, semantic message values, catalogs/fallback, formatting, missing-translation policy, catalog revision/reload | Provider lifetime, Unicode/text algorithms, layout direction, inspection schema, or implementation status |
| [Input](architecture/input.md) | Normalized events, hit tests, focus/navigation, event-to-action translation, editing/IME/clipboard boundary, typed drag and drop | Semantic schema, command resolution, or OS adapters |
| [Commands](architecture/commands.md) | Command registry and descriptors, action-to-command resolution, command eligibility, transaction boundary | Event translation, application implementations, undo history, or palette mechanisms |
| [Semantics](architecture/semantics.md) | Semantic projection and owned snapshot schema, roles/names/state/actions, accessibility/automation projection boundary | Focus/dispatch algorithms, inspection protocols, browser mapping, or host action implementation |
| [Replay](architecture/replay.md) | Versioned recording inputs, deterministic playback and observable outputs | Test procedure or CLI support claims |
| [Inspection and tooling](architecture/inspection.md) | Shared observation records, source mapping, target resolution, declared state injection, diagnostics/comparisons, performance metrics, UI invariants, tooling/production boundaries | Semantic schema, dispatch policy, command descriptors, replay inputs, live tool availability, or bundle/discovery/history mechanisms |
| [Graph editor](integrations/graph-editor.md) | Graph model/view split, canvas layers, graph coordinate spaces, culling/LOD, port/edge hit rules, graph editing and automation identities | Domain graph schemas, general custom-paint vocabulary, or transform hit-test rules |
| [Conversational UI](integrations/conversational-ui.md) | Conversational/agent workspace workload, structured content model, streaming/async boundaries, provider independence, reference fixtures | Virtualization geometry, text/editing contracts, or provider adapters |
| [Path-finder integration](integrations/path-finder.md) | Shared-schema editing consumer, source-consumption boundary, diagnostics and preview bridge requirements | A second property/semantic schema or runtime, reload mechanisms, or active scope |
| [Settings application](integrations/settings-app.md) | Proposed ordinary-authoring reference workflow, host interaction and consumer verification requirements | Public API syntax, implementation status or active scheduling |
| [Renderer diagnostics](integrations/renderer-diagnostics.md) | Telemetry consumer contract, proposed control panel, host snapshot/command ownership and verification requirements | Renderer SDKs, a second paint/input model, implementation status or active scheduling |
| [Development](guides/development.md) | Build/run commands and workflow | Test-result history or subsystem contracts |
| [Getting Started](guides/getting-started.md) | First-app build/copy/run procedure and prerequisites | Capability evidence or future syntax |
| [Your first app](guides/first-app.md) | Source walkthrough, composition and explicit host responsibilities | A second API/lifetime contract or test-result history |
| [Settings starter](guides/settings-starter.md) | Copy/build/run and source walkthrough for controlled editing and owned Apply copies | A second API/consumer contract, expanded widget scope or test-result history |
| [SplitPane guide](guides/split-pane.md) | Public C++ composition and explicit host adjustment wiring | A second layout/input/semantic contract or test-result history |
| [Stack guide](guides/stack.md) | Public in-parent overlay composition and viewport toolbar procedure | A second layout/input contract, portals, modal behavior or test-result history |
| [Grid guide](guides/grid.md) | Public Inspector property columns, narrow inputs and controlled-edit procedure | A second layout/input contract, advanced Grid placement or test-result history |
| [Popup/Tooltip guide](guides/popup-tooltip.md) | Root-sibling anchored composition and explicit host interaction procedure | A second layout/input contract, generalized Portal/modal behavior or test-result history |
| [Modal/Dialog guide](guides/modal-dialog.md) | Single root-modal confirmation composition, scrolling and host dismissal/focus recovery | A second layout/input/semantic contract, stacked modals, Portal or test-result history |
| [TreeView/ListView guide](guides/tree-list.md) | Controlled collection composition, browser selection and host navigation/scroll wiring | Multiple selection, virtualization, a second semantic contract or test-result history |
| [Renderer telemetry guide](guides/renderer-diagnostics.md) | Copy/build/run and source walkthrough for telemetry composition and the synthetic snapshot host | A second API/consumer contract, native renderer integration or test-result history |
| [Testing](guides/testing.md) | Verification responsibilities, owner-test references, tier/selection and measurement policy, documentation checks | Complete test-target inventory, workflow implementation or capability status |
| [Dependencies](reference/dependencies.md) | Adopted/candidate choices, acquisition and licensing policy, including font licensing | Validated configuration claims |
| [Support matrix](reference/support-matrix.md) | **Only live implementation/validation status**, configurations and evidence | Work scheduling, contract definitions, or rows for capabilities without implementation |
| [Roadmap index](roadmap/README.md) | Planning conventions and links | A second milestone/status table |
| [Current](roadmap/current.md) | **Only active milestone**, remaining work and exit criteria | Completed checklists or delivery history |
| [Backlog](roadmap/backlog.md) | Inactive candidates, dependencies, deferred scope | Active tasks or implementation status |
| [Changelog](../CHANGELOG.md) | Dated delivery history and release records | Live capability status |

## Proposal owners

Proposals own possible mechanisms and promotion criteria.

| Document | Owns | Does not own |
| --- | --- | --- |
| [Automatic component lifecycle](proposals/automatic-component-lifecycle.md) | Proposed mechanisms and promotion criteria for automatic component lifecycle | Active scope or implementation evidence |
| [C++ authoring](proposals/cpp-authoring.md) | Proposed public authoring, state/component/widget/style conveniences and promotion criteria | A second runtime/style model, settled syntax, scheduling or support claims |
| [Async resources](proposals/async-resources.md) | Proposed mechanisms and promotion criteria for async resources | Active scope or implementation evidence |
| [UI error boundaries](proposals/error-boundaries.md) | Proposed mechanisms and promotion criteria for UI error boundaries | Active scope or implementation evidence |
| [Property reflection](proposals/property-reflection.md) | Proposed mechanisms and promotion criteria for property reflection | Active scope or implementation evidence |
| [Overlays and portals](proposals/overlays-and-portals.md) | Proposed mechanisms and promotion criteria for overlays and portals | Active scope or implementation evidence |
| [Live reload transactions](proposals/live-reload.md) | Proposed mechanisms and promotion criteria for live reload transactions | Active scope or implementation evidence |
| [Virtualization](proposals/virtualization.md) | Proposed mechanisms and promotion criteria for virtualization | Active scope or implementation evidence |
| [Incremental evaluation](proposals/incremental-evaluation.md) | Proposed cross-stage invalidation, text cache dependencies and retained-render mechanisms, with promotion criteria | Implemented subsystem algorithms, active scope, or implementation evidence |
| [Command palette](proposals/command-palette.md) | Proposed mechanisms and promotion criteria for command palette | Active scope or implementation evidence |
| [Animation system](proposals/animation.md) | Proposed mechanisms and promotion criteria for animation system | Active scope or implementation evidence |
| [Snapshot bundles and CLI](proposals/snapshot-bundles.md) | Proposed bundle contents and one-shot consumer, with promotion criteria | In-process runner contract, active scope, or implementation evidence |
| [Agent capability manifest](proposals/agent-capability-manifest.md) | Proposed mechanisms and promotion criteria for agent capability manifest | Active scope or implementation evidence |
| [Generation history](proposals/generation-history.md) | Proposed mechanisms and promotion criteria for generation history | Active scope or implementation evidence |
| [Web host](proposals/web-host.md) | Proposed WASM/browser ownership, semantic DOM, browser editing/JS bridge and optional worker integration, with promotion criteria | Core algorithms, active scope, or browser support claims |

## Contract labels

Sections named **Implemented ...** describe source-backed APIs with header links. Sections named **Proposed ...** describe designs that are not yet implemented. Implementation and validation vocabulary is defined in the [support matrix](reference/support-matrix.md#evidence-vocabulary).

## Change routing

1. **Contracts and procedures:** update the owner whose public contract or procedure changes; link to it from consumers instead of copying.
2. **Scope:** current owns the active milestone; backlog owns inactive candidates. Remove completed work from current.
3. **Support and evidence:** update support when a capability, support claim or material validation evidence changes. Routine check results belong in the PR.
4. **Delivery:** changelog owns deliveries and release history.

Run the [documentation checks](guides/testing.md#documentation-verification) after edits; they check links, anchors, code fences and ownership links. Documentation-only changes require no runtime tests.
