# Agent capability manifest

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed agent capability manifest

Agents operate UI through meaning rather than screen scraping. A capability manifest describes, for one generation and one adapter's permissions, the [commands](../architecture/commands.md) with their eligibility and argument schemas, the inspectable surfaces (document, hierarchy, selection, properties), and the semantic actions (activate, select, focus, set value). It is a projection of the semantic tree, the command registry, and inspection records, not an agent-specific runtime API. Development-only commands and private state stay out of production manifests, and the manifest version is independent of document, replay, and bundle versions.

The manifest also declares adapter support for tree/property inspection, semantic action and command invocation, input replay, frame capture, invariant queries, source-target editing, reload, and snapshot comparison. Each entry identifies its request/result schema, availability, permission/development restrictions, and target/generation preconditions; unsupported operations are explicit. Editing resolves validated source mappings through a host-authorized source adapter, and runtime property edits use declared commands or edit operations. Capability discovery grants no bypass of eligibility, transactions, or lifetime checks.

## Promotion criteria

Promote discovery only when a concrete adapter cannot use its declared inspect/invoke/capture interfaces without a machine-readable operation inventory. Include only those operations, with explicit permissions and unsupported results.
