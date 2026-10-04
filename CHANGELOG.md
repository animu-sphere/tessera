# Changelog

## Unreleased — 2026-10-05

- Added the first Phase 0 implementation slice: C++20 `tessera::core`, owned Box/Text documents, immutable indexed tree snapshots, typed properties, action-name validation, and actionable diagnostics.
- Added deterministic JSON v1 load/save with namespaced editor metadata, UTF-8 checks, finite binary64 values, and bounded input/nesting.
- Added a core-only serialization example and focused document/failure/lifetime checks. Verification configuration and limitations are recorded in the [support matrix](docs/reference/support-matrix.md).

No public release, layout engine, input dispatcher, text shaper, or renderer is delivered yet. Remaining foundation contracts are tracked in [current work](docs/roadmap/current.md).
