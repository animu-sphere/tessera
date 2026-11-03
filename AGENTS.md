# Repository agent instructions

## Where to start

- `docs/roadmap/current.md` names the active milestone and what the reference application must do. Build whatever that application needs, including new widgets, host integration and runtime changes.
- `docs/architecture/overview.md` states the dependency boundaries to keep (core free of GPU/platform/browser/USD/editor SDKs; layout independent of rendering).
- `docs/guides/development.md` has the build and test commands; `docs/guides/testing.md` lists the existing owner tests per subsystem.

## Git workflow

- Never commit or push directly to `main` unless the user explicitly requests it.
- Start from `origin/main`, work on a `codex/*` branch, push it and open a pull request targeting `main`.
- Before pushing, verify the branch and upstream with `git status -sb`.
