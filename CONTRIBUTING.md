# Contributing

Read [current work](docs/roadmap/current.md) and the code/docs near your change. [Architecture](docs/architecture/overview.md) states the boundaries to keep: layout independently testable, UI documents backend-neutral, GPU/platform dependencies outside the core.

Deliver a feature whole: the widget or API, its example wiring, its tests and the owning document in one pull request. Design pages define intended contracts; when implementation resolves a decision, update the owning page rather than copying its contract elsewhere.

## Verification

Follow the [testing guide](docs/guides/testing.md) and the commands in the [development guide](docs/guides/development.md). For documentation-only changes, run the documentation checker.

## Keep plans and evidence separate

- [Current](docs/roadmap/current.md) contains remaining active work; remove completed tasks.
- [Backlog](docs/roadmap/backlog.md) contains inactive candidates and deferred work.
- [Support matrix](docs/reference/support-matrix.md) records implemented and validated capabilities.
- [Changelog](CHANGELOG.md) owns dated delivery history.
