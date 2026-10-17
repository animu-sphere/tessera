# Select, edit and Apply Settings

Extend [your first app](first-app.md) with the two-file [Settings starter](../../examples/settings-starter/main.cpp) and its [CMake project](../../examples/settings-starter/CMakeLists.txt). `Field` and `Settings` compose the application; `Host`, `prepare`, `operate` and `input` provide the explicit host boundary. The [form API](../architecture/ui-model.md#implemented-c-form-authoring) and [Settings consumer](../integrations/settings-app.md#implemented-copyable-editapply-starter-boundary) own their contracts.

## Copy, build and run

Use the compiler and CMake prerequisites from [Getting Started](getting-started.md#prerequisites). From the repository root:

```powershell
New-Item -ItemType Directory -Path build-settings-starter-source -Force | Out-Null
Copy-Item examples/settings-starter/main.cpp, examples/settings-starter/CMakeLists.txt -Destination build-settings-starter-source
cmake -S build-settings-starter-source -B build-settings-starter -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_SOURCE_DIR=C:/dev/tessera
cmake --build build-settings-starter --config Debug --parallel 4 -- /nodeReuse:false
& .\build-settings-starter\Debug\settings_starter.exe
ctest --test-dir build-settings-starter -C Debug --output-on-failure --no-tests=error
cmake --build build-settings-starter --config Release --parallel 4 -- /nodeReuse:false
ctest --test-dir build-settings-starter -C Release --output-on-failure --no-tests=error
```

Replace `C:/dev/tessera` with the absolute checkout path. Both copied files are self-contained application sources; they may live outside the checkout. The project links only `tessera::core`, inherits C++20 and public includes, and disables optional fonts/Vulkan. Embedded Tessera defaults exclude its tests/examples; the copied project's own CTest runs `settings_starter`.

For local-source FetchContent, configure a separate directory with the same source:

```powershell
cmake -S build-settings-starter-source -B build-settings-starter-fetch -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_SOURCE_DIR=C:/dev/tessera -DTESSERA_IMPORT_MODE=FetchContent
cmake --build build-settings-starter-fetch --config Debug --parallel 4 -- /nodeReuse:false
ctest --test-dir build-settings-starter-fetch -C Debug --output-on-failure --no-tests=error
cmake --build build-settings-starter-fetch --config Release --parallel 4 -- /nodeReuse:false
ctest --test-dir build-settings-starter-fetch -C Release --output-on-failure --no-tests=error
```

No network fetch is needed for either import. Independent checks copy these exact two files through the [consumer driver](../../tests/cmake/run-consumer.cmake). Actual configurations and results belong to [support](../reference/support-matrix.md#settings-starter-section-evidence--2026-10-17).

The program supplies normalized input itself. Each checkpoint prints `Section <id>`, the `User ...` summary and the saved result, in this order:

| Checkpoint | Section | Username / feature | Saved result |
| --- | --- | --- | --- |
| Initial | account | Ada / disabled | Not saved |
| Insert `本` | account | Ada本 / disabled | Not saved |
| Select Feature | feature | Ada本 / disabled | Not saved |
| Toggle | feature | Ada本 / enabled | Not saved |
| Gamepad Apply | feature | Ada本 / enabled | Saved 1: Ada本; feature enabled |
| Select About | about | Ada本 / enabled | Saved 1: Ada本; feature enabled |
| Return to Account | account | Ada本 / enabled | Saved 1: Ada本; feature enabled |
| Insert `!` | account | Ada本! / enabled | Saved 1: Ada本; feature enabled |
| Keyboard Apply | account | Ada本! / enabled | Saved 2: Ada本!; feature enabled |

It ends with:

```text
2 owned saves; 8 host commands; 640x360 RGBA8 capture in memory (placeholder text, synthetic glyphs; no window)
```

The process exits nonzero with diagnostics on failure. This is a window-free example with placeholder text and synthetic glyph marks; a native host supplies its own window, physical input normalization and rendering services.

## Compose the application and its styles

`Settings` accepts the selected section and four controlled values: username edit state, enabled boolean, derived summary string and saved-result string. Iteration over the fixed `account`, `feature`, `about` list creates ordinary Buttons with `section-<id>` identities; `* ` marks the selected choice and `selected-section` names it. A C++ branch composes only Account's TextInput, Feature's Checkbox or About's help. Summary, Apply and saved result follow every branch. `Field` is a reusable typed function with a default gap and ordered children. Explicit IDs identify `username`, `toggle`, `section-help`, `summary`, `apply` and `save-result` across full-tree replacement.

`application_styles` keeps reusable form, control and status class declarations separate from composition. `Props::classes` attaches them; Apply supplies a typed instance background override. `prepare` passes the sheet and lowered instance declarations to the shared resolver. Change that palette to restyle the application through the existing cascade. Theme switching and interaction-state rules are demonstrated in the [larger Settings fixture](development.md#settings-form-fixture).

One session owns named `username`, `enabled` and `save-result` observations. A pure computed summary reads username and enabled. The form holds weak handles, so the session must outlive all lowering and publication on its creating UI thread. The `form(selected)` lambda explicitly calls `Settings` to compose a candidate Element tree before each `prepare`; `session.lower` settles and copies values without rerunning C++ branches. Removing a branch does not remove host/session state, and this composition needs no per-section component mount or keyed local state.

## Own the draft and saved copies in the host

`Host::username` owns the accepted `TextEditState`: a never-reused nonzero owner, revision, committed UTF-8 bytes and byte anchor/caret. `Host::draft` owns `{username, feature}` and `Host::selected` owns section selection independently of text selection. The session values observe that host state; they do not own application saving. These lifetimes span all branch replacements, so returning to Account restores the exact edit owner, revision and selection.

`Host::execute` proposes copies in `next_username`, `next_draft`, `next_result` and `next_selected`. Editing calls the pure `edit_text` transform with an owned request matching the accepted owner/revision. Toggling changes the candidate boolean; section commands change only candidate selection. Apply formats the next save result. Only after the whole candidate presentation succeeds does `operate` append an owned `SettingsData` copy to `saves`, accept host values and publish. Section changes append no save, and further draft edits cannot mutate an earlier record or its displayed result. This synchronous in-memory operation has no persistence or asynchronous/fallible save protocol.

The host registers six ordinary action/command mappings, including the three section choices, and publishes explicit eligibility for each presentation. `operate` resolves the live action owner against the accepted command generation and semantic input, then uses one executor and increasing sequence stream. Inspecting an outer execution result alone is insufficient: the host checks its outcome before staging. Each logical keyboard/gamepad Apply uses the same descriptor and host operation after dispatch returns.

## Route editing before navigation

`input` validates a normalized event and refreshes focus against the accepted tree. It first requires a present username owner, then compares that owner with eligible refreshed focus. Two empty optionals must never count as a focused field. When username is eligible and focused, committed `tessera::TextInput` becomes an insert request. Left/Right/Home/End become movement, Shift extends selection, and Backspace/Delete become erase requests. Control/Alt/Super modified keys are outside this mapping. Consumed editing input returns before logical navigation; other raw keys/text are ignored by this bounded host.

The remaining logical events go to `FocusDispatcher`. Four `FocusNext` events traverse the three choices to username; committed text inserts `本` after `Ada`. Two `FocusPrevious` events reach Feature, and keyboard `Activate` selects it. Two `FocusNext` events reach Checkbox; activation toggles true. The next focus step reaches Apply for logical gamepad activation. Two previous steps reach About for gamepad selection. The script resets focus to exercise absent username plus empty focus; committed text and all supported editing keys on Feature/About leave observations and command count unchanged. From empty focus, `FocusNext`/`Activate` returns to Account, retaining the edit lifetime and 6:6 selection. Three next steps reach username, `!` changes only the draft, and the next step reaches Apply for keyboard activation. Each activated choice retains its author-ID focus after replacement. A real host maps Tab/D-pad and Enter/A into these logical events and owns the monotonic clock/repeat policy; changing `CommandSource` does not poll a device.

Offsets are UTF-8 bytes at scalar boundaries: `Ada本` ends at byte 6; `Ada本!` ends at byte 7. Each accepted text edit or caret/byte-selection command advances the edit revision; section selection preserves it. The [logical editing contract](../architecture/input.md#implemented-logical-string-editing) owns bounds and rejection rules. TextInput exposes label, committed bytes and selection as ordinary Text/button semantics; it supplies no native editable accessibility adapter or caret geometry.

## Stage, accept and observe

`prepare` returns a complete candidate only after lowering, command eligibility, tree creation, style resolution, layout, inspection and paint succeed. Until then, the accepted host draft, save records and presentation stay intact. On a returned staging failure, `operate` restores the three session observations to accepted host values and exits with the stage diagnostics. It does not publish the failed candidate or save it. The consumed command sequence and candidate generation counter are not rewound. Allocation failures and arbitrary exceptions have no general rollback guarantee; rejection/retry and stale-owner injection belong to the [Settings regression fixture](../../examples/settings/main.cpp).

The sheet, shaper and lowered source/style inputs are borrowed synchronously during preparation. The accepted presentation owns its command snapshot, tree, resolved styles, layout, inspection and paint. Dispatch/command views borrow that presentation only until their calls return; replacement invalidates old tree handles. `focus.refresh` recovers by explicit author ID after acceptance. Session publication follows successful preparation.

`observe` resolves author IDs from owned inspection and compares selected markers/name, the conditional branch, username/byte selection or Checkbox when present, summary and saved result with host observations. It checks visible geometry within the 640×360 logical viewport, repeats CPU captures at nine accepted checkpoints, and checks that branch and draft changes affect pixels. Capture fixes scale one and time zero. The rasterizer outlives its borrowed glyph cache, and the cache outlives the capture adapter; capture returns owned RGBA8 bytes in memory. The [larger fixture](../integrations/settings-app.md) demonstrates dynamic section-list mutation, constrained scrolling, richer styles and replay of accepted views through the same runtime. A smaller viewport requires an explicit host scroll/reveal policy; resizing alone does not connect that operation.
