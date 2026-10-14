# Command palette

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed command palette

The command palette is the reference consumer of the registry and is composed from ordinary components, list virtualization, and text input; it is not a special runtime case. It provides fuzzy search over labels, categories, and descriptions; category grouping; shortcut display; recently used commands and command history; disabled reasons; and argument prompts generated from the argument schema. Search ranking and history storage are application- or library-owned policies. A [graph editor's](../integrations/graph-editor.md#proposed-interaction-and-editing) node-creation search can reuse the same component with a filtered command set.

## Promotion criteria

Promote a palette only when the active consumer needs searchable command discovery. A toolbar or direct command invocation is sufficient until a fixture demonstrates that requirement.
