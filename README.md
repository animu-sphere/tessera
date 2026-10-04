# Tessera

Tessera is an early lightweight, native UI framework for the animu-sphere ecosystem. Its direction combines web-inspired declarative authoring with a small runtime for real-time applications and GPU rendering.

Intended uses include game HUDs, menus, editor panels, inspectors, overlays, in-world UI, and utility applications. The name refers to small independent pieces that compose into a coherent surface.

## Project status

**Phase 0 in progress — 2026-10-05.** The repository now has a C++20 core with an owned Box/Text tree, typed properties, validation diagnostics, and deterministic JSON v1 load/save. A core-only CMake build, document tests, and the `hello-ui` serialization example are verified on Windows x64/MSVC; see [development](docs/guides/development.md) and [support evidence](docs/reference/support-matrix.md). Layout, input dispatch, text shaping, and rendering are not implemented. Design snippets remain proposals unless explicitly marked implemented.

The next work is the remaining [Phase 0 contracts](docs/roadmap/current.md). The first public milestone candidate is a small Vulkan menu prototype; later milestone candidates are tracked in the [backlog](docs/roadmap/backlog.md). Delivered slices are recorded in the [changelog](CHANGELOG.md).

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
