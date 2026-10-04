# Tessera

Tessera is an early lightweight, native UI framework for the animu-sphere ecosystem. Its direction combines web-inspired declarative authoring with a small runtime for real-time applications and GPU rendering.

Intended uses include game HUDs, menus, editor panels, inspectors, overlays, in-world UI, and utility applications. The name refers to small independent pieces that compose into a coherent surface.

## Project status

**Paint generation and pointer dispatch delivered — 2026-10-05.** The repository has a C++20 core with an owned Box/Text tree, typed properties, validation diagnostics, and deterministic JSON v1 load/save, plus validated contracts for resolved styles and layout boxes, normalized input events and action requests, a draw list and frame submission, text measurement with a deterministic placeholder shaper, and a deterministic fixed/stack/flex layout prototype that turns a tree plus resolved styles into `LayoutBox` geometry. `build_paint_list` converts that geometry and resolved styles into owned background, border, and placeholder glyph commands in tree order. Rectangular hit testing and `PointerDispatcher` handle hover, primary clicks, cancellation, and host action requests. A core-only CMake build, contract/document/layout/paint/pointer tests, and the `hello-ui`, `flex-layout`, and synthetic `pointer-menu` examples are verified on Windows x64/MSVC; see [development](docs/guides/development.md) and [support evidence](docs/reference/support-matrix.md). Style resolution, focus/navigation dispatch, real text shaping, native input, and GPU rendering are not implemented. Design snippets remain proposals unless explicitly marked implemented.

Active work is the [v0.1.0 minimal Vulkan menu candidate](docs/roadmap/current.md); later milestone candidates are tracked in the [backlog](docs/roadmap/backlog.md). Delivered slices are recorded in the [changelog](CHANGELOG.md).

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
