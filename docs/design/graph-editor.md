# Graph canvas and node editor

## Proposed scope

Tessera should provide a domain-agnostic graph canvas for node-based editors in the style of Blender nodes, Unreal Blueprint, MaterialX graph editors, or Node-RED. It is a thin layer over ordinary components, layout, input, and custom paint, not a separate UI system. This page proposes a design; no graph API exists, and capability evidence belongs in [support](../reference/support-matrix.md).

Intended domains include material graphs (UsdShade, MaterialX, OpenPBR), USD composition, render-pass graphs, motion/retargeting pipelines, behavior graphs and state machines, physics, avatar control, and agent workflows. Each domain is application-owned and reaches Tessera through view models, as defined by [architecture](architecture.md#ecosystem-ownership); OpenUSD and domain types never enter the graph layer.

```text
application graph domain (material, motion, behavior, ...)
        |  view model
graph layer: canvas, nodes, ports, edges, selection, viewport
        |
custom paint primitives: paths, curves
        |
core: UI tree, events/actions, layout, paint
```

## Proposed model and view separation

The graph model holds semantics only: nodes with stable IDs, types, properties, and typed input/output ports, plus edges between ports. It contains no pixel coordinates, fonts, colors, hover, selection, GPU resources, or widget state. The application owns it and its schema.

Connection validity is a domain decision. A domain compatibility query reports compatible, convertible, invalid, cycle, or multiplicity violation; the canvas presents the result but never decides it.

Graph view state is separate: node positions in graph space, selection, viewport transform, collapsed state, frame/group placement, and transient interaction state. One model can then back a desktop editor, a Web editor, a read-only viewer, a minimap, or a debug view. View state persisted by a tool uses its own format version, independent of the domain schema version; Tessera fixes neither file format. Exposure to tooling follows host-declared state slots under [inspection](inspection.md#proposed-inspection-records-and-source-mapping).

## Proposed canvas structure

```text
GraphCanvas
  background grid
  edge layer
  node layer
  overlay: selection rectangle, temporary connection, context menu, search, minimap
```

The canvas owns pan/zoom, coordinate conversion, spatial queries, selection, dragging, edge creation, hit testing, realization of visible items, and layer order.

- **Nodes** are ordinary container components with a header, input ports, content, and output ports. Content can use any component (labels, numeric fields, sliders, toggles, color controls, thumbnails, plots, small previews) with ordinary style, layout, focus, and semantics.
- **Ports** are interactive primitives: hit target, connection anchor, type, connection state, and drag source/drop target. Their hit area may exceed the visual bounds, for example a 5 px visual radius with a 10 px hit radius.
- **Edges** are canvas paint primitives, not one component per edge. Visible edges are tessellated into shared geometry through the [custom paint](rendering.md#batching) vocabulary so many edges batch into few draws. Cubic Bézier curves are the default, with control points offset along the port tangents; straight, quadratic, and orthogonal routes are alternatives.

Groups/frames may be view metadata or domain semantics depending on the domain. Nested subgraphs are a later extension.

## Proposed coordinate spaces and navigation

Graph space holds logical node positions; the viewport transform maps it to the canvas's logical UI space, which [rendering](rendering.md#coordinate-conversion) maps to physical pixels. Zoom changes only the viewport transform, never the model. Wheel zoom keeps the graph point under the cursor fixed; the zoom range (for example 0.05x to 8x) is declared.

This requires the transformed hit-test mapping that [layout](layout.md#coordinate-spaces) defers: paint and pointer targeting must share the same transform. Navigation covers middle-drag and space-drag panning, wheel and pinch zoom, touch pan, fit-to-graph, and focus-selection.

## Proposed visibility, culling, and LOD

Design for a visible set from the start: a spatial index (spatial hash or R-tree first) answers viewport queries, and only visible nodes are realized as components and painted. Edges are culled using endpoint visibility and bounding boxes; the in-progress connection always draws. Full realization remains the correctness reference: a culled frame must match the full frame within the viewport, as required by [architecture](architecture.md#frame-scheduling).

Zoom-dependent level of detail is a standard canvas feature, for example full content near 100%, title and ports near 30%, and a category-colored rectangle near 10%. Low-zoom text is culled and unchanged node text reuses cached layout and glyphs. Zoomable text is a motivating case for distance-field glyphs in [text](text.md#measurement-and-rendering-agreement). The minimap reuses the view state and draws simplified node rectangles and the viewport frame only.

## Proposed hit testing

Nodes use ordinary hit testing. Ports use their enlarged hit areas. Edges test distance to the curve in three steps: bounding box, coarse segments, exact distance.

## Proposed interaction and editing

- **Selection:** click, modifier toggle/add, rectangle, lasso, select all, invert. Selection is view state.
- **Moving:** dragging moves the selection snapshot by a graph-space delta; grid snap, alignment guides, and spacing are optional.
- **Connecting:** a port drag shows a temporary edge, queries domain compatibility on hover, and connects on drop. Valid, convertible, and invalid feedback uses theme tokens rather than fixed colors.
- **Creation:** a context action on empty canvas, or releasing a connection on empty space, opens a search palette filtered by compatible port types and indexed by name, category, description, aliases, and tags.
- **Context menus:** per canvas, node, port, edge, and group, using the [overlay model](ui-model.md#proposed-overlays-and-portals).
- **Keyboard and gamepad:** next node, directional neighbor, edit, delete, select all, copy/paste, and focus-selection are logical commands through [focus and navigation](input.md#focus-and-navigation), not canvas-private key handling.

Edits are requests to the application: add/remove/move node, connect/disconnect, and property updates. The domain applies them and owns undo/redo history. A continuous gesture forms one transaction, so a 100-frame drag is one history entry. Copy/paste uses a serializable, JSON-compatible intermediate representation; clipboard access stays host-owned under [input](input.md#proposed-editing-ime-and-clipboard-boundary).

## Proposed definitions, registry, and reload

Node types are declared through the common [UI model](ui-model.md#common-representation): ports, types, and body components. A future DSL exposes graph primitives inside the general authoring language, not a separate node-editor language. A node type registry lets domains and plugins register types; dynamic plugin loading is later.

Live reload updates node appearance, layout, controls, styles, and labels while preserving the graph model, positions, selection, viewport, and property values, following the [reload transaction](path-finder-integration.md#live-reload-transaction).

## Proposed automation and observation

Node, port, and edge IDs are semantic identities independent of screen coordinates. Inspection can enumerate nodes and edges with bounds, and operations such as connecting `texture.out.color` to `material.in.baseColor` resolve by ID through ordinary declared actions under [target resolution](inspection.md#proposed-target-resolution-and-operations), so agents need not drive coordinates.

Graph snapshots declare viewport, scale, fonts, theme, and graph content. Structural assertions (node/edge counts, node and port bounds, selection) come first; a few golden images cover visual regressions. A development overlay can show node bounds, port hit areas, edge bounds, spatial index cells, repaint regions, draw calls, and visible node/edge counts through [inspection metrics](inspection.md#proposed-diagnostics-and-comparisons).

## Proposed theming, animation, and overlays

Colors come from theme tokens such as `graph.canvas.background`, `graph.grid.major`, `graph.node.selected`, `graph.port.input`, and `graph.edge.invalid`, defined once [themes](styling.md#proposed-themes) exist. Animation (selection, connection feedback, collapse) stays restrained and can be limited automatically on large graphs.

Execution visualization overlays application-supplied runtime state on the same canvas: inactive/running/completed/failed nodes, value previews, timing, invocation counts, and errors.

## Later extensions

Automatic layout goes through a layout-provider interface (DAG/Sugiyama, force-directed, tree, orthogonal) instead of algorithms built into the canvas. Graph diff (added, removed, modified, moved, reconnected), graph templates, validation/lint, conversion-node insertion, and profiling follow consumer demand. Delivery scope and planning-scale targets are owned by [backlog](../roadmap/backlog.md).

## Verification

Test coordinate round trips and cursor-anchored zoom, port and curve hit distances, culled-versus-full agreement, selection and transaction grouping, compatibility presentation, ID-based operations after reload, and LOD thresholds with numeric assertions. Use a small set of deterministic graph images under the [testing strategy](../guides/testing.md).
