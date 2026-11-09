# Documentation

Start with [Getting Started](guides/getting-started.md) and [your first app](guides/first-app.md) to build an application. For repository changes, read [current work](roadmap/current.md) and the code/docs near the change.

## Canonical owners

| Category | Information |
| --- | --- |
| [Product](product/) | Purpose, principles, intended consumers and non-goals |
| [Architecture](architecture/) | Subsystem boundaries and technical contracts; **Implemented** sections link source-backed APIs, **Proposed** sections describe future designs |
| [Guides](guides/) | Usage and procedures; [development](guides/development.md) owns commands, [testing](guides/testing.md) owns test selection |
| [Reference](reference/) | [Current support and limits](reference/support-matrix.md), [dependency choices](reference/dependencies.md) |
| [Roadmap](roadmap/README.md) | [Remaining active work](roadmap/current.md), [inactive candidates](roadmap/backlog.md) |
| [Proposals](proposals/) | Possible mechanisms and promotion criteria |
| [Integrations](integrations/) | Consumer requirements and designs |
| [Releases](releases/README.md) | Per-version entry points into [delivery history](../CHANGELOG.md) and published source releases |

Update the page whose contract, procedure or status changes; link to it elsewhere rather than copying. Completed work leaves current. Routine execution results belong in the PR/CI, not another status report. Serialized fields are owned by [JSON v1](../formats/tessera-ui/README.md).

## Architecture

[Overview](architecture/overview.md) · [Reactive runtime](architecture/reactive-runtime.md) · [UI model](architecture/ui-model.md) · [Layout](architecture/layout.md) · [Styling](architecture/styling.md) · [Rendering](architecture/rendering.md) · [Text](architecture/text.md) · [Localization](architecture/localization.md) · [Input](architecture/input.md) · [Commands](architecture/commands.md) · [Semantics](architecture/semantics.md) · [Replay](architecture/replay.md) · [Inspection](architecture/inspection.md)

## Guides

[Getting Started](guides/getting-started.md) · [First app](guides/first-app.md) · [Settings starter](guides/settings-starter.md) · [Hierarchy / Inspector](guides/inspector-hierarchy.md) · [SplitPane](guides/split-pane.md) · [Scrollbar](guides/scrollbar.md) · [Stack](guides/stack.md) · [Grid](guides/grid.md) · [Popup/Tooltip](guides/popup-tooltip.md) · [Modal/Dialog](guides/modal-dialog.md) · [TreeView/ListView](guides/tree-list.md) · [Menu/ContextMenu](guides/menu-context.md) · [RadioGroup](guides/radio-group.md) · [Checkbox/Switch](guides/checkbox-switch.md) · [TextInput/NumberInput](guides/text-number-input.md) · [Renderer telemetry](guides/renderer-diagnostics.md) · [Development](guides/development.md) · [Testing](guides/testing.md)

## Product and consumers

[Vision](product/vision.md) · [Principles](product/principles.md) · [Use cases](product/use-cases.md) · [Non-goals](product/non-goals.md)

[Settings application](integrations/settings-app.md) · [Renderer diagnostics](integrations/renderer-diagnostics.md) · [Path-finder](integrations/path-finder.md) · [Graph editor](integrations/graph-editor.md) · [Conversational UI](integrations/conversational-ui.md)

## Proposals

[C++ authoring](proposals/cpp-authoring.md) · [Component lifecycle](proposals/automatic-component-lifecycle.md) · [Async resources](proposals/async-resources.md) · [Error boundaries](proposals/error-boundaries.md) · [Property reflection](proposals/property-reflection.md) · [Overlays/portals](proposals/overlays-and-portals.md) · [Live reload](proposals/live-reload.md) · [Virtualization](proposals/virtualization.md) · [Incremental evaluation](proposals/incremental-evaluation.md) · [Command palette](proposals/command-palette.md) · [Look / visual rendering](proposals/look.md) · [Animation](proposals/animation.md) · [Snapshot bundles/CLI](proposals/snapshot-bundles.md) · [Agent capability manifest](proposals/agent-capability-manifest.md) · [Generation history](proposals/generation-history.md) · [Web host](proposals/web-host.md)
