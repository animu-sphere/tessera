# Intended use cases

These are intended consumers, not support claims or implementation tasks. Consult the [support matrix](../reference/support-matrix.md) for capability evidence.

The retained, declarative component model targets dense application UI, including inspectors, timelines, data grids, node graphs, conversational/agent workspaces, and UI alongside 3D viewports. [Graph editors](../integrations/graph-editor.md) and [conversational workspaces](../integrations/conversational-ui.md) are reference workloads that stress text, virtualization, input, custom paint, commands, and automation together; they remain consumers of the common runtime rather than separate frameworks. Advanced UI is composed from small runtime primitives instead of a large widget catalog. Source review, reusable composition, predictable updates, live reload, accessibility, and machine observation/operation are design requirements.

Other intended uses include game HUDs and menus, overlays, in-world UI, Mimikuri application UI, Path-finder previews and OpenUSD utility applications. Hosts keep ownership of their application state, scenes and external services.

Consumer designs:

- [Path-finder](../integrations/path-finder.md): shared-schema authoring and runtime preview.
- [Graph editors](../integrations/graph-editor.md): domain-owned graph data presented through ordinary UI boundaries.
- [Conversational workspaces](../integrations/conversational-ui.md): provider-independent structured and streaming application UI.
- Accessibility tools, tests and agents inspect and operate the same semantic/action model under [architecture](../architecture/overview.md#pipeline-and-dependency-direction).

An integration requirement needs explicit promotion to [current work](../roadmap/current.md) before it expands runtime scope.
