# Repository agent instructions

## Where to start

- `docs/roadmap/current.md` names the active milestone and what the reference application must do. Build whatever that application needs, including new widgets, host integration and runtime changes.
- Keep only unfinished selected work in `docs/roadmap/current.md`. Remove implemented or completed items instead of retaining progress entries; record capability scope in the support matrix and delivery history in the changelog.
- `docs/architecture/overview.md` states the dependency boundaries to keep (core free of GPU/platform/browser/USD/editor SDKs; layout independent of rendering).
- `docs/guides/development.md` has the build and test commands; `docs/guides/testing.md` lists the existing owner tests per subsystem.

## Observing UI changes

- To see what a UI change does without opening a window, build Release and run `tessera_inspector_hierarchy --snapshot <scenario> <directory>`. Each scenario takes under a second. Read `g<n>/tree.txt` first, then `g<n>/frame.png`. Scenarios are listed in `docs/guides/inspector-hierarchy.md#snapshot-bundles`.
- `tessera_inspector_hierarchy --capabilities <scenario> <file>` writes that scenario's capability manifest: its commands with enabled state and arguments, the controls that can be focused or activated with the command each requests, and the operations the window-free adapter offers. See `docs/guides/inspector-hierarchy.md#capability-manifest`.
- `tessera_inspector_hierarchy --semantic-diff <scenario> <file>` writes what that scenario changed in meaning, compared with the launch state: focus, added, removed and moved entries, and name, value, selection, enabled and action changes by author ID. `tessera_snapshot semantic-diff <before.json> <after.json>` does the same for two `g<n>/semantics.json` files, with `--scope <id>` and `--kind <kinds>` filters. See `docs/guides/inspector-hierarchy.md#semantic-diff`.
- `tessera_snapshot capture <recording.json> --out <directory>` writes the same kind of bundle for a placeholder-text Replay JSON recording. Write bundles outside the repository or under `build/`.
- To check what a change did, write a scenario's bundle before and after it and run `tessera_snapshot diff <before> <after>`. The report lists the first changed level and the semantic, layout, action, paint and pixel changes. Exit status 0 means equal, 1 different, 2 invalid and 3 incompatible conditions such as a different viewport.

## Git workflow

- Never commit or push directly to `main` unless the user explicitly requests it.
- Start from `origin/main`, work on a `codex/*` branch, push it and open a pull request targeting `main`.
- Before pushing, verify the branch and upstream with `git status -sb`.
