# Tessera

Tessera is a planned lightweight, native UI framework for the animu-sphere ecosystem. It combines web-inspired declarative authoring with a small runtime designed for real-time applications and GPU rendering.

Intended uses include game HUDs, menus, editor panels, inspectors, overlays, in-world UI, and utility applications. The name refers to small independent pieces that compose into a coherent surface.

## Project status

**Documentation foundation — 2026-10-05.** This repository currently contains design and planning documents. There is no runtime implementation, build system, executable example, or validated platform support yet. Types and code snippets in the design documents are conceptual, not available APIs.

The first implementation work is [Phase 0: foundation](docs/roadmap/current.md). The first public milestone candidate is a small Vulkan menu prototype; later milestone candidates are tracked in the [backlog](docs/roadmap/backlog.md).

## Direction

- A retained UI tree and backend-neutral, versioned document model.
- Independent style, layout, input, text, and rendering layers.
- C++ API and a simple serialized frontend first; optional authoring languages later.
- Vulkan first, WebGPU later, with Slang evaluated below the rendering boundary.
- Keyboard and gamepad interaction designed alongside pointer interaction.
- Explicit ownership, deterministic behavior, useful diagnostics, and reviewable source.

The host application owns windows, application state, input devices, and GPU device lifetime. Tessera owns the UI runtime. Path-finder owns visual authoring and consumes the same document model. See [architecture](docs/design/architecture.md) for the boundaries.

## Documentation

Start at the [documentation index](docs/README.md).

| Topic | Document |
| --- | --- |
| Architecture and ownership | [Architecture](docs/design/architecture.md) |
| Documents, nodes, components, and state | [UI model](docs/design/ui-model.md) |
| Layout algorithms and output | [Layout](docs/design/layout.md) |
| Properties, selectors, and themes | [Styling](docs/design/styling.md) |
| Paint commands, backends, shaders, and assets | [Rendering](docs/design/rendering.md) |
| Shaping, measurement, and glyph resources | [Text](docs/design/text.md) |
| Events, focus, navigation, and accessibility metadata | [Input](docs/design/input.md) |
| Editor bridge and live reload | [Path-finder integration](docs/design/path-finder-integration.md) |
| Development and verification | [Development](docs/guides/development.md), [Testing](docs/guides/testing.md) |
| Current work and later candidates | [Roadmap](docs/roadmap/README.md) |
| Actual support and dependency choices | [Support matrix](docs/reference/support-matrix.md), [Dependencies](docs/reference/dependencies.md) |

See [contributing](CONTRIBUTING.md) before changing the design or starting implementation.
