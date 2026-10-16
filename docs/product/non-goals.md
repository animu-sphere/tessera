# Product non-goals

Tessera does not aim to replace Qt completely, reproduce all CSS, implement a browser engine or adopt a JavaScript VM as a core requirement. A broad widget catalog and framework API compatibility are not product prerequisites.

An approachable authoring API and practical widget library are product goals. They do not require a separate widget runtime, styling engine, execution model per language, or agent-only UI system. Stable syntax, complete platform coverage and generalized infrastructure must not become implicit prerequisites for a bounded application workflow.

The runtime does not own application data, domain graph schemas, world scenes, edit history, external execution providers or editor orchestration. Those reach it through explicit host/adaptor boundaries in [architecture](../architecture/overview.md#ecosystem-ownership).

A detailed proposal does not commit to implementation. Backend-neutral design does not require every backend or platform adapter to exist, and an intended use case does not establish support. [Current work](../roadmap/current.md) owns active scope, [backlog](../roadmap/backlog.md) owns inactive candidates, and [support](../reference/support-matrix.md) owns capability/configuration evidence. Follow the [agent policy](../agent-policy.md) rather than completing adjacent mechanisms for their own sake.
