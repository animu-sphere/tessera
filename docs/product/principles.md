# Product principles

Durable principles constrain design without scheduling features. Technical ownership and update rules belong to the [architecture](../architecture/overview.md).

1. Declarative composition above imperative execution.
2. Layout is independent of rendering; rendering is independent of application state.
3. UI documents are backend-neutral, serializable, deterministic, and versioned.
4. The core has no browser engine, JavaScript VM, platform SDK, or OpenUSD requirement.
5. Keyboard/gamepad interaction, text, and semantic metadata are first-class subsystems.
6. Prefer composition, explicit ownership, small interfaces, and useful diagnostics.
7. Keep dependencies modular; validate boundaries before optimizing draw calls.
8. Path-finder edits Tessera documents and uses the same runtime for preview.
9. DevTools, tests, and external agents share inspection and ordinary action boundaries; no agent SDK or AI model is required by core.
10. Native and Web hosts share the runtime model; browser services belong to optional host adapters.
11. Applications own their state, data, and edit history. Tessera provides view bindings and [command and transaction boundaries](../architecture/commands.md), not an application model or undo store.
12. Pointer, keyboard, gamepad, accessibility, and agent input reach the application through one semantic action and command path with the same eligibility; no input source gets a private route.

The [agent policy](../agent-policy.md) governs fixture-driven task selection and when subsystem work stops.
