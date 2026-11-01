# Tessera agent guidance

## Start here

Read `docs/roadmap/current.md` and the code/docs near the change. Start from a user operation or reproducible defect; use existing public APIs before extending a subsystem.

Consult other owners when needed:

- `docs/agent-policy.md`: scope, verification ownership or stop-rule decisions.
- `docs/guides/testing.md`: affected checks for code changes.
- The relevant subsystem contract: before changing its public behavior or invariants.
- `docs/architecture/overview.md`: structural or dependency-boundary changes.
- `docs/reference/support-matrix.md`: support claims, validation evidence or release acceptance.
- `docs/README.md`: documentation ownership, moves or classification.

## Implementation constraints

- Keep core independent of browser engines, GPU SDKs, platform input/window APIs, OpenUSD and editor SDKs. Keep layout independent of rendering and backends independent of application/component state.
- Keep serialized UI backend-neutral and versioned. Validate inputs with actionable diagnostics; make ownership and lifetime explicit.
- Normalize input at the host boundary. Treat keyboard/gamepad navigation as core design requirements. Isolate text shaping and external asset loading behind interfaces.
- Prefer composition, stable names and narrow public APIs. Build on the runtime IR before adding a DSL, JSX frontend or JavaScript runtime.
- Follow current scope. Designs, proposals, integrations and backlog do not independently authorize implementation. Name the blocked application operation before adding a widget/API; runtime changes need a demonstrated blocker, existing contract defect or measured bottleneck.
- Prefer one user operation or defect per change; closely related small fixes may be grouped when splitting adds overhead. Summarize behavior, relevant risk and executed checks in a short PR description; no fixed acceptance template is required.
- Stop when the operation works and affected regression checks pass. Return to the application instead of expanding adjacent proposals.

## Documentation and checks

Update the canonical owner only when its public contract, procedure or scope changes. Preserve source-backed Implemented/Proposed labels. Current owns active scope, backlog inactive candidates, support live capability/configuration evidence and changelog delivery history; avoid competing status records. Update support for material capability/support/evidence changes, not every UI PR. Never invent commands, dependencies, platform support or executed results.

For code changes, run affected owner checks and one representative consumer smoke; inherit unchanged runtime guarantees rather than re-proving them for every widget. Wider integration and release checks follow the testing guide. Preserve ownership, lifetime, invalid-input and regression coverage.

For documentation-only changes, run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-docs.ps1` and review canonical ownership; runtime tests are unnecessary. Keep relative Markdown links portable. Do not generate redundant tests or extensive widget screenshot suites.
