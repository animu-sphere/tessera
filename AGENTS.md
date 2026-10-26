# Tessera agent guidance

## Read first

- `README.md` and `docs/README.md` provide navigation and canonical document ownership.
- `docs/agent-policy.md` defines implementation authority and the subsystem stop rule.
- `docs/roadmap/current.md` identifies the active integration slice and its exit criteria; read it before choosing feature work.
- `docs/reference/support-matrix.md` is the sole live implementation/validation status record.
- `docs/architecture/overview.md` defines subsystem and ecosystem boundaries.
- Read the relevant subsystem design before changing its contract.

## Implementation constraints

- Keep the core independent of browser engines, GPU SDKs, platform input/window APIs, OpenUSD, and editor SDKs.
- Keep layout independent of rendering and render backends independent of component/application state.
- Keep serialized UI definitions backend-neutral and versioned. Validate inputs with actionable diagnostics.
- Make ownership and lifetime explicit. Prefer composition, stable names, and narrow interfaces.
- Normalize input at the host boundary. Treat keyboard/gamepad navigation as core design requirements.
- Isolate text shaping and external asset loading behind interfaces.
- Build the runtime IR before introducing a DSL, JSX frontend, or JavaScript runtime.
- Follow the current milestone; avoid unrelated widgets, frameworks, and optimization work.
- Designs, proposals, integrations, and backlog entries do not independently authorize implementation. Promote only the minimum change required by the active fixture, or fix an existing contract/test.
- Stop subsystem expansion when the active fixture can proceed and its required invariants and exit criteria are satisfied. Return to integration instead of implementing adjacent proposals for completeness.
- Follow the verification ownership and bounded acceptance rules in `docs/agent-policy.md`; use `docs/guides/testing.md` to select affected checks and cite inherited owner tests instead of re-proving runtime guarantees in each consumer.

## Documentation and checks

Design sections distinguish implemented contracts (with source links) from proposed APIs. Preserve that distinction. Do not invent build commands, dependency pins, platform support, or completed test evidence.

Write status only in the support matrix; contract pages preserve Implemented/Proposed maturity labels without progress words, versions, or dates (see `docs/README.md` status and wording rules). `docs/product/` owns intent, `docs/architecture/` owns invariants and contracts, `docs/proposals/` owns possible mechanisms and promotion criteria, and `docs/integrations/` owns consumer designs. Proposal authority markers do not schedule work. Update the canonical subsystem page when a contract changes. Current owns remaining active scope; backlog owns inactive candidates; support owns live capability/configuration evidence; changelog owns delivery history. Follow `docs/README.md` change routing and do not copy status into other pages. Keep relative Markdown links portable and check them after edits.

For code changes, verify the affected algorithms and boundaries using the testing guide. For documentation-only work, run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-docs.ps1` and review canonical ownership; runtime tests are unnecessary. Do not generate redundant tests or extensive widget screenshot suites.
