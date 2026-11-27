# Repository agent instructions

## Where to start

- `docs/roadmap/current.md` names the active milestone and what the reference application must do. Build whatever that application needs, including new widgets, host integration and runtime changes.
- Keep only unfinished selected work in `docs/roadmap/current.md`. Remove implemented or completed items instead of retaining progress entries; record capability scope in the support matrix and delivery history in the changelog.
- `docs/architecture/overview.md` states the dependency boundaries to keep (core free of GPU/platform/browser/USD/editor SDKs; layout independent of rendering).
- `docs/guides/development.md` has the build and test commands; `docs/guides/testing.md` lists the existing owner tests per subsystem.

## Observing UI changes

- To see what a UI change does without opening a window, build Release and run `tessera_inspector_hierarchy --snapshot <scenario> <directory>`. Each scenario takes under a second. Read `g<n>/tree.txt` first, then `g<n>/frame.png`. Scenarios are listed in `docs/guides/inspector-hierarchy.md#snapshot-bundles`.
- `tessera_snapshot capture <recording.json> --out <directory>` writes the same kind of bundle for a placeholder-text Replay JSON recording. Write bundles outside the repository or under `build/`.

## Git workflow

- Never commit or push directly to `main` unless the user explicitly requests it.
- Start from `origin/main`, work on a `codex/*` branch, push it and open a pull request targeting `main`.
- Before pushing, verify the branch and upstream with `git status -sb`.
