# Tessera agent guidance

## Read first

- `README.md` and `docs/README.md` explain repository status and document ownership.
- `docs/design/architecture.md` defines subsystem and ecosystem boundaries.
- `docs/roadmap/current.md` identifies active implementation scope.
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

## Documentation and checks

Design snippets currently describe proposals, not implemented APIs. Do not invent build commands, dependency pins, platform support, or completed test evidence.

Update the canonical subsystem page when a contract changes. Update current work, backlog, and support claims only when their status changes. Keep relative Markdown links portable and check them after edits.

For code changes, verify the affected algorithms and boundaries using the testing guide. For documentation-only work, link and consistency checks are sufficient. Do not generate redundant tests or extensive widget screenshot suites.
