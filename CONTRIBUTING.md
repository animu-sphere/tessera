# Contributing

Read the [documentation index](docs/README.md), [architecture](docs/design/architecture.md), and [current work](docs/roadmap/current.md) before making changes.

## Scope changes around a boundary

Prefer small changes with a concrete input, output, and owner. Keep layout independently testable, UI documents backend-neutral, and GPU/platform dependencies outside the core. Use composition and explicit lifetimes rather than global registries or deep inheritance.

Design pages define intended contracts. Mark unresolved alternatives as proposals or open decisions. Conceptual snippets must not be presented as working examples. When implementation resolves a decision, update the owning page rather than copying its contract into other documents.

## Verification

For documentation changes, run the documentation checker and review canonical ownership under the [testing guide](docs/guides/testing.md#documentation-verification). For code changes, follow the [testing strategy](docs/guides/testing.md) and the verified commands in the [development guide](docs/guides/development.md).

A change description should state the resulting behavior, relevant boundary, verification performed, and unresolved limitations. Support claims require reproducible evidence in the [support matrix](docs/reference/support-matrix.md).

## Keep plans and evidence separate

- [Current](docs/roadmap/current.md) contains remaining active work; remove completed tasks.
- [Backlog](docs/roadmap/backlog.md) contains inactive candidates and deferred work.
- [Support matrix](docs/reference/support-matrix.md) records implemented and validated capabilities.
- [Changelog](CHANGELOG.md) owns dated delivery history. Follow the [change-routing rules](docs/README.md#change-routing); indexes and design introductions do not repeat status.
