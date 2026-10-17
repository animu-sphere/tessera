# Tessera

Tessera is a deterministic GPU-native UI runtime for humans and agents, built for the animu-sphere ecosystem. It combines declarative, backend-neutral documents with explicit C++ ownership, and is designed so that people and machines inspect, operate, replay, and verify the same UI through one semantic model.

The product goal is to make ordinary application UI easy to author through a high-level C++ surface and composed widgets, backed by that same runtime. See the [authoring direction](docs/product/vision.md) and [active reference-application scope](docs/roadmap/current.md); usable capabilities and validation evidence are recorded in the support matrix below.

Intended uses include game HUDs and menus, editor panels, inspectors, overlays, in-world UI, Mimikuri application UI, Path-finder previews, and OpenUSD utility applications. These are intended consumers, not support claims.

The host owns application state, windows, devices, and frame scheduling. Tessera owns the UI runtime; Path-finder owns authoring and uses the same runtime for preview. The core stays independent of browser engines, GPU/platform SDKs, OpenUSD, and editor SDKs. See [architecture](docs/architecture/overview.md).

## Start here

- [Getting Started](docs/guides/getting-started.md) and [your first app](docs/guides/first-app.md): build and copy a small C++ form with an explicit host boundary.
- [Documentation index and ownership](docs/README.md)
- [Product vision](docs/product/vision.md), [principles](docs/product/principles.md), [use cases](docs/product/use-cases.md) and [non-goals](docs/product/non-goals.md)
- [Agent implementation policy](docs/agent-policy.md): authority, fixture-driven work, and subsystem stop rule
- [Implementation and validation status](docs/reference/support-matrix.md)
- [Active work](docs/roadmap/current.md) and [future candidates](docs/roadmap/backlog.md)
- [Build workflow](docs/guides/development.md) and [testing strategy](docs/guides/testing.md)
- [Delivery history](CHANGELOG.md) and [contributing](CONTRIBUTING.md)

The roadmap's version labels are planning candidates. Only active roadmap scope authorizes repository-planned feature work; detailed designs and proposals do not schedule implementation. Consult the support matrix for actual capabilities and configurations.

## Community

Follow the [code of conduct](CODE_OF_CONDUCT.md). Report vulnerabilities privately as described in the [security policy](SECURITY.md).

## License

Tessera is licensed under the [Apache License 2.0](LICENSE). Third-party tools and SDKs used by optional modules are listed in [third-party notices](THIRD_PARTY_NOTICES.md). Tessera grants no rights to third-party fonts that applications load; see [font licensing](docs/reference/dependencies.md#font-licensing).
