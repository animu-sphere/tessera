# Documentation

These documents organize the owner-provided **Tessera Implementation Strategy**, draft dated 2026-10-04. They preserve its architectural direction while separating subsystem contracts, implementation order, and evidence. Repository status was checked on 2026-10-05: Phase 0 tree/document/serialization and subsystem boundary contracts, the Phase 1 fixed/stack/flex layout prototype, backend-neutral background/border/Text paint generation, and rectangular pointer targeting/activation implemented; the v0.1.0 Vulkan menu candidate is active, with backend and native host work remaining.

## Reading order

1. [Architecture](design/architecture.md): purpose, ownership, dependency direction, and host integration.
2. [UI model](design/ui-model.md): common representation beneath every authoring frontend.
3. [Current work](roadmap/current.md): the foundation to implement next.
4. The relevant subsystem design and [testing strategy](guides/testing.md).

## Canonical documents

| Document | Owns |
| --- | --- |
| [Architecture](design/architecture.md) | Long-term constraints, ecosystem responsibilities, frame scheduling, proposed repository layout |
| [UI model](design/ui-model.md) | IR, serialization, properties, components, state, reconciliation, authoring frontend order |
| [JSON v1](../formats/tessera-ui/README.md) | Implemented serialized fields, formatting, metadata preservation, rejection and bounds |
| [Layout](design/layout.md) | Layout inputs/output, algorithm order, sizing, scrolling |
| [Styling](design/styling.md) | Property vocabulary, selectors, cascade, inheritance, pseudo states, themes, animation |
| [Rendering](design/rendering.md) | Paint commands, backend contract, GPU integration, Slang, batching, asset boundary |
| [Text](design/text.md) | Font/shaping abstraction, measurement, fallback, glyph caching |
| [Input](design/input.md) | Normalized events, hit testing, propagation, focus, navigation, accessibility metadata |
| [Path-finder integration](design/path-finder-integration.md) | Shared document editing, metadata, diagnostics, reload transaction |
| [Development](guides/development.md) | Current setup status and future build workflow requirements |
| [Testing](guides/testing.md) | High-value checks and milestone evidence |
| [Dependencies](reference/dependencies.md) | Candidate dependencies, module boundaries, adoption requirements |
| [Support matrix](reference/support-matrix.md) | Implemented capabilities and validated configurations only |
| [Roadmap index](roadmap/README.md) | Phase sequence and planning conventions |
| [Current](roadmap/current.md) | Active and immediately next work with exit criteria |
| [Backlog](roadmap/backlog.md) | Inactive milestones, dependencies, and deferred scope |

## Status vocabulary

- **Direction**: an architectural constraint inherited from the strategy.
- **Proposed contract**: a concrete design to validate during implementation; not a stable public API.
- **Open decision**: a choice that still needs implementation or integration evidence.
- **Planned**: a capability with no implementation evidence yet.
- **Implemented / validated**: use only with identifiable code and verification evidence.

Design pages distinguish implemented foundation contracts from draft plans. Type names and snippets are illustrative unless explicitly promoted to implemented API documentation. Version labels in the roadmap are candidates, not release promises. Dates do not imply platform validation.

## Maintenance

Define a contract in one owning document and link to it elsewhere. Current work must not become a delivery history. The root [changelog](../CHANGELOG.md) records delivered implementation slices; no public release has been made.

The organization follows the separation used by [hydra-merlin](https://github.com/animu-sphere/hydra-merlin): [design](https://github.com/animu-sphere/hydra-merlin/blob/main/docs/design/renderer-architecture.md), [current work](https://github.com/animu-sphere/hydra-merlin/blob/main/docs/roadmap/current.md), [backlog](https://github.com/animu-sphere/hydra-merlin/blob/main/docs/roadmap/backlog.md), and [support reference](https://github.com/animu-sphere/hydra-merlin/blob/main/docs/reference/support-matrix.md). Its renderer-specific contracts, dependency versions, and support claims do not apply to Tessera.
