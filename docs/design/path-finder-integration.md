# Path-finder integration

Status: Draft design. No editor bridge or reload implementation exists.

## Shared model

```text
Path-finder edits -> Tessera UI document -> validate/load -> Tessera runtime
```

Path-finder owns hierarchy/property editing, visual authoring, drag/drop, preview orchestration, and generated source. Tessera owns the runtime model and validation. Preview and deployed applications consume the same [UI document](ui-model.md), not divergent editor/runtime node models.

Design metadata can be an extension section or sidecar referencing stable author IDs. Decide this encoding in the schema design. Runtime behavior must not depend on editor-only fields, while editor round trips must preserve metadata according to a declared policy.

## Proposed editor bridge

Expose narrow operations for document/schema inspection, validation diagnostics, reload requests, and preview results. Source locations allow Path-finder to link runtime/validation errors back to authored text. Avoid adding editor SDKs or reflection frameworks to the core.

Bridge transport, protocol versioning, process boundaries, preview asset resolution, and source-generation ownership are open. Start with an in-process adapter only if that is enough to prove shared-model behavior; do not commit to IPC before a consumer requires it.

## Live reload transaction

Proposed sequence:

1. Parse and validate a candidate document using the runtime schema.
2. Resolve required references and report errors with source locations.
3. Determine which existing node identities and local state remain compatible.
4. Commit the replacement at a defined frame boundary.
5. Recalculate style/layout/paint and recover focus/interaction state.

A rejected candidate leaves the last valid tree usable. Successful reload cleans up removed bindings/components and preserves only state declared compatible by identity, node/component type, and state schema. Author IDs and reconciliation keys must have explicit scopes before this can be reliable.

Application state remains host-owned throughout reload. GPU resources replaced by a document follow completion-safe retirement; see [rendering](rendering.md). Initial atomic full-document reload can precede fine-grained patches.

## Verification and scope

Use one document fixture in both a runtime loader and an editor preview consumer. Check invalid edits, stable-ID state preservation, type changes, deleted nodes, focus recovery, extension metadata round trips, and cleanup across repeated reloads.

The v0.3.0 candidate is an integration prototype, not a complete visual editor. A full bridge follows the component identity and schema foundations in the [backlog](../roadmap/backlog.md).
