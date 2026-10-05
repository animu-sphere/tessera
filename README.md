# Tessera

Tessera is a small native declarative UI runtime for the animu-sphere ecosystem. It combines web-inspired authoring with explicit C++ ownership and backend-neutral documents for real-time applications.

Intended uses include game HUDs and menus, editor panels, inspectors, overlays, in-world UI, Mimikuri application UI, Path-finder previews, and OpenUSD utility applications. These are intended consumers, not support claims.

The host owns application state, windows, devices, and frame scheduling. Tessera owns the UI runtime; Path-finder owns authoring and uses the same runtime for preview. The core stays independent of browser engines, GPU/platform SDKs, OpenUSD, and editor SDKs. See [architecture](docs/design/architecture.md).

## Start here

- [Documentation index and ownership](docs/README.md)
- [Implementation and validation status](docs/reference/support-matrix.md)
- [Active work](docs/roadmap/current.md) and [future candidates](docs/roadmap/backlog.md)
- [Build workflow](docs/guides/development.md) and [testing strategy](docs/guides/testing.md)
- [Delivery history](CHANGELOG.md) and [contributing](CONTRIBUTING.md)

The roadmap's version labels are planning candidates. Consult the support matrix for actual capabilities and configurations.

## Community

Follow the [code of conduct](CODE_OF_CONDUCT.md). Report vulnerabilities privately as described in the [security policy](SECURITY.md).

## License

Tessera is licensed under the [Apache License 2.0](LICENSE). Third-party tools and SDKs used by optional modules are listed in [third-party notices](THIRD_PARTY_NOTICES.md).
