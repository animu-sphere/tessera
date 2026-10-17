# Documentation

Repository documents are the only maintained plan and contract. Owner-provided direction documents are design inputs: when one is adopted, its content is merged into the owners below (intent into product, constraints into architecture, possible mechanisms into proposals, consumer requirements into integrations, scheduling into current or backlog) and the intake is recorded in the [changelog](../CHANGELOG.md). Do not keep a summary of adopted direction here or link to files outside the repository. Illustrative CLI/build switches, environment variables, DSL syntax, API sketches, package names, directory layouts, speedup estimates, scale targets, and phase numbering in such inputs are not contracts.

## Reading order

For application authors, begin with [Getting Started](guides/getting-started.md) and [your first app](guides/first-app.md), then follow the Settings workflow. The order below is for implementation and contract work.

1. [Agent implementation policy](agent-policy.md): implementation authority, fixture-driven work, and subsystem stop rule.
2. [Current work](roadmap/current.md): one integration target, bounded supporting work, and observable exit criteria.
3. [Support matrix](reference/support-matrix.md): implementation and validation evidence.
4. [Architecture](architecture/overview.md): subsystem ownership, dependency direction, and frame lifecycle; [product vision](product/vision.md) explains the intended product.
5. The relevant subsystem contract and [testing strategy](guides/testing.md); start with the [UI model](architecture/ui-model.md) and [reactive runtime](architecture/reactive-runtime.md) when component or update boundaries are affected.

## Documentation authority

| Directory | Purpose | Authority |
| --- | --- | --- |
| [Product](product/) | Why Tessera exists, principles, intended consumers and non-goals | Product intent; no implementation queue |
| [Roadmap](roadmap/) | Active fixture and inactive candidates | Only current scope authorizes repository-planned feature work |
| [Architecture](architecture/) | System boundaries, invariants and subsystem contracts | Constraints; Implemented/Proposed labels distinguish maturity |
| [Reference](reference/) | Capability/configuration evidence and dependency policy | Support matrix is the only live implementation/validation record |
| [Guides](guides/) | Build, test and supported operational procedures | Procedures; no future API design |
| [Proposals](proposals/) | Possible mechanisms and promotion criteria | No independent implementation authority |
| [Integrations](integrations/) | Consumer designs and requirements | No implicit new core requirements |

Detailed Proposed sections are intentionally non-actionable unless the active fixture requires the specific behavior. Backlog candidates and integration examples do not expand active scope. The [agent policy](agent-policy.md) owns the authority and stop rules; current work owns the concrete slice. Follow links for constraints without treating the linked page as a task list.

## Canonical owners

| Document | Owns | Does not own |
| --- | --- | --- |
| [Agent policy](agent-policy.md) | Implementation authority, fixture-driven task selection, subsystem stop rule | Feature scheduling, subsystem contracts, or capability evidence |
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
| [Renderer telemetry guide](guides/renderer-diagnostics.md) | Copy/build/run and source walkthrough for telemetry composition and the synthetic snapshot host | A second API/consumer contract, native renderer integration or test-result history |
| [Testing](guides/testing.md) | Verification methods and documentation checks | Test-target inventory or capability status |
| [Dependencies](reference/dependencies.md) | Adopted/candidate choices, acquisition and licensing policy, including font licensing | Validated configuration claims |
| [Support matrix](reference/support-matrix.md) | **Only live implementation/validation status**, configurations and evidence | Work scheduling, contract definitions, or rows for capabilities without implementation |
| [Roadmap index](roadmap/README.md) | Planning conventions and links | A second milestone/status table |
| [Current](roadmap/current.md) | **Only active milestone**, remaining work and exit criteria | Completed checklists or delivery history |
| [Backlog](roadmap/backlog.md) | Inactive candidates, dependencies, deferred scope | Active tasks or implementation status |
| [Changelog](../CHANGELOG.md) | Dated delivery history and release records | Live capability status |

## Proposal owners

Proposals own possible mechanisms and promotion criteria. Their detailed sections constrain future choices without granting implementation authority.

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

Sections named **Implemented ... contract/boundary/algorithm** describe identifiable source APIs, with header links. Sections named **Proposed ...** describe designs to validate, not usable APIs or scheduled work. Accepted architectural direction constrains future choices; open decisions are exploration and must not be implemented directly as settled contracts. Keep the Implemented/Proposed labels distinct from implementation authority: neither a detailed mechanism nor an unresolved idea authorizes work without the active roadmap's specific requirement. Implementation and validation vocabulary is defined only in the [support matrix](reference/support-matrix.md#evidence-vocabulary).

Design roles complement those maturity labels: **Contract** names normative invariants and the source-backed Implemented sections, **Direction** names Proposed architectural intent without scheduling, and **Exploration** names unresolved API/semantic choices. Architecture pages open with core invariants and a reading rule. Proposals open with their Proposed/Direction role, no independent authority, the default of no scheduled work, and promotion criteria. These are authority markers, not live capability status; actual scheduled behavior is defined only in current work.

## Status and wording rules

Status drifts when it is written in more than one place, so it is written only in the support matrix. Everywhere else:

- Maturity is expressed by section labels (**Implemented** or **Proposed**) and the proposal authority marker above. Do not add progress words such as "currently", "yet", "not yet", "still needs", "remains a proposal", "is planned", or "no X exists" to architecture, proposal, integration, product, guide or index pages; a checker rejects the common forms.
- State an implemented API's limits as permanent facts of that contract ("the resolver has no theme layer"), and link the proposal that would extend it. When the limit is lifted, the owning section changes in the same commit as the code.
- Architecture, proposal and integration pages do not name versions, milestones, dates, or test results. Scheduling lives in current/backlog; evidence lives in support.
- **Verification** sections state what must be verified and link the check files. They do not list missing fixtures or report outcomes.
- Roadmap pages describe scope and exit criteria and link contracts; they do not describe how a capability currently behaves.
- Support rows give status, claim scope, and evidence links in one row; details go in dated evidence sections. Capabilities without implementation have no row.

## Change routing

- Contract change: update its owner and link to it from consumers. Keep exact rules, fields, defaults, and lifetimes in that owner. When extracting a proposal, retain normative invariants and source-backed contracts in architecture, preserve the possible mechanism and its open decisions in the proposal, and retarget links to the owning section. Integrations cannot silently add core requirements.
- Scope change: edit current or backlog. Promote only the behavior required by an integration fixture, with observable acceptance and a stop condition; leave broader candidate scope in backlog. When a whole candidate becomes active, move its complete scope to current and leave a link in backlog. Do not maintain the same active task in both.
- Delivery: remove finished tasks from current, append history to the changelog, and update affected support rows with source/evidence. Contract pages change only when their contract or maturity changes.
- Validation: update support evidence only. Build instructions change only when the procedure changes.
- Index/README: change for navigation or ownership, not each implementation slice.

Do not copy milestone tables, rolling `Status:` summaries, toolchain results, or delivered checklists into indexes, guides, or design introductions. An API limitation belongs with its contract; the fact that a whole capability is absent belongs in support. Historical evidence is read in its dated context.

Run [documentation checks](guides/testing.md#documentation-verification) after edits. They check links/anchors, code fences, required ownership links, and common forms of duplicate status tracking. Review semantic ownership as well; a checker cannot prove that prose passages express the same contract.
