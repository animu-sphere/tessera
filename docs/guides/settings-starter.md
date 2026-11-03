# Select, scroll, edit, Apply and Cancel Settings

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
| Edit and toggle before first Apply | feature | Adadiscard日😀 / enabled | Not saved |
| Keyboard Cancel | feature | Ada / disabled | Not saved |
| Initial | account | Ada / disabled | Not saved |
| Insert `本` | account | Ada本 / disabled | Not saved |
| Select Feature | feature | Ada本 / disabled | Not saved |
| Toggle | feature | Ada本 / enabled | Not saved |
| Gamepad Apply | feature | Ada本 / enabled | Saved 1: Ada本; feature enabled; theme system |
| Select About | about | Ada本 / enabled | Saved 1: Ada本; feature enabled; theme system |
| Return to Account | account | Ada本 / enabled | Saved 1: Ada本; feature enabled; theme system |
| Insert `!` | account | Ada本! / enabled | Saved 1: Ada本; feature enabled; theme system |
| Keyboard Apply | account | Ada本! / enabled | Saved 2: Ada本!; feature enabled; theme system |
| Control+A | account | Ada本! / enabled, selection 0:7 | Saved 2: Ada本!; feature enabled; theme system |
| Replace with `日😀` | account | 日😀 / enabled | Saved 2: Ada本!; feature enabled; theme system |
| Keyboard Apply | account | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system |
| Edit and toggle after Apply | feature | 日😀! / disabled | Saved 3: 日😀; feature enabled; theme system |
| Gamepad Cancel | feature | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system |
| Return to Account | account | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system |
| Wheel each pane to both limits | account | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system |
| Pointer About then Account | account | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system |
| Pending username and Dark preference | account | 日😀? / enabled | Saved 3: 日😀; feature enabled; theme system |
| About, pointer Light, Apply | about | 日😀? / enabled | Saved 4: 日😀?; feature enabled; theme light |
| Select Dark, then Cancel | about | 日😀? / enabled | Saved 4: 日😀?; feature enabled; theme light |

It ends with:

```text
4 owned saves; 38 host commands; 640x240 RGBA8 capture in memory (placeholder text, synthetic glyphs; no window)
```

The process exits nonzero with diagnostics on failure. This is a window-free example with placeholder text and synthetic glyph marks; a native host supplies its own window, physical input normalization and rendering services.

## Compose the application and its styles

`Settings` accepts the selected section, theme key, expansion flag and four controlled values: username edit state, enabled boolean, derived summary string and saved-result string. Iteration over the fixed `account`, `feature`, `about` list creates ordinary Buttons with `section-<id>` identities; `* ` marks the selected choice and `selected-section` names it. A C++ branch composes only Account's TextInput, Feature's Checkbox or About's help. A fixed title sits above a Row containing two `ScrollView` panes: the 140×64 category sidebar and 440×144 form. The form contains the selected-section label, conditional branch, Theme Dropdown, summary, Apply/Cancel Row and saved result. Both panes use 12-unit gaps in a 640×240 logical viewport. `Field` is a reusable typed function with a default gap and ordered children. Explicit IDs identify `username`, `toggle`, `section-help`, `summary`, `apply`, `cancel` and `save-result` across full-tree replacement.

`application_styles` keeps reusable form, control and status class declarations separate from composition. `Props::classes` attaches them; Apply supplies a typed instance background override. `prepare` passes the sheet and lowered instance declarations to the shared resolver. Change that palette to restyle the application through the existing cascade. Theme switching and interaction-state rules are demonstrated in the [larger Settings fixture](development.md#settings-form-fixture).

One session owns named `username`, `enabled` and `save-result` observations. A pure computed summary reads username and enabled. The form holds weak handles, so the session must outlive all lowering and publication on its creating UI thread. The `form(selected, theme, open)` lambda explicitly calls `Settings` to compose a candidate Element tree before each `prepare`; `session.lower` settles and copies values without rerunning C++ branches. Removing a branch does not remove host/session state, and this composition needs no per-section component mount or keyed local state.

## Own the draft and saved copies in the host

`Host::username` owns the accepted `TextEditState`: a never-reused nonzero owner, revision, committed UTF-8 bytes and byte anchor/caret. `Host::draft` owns `{username, feature, theme}` and `Host::selected` owns section selection independently of text selection. The session values observe that host state; they do not own application saving. These lifetimes span all branch replacements, so returning to Account restores the exact edit owner, revision and selection.

`Host::execute` proposes copies in `next_username`, `next_draft`, `next_result`, `next_selected` and `next_theme_open`. Editing calls the pure `edit_text` transform with an owned request matching the accepted owner/revision. Toggling changes the candidate boolean; section commands change only candidate selection. Apply formats the next save result. Only after the whole candidate presentation succeeds does `operate` append an owned `SettingsData` copy to `saves`, accept host values and publish. Section changes append no save, and further draft edits cannot mutate an earlier record or its displayed result. This synchronous in-memory operation has no persistence or asynchronous/fallible save protocol.

`Host::applied` starts with the initial draft and changes only when an Apply presentation is accepted. Cancel restores that copy, preserving the selected section, saved copies and saved-result Text. Two pure `edit_text` transforms select all current bytes and replace them with the applied username; the owner stays the same, revision advances twice, and selection collapses at the restored buffer end. Both transforms remain local until preparation succeeds. Cancel therefore also works while Account is absent.

The host registers ordinary action/command mappings for editing, toggling, Apply/Cancel, section selection and the Dropdown, and publishes explicit eligibility for each presentation. `operate` resolves the live action owner against the accepted command generation and semantic input, then uses one executor and increasing sequence stream. Inspecting an outer execution result alone is insufficient: the host checks its outcome before staging. Each logical keyboard/gamepad Apply or Cancel uses its ordinary descriptor and host operation after dispatch returns.

## Route editing before navigation

`input` validates a normalized event and refreshes focus against the accepted tree. After wheel, pointer and Dropdown dismissal routing, editing requires a present username owner, then compares that owner with eligible refreshed focus. Two empty optionals must never count as a focused field. When username is eligible and focused, committed `tessera::TextInput` becomes an insert request. Left/Right/Home/End become movement, Shift extends selection, and Backspace/Delete become erase requests. Exact Control+A, without repeat or Shift/Alt/Super, selects byte range `0:text.size()` through the same edit command. Other Control/Alt/Super modified keys are outside this mapping. Consumed editing input returns before logical navigation; other raw keys/text are ignored by this bounded host.

Normalized `Scroll` and pointer events route before editing through the pane policy below. The remaining logical events go to `FocusDispatcher`. The script first edits both fields and reaches Cancel by traversing through Apply; keyboard activation restores initial values while Feature remains selected. It returns to Account and clears focus before the following edit/Apply sequence. Four `FocusNext` events traverse the three choices to username; committed text inserts `本` after `Ada`. Two `FocusPrevious` events reach Feature, and keyboard `Activate` selects it. Two `FocusNext` events reach Checkbox; activation toggles true. Two focus steps pass the Theme trigger and reach Apply for logical gamepad activation. Three previous steps pass Theme and Checkbox to reach About for gamepad selection. The script resets focus to exercise absent username plus empty focus; committed text and all supported editing keys on Feature/About leave observations and command count unchanged. From empty focus, `FocusNext`/`Activate` returns to Account, retaining the edit lifetime and 6:6 selection. Three next steps reach username, `!` changes only the draft, and two next steps pass Theme to reach Apply for keyboard activation. Each activated choice retains its author-ID focus after replacement. A real host maps Tab/D-pad and Enter/A into these logical events and owns the monotonic clock/repeat policy; changing `CommandSource` does not poll a device.

After the second Apply, the script returns to username, uses Shift+Home for reversed selection 7:0, then Control+A for 0:7. Committed `日😀` replaces the entire value and keyboard navigation through Theme reaches Apply to save it. Selection alone preserves draft bytes; both selection and replacement preserve the two earlier owned saves and saved result until Apply. Checks also send Control+A with no focus, Apply focus and absent Account, plus repeated or additionally modified chords while username is focused, requiring unchanged command generation and observations for these ignored inputs.

After the third Apply, another username edit and feature toggle create `日😀!` / false across category changes. Logical gamepad activation of Cancel restores `日😀` / true, retaining Feature selection and all three saves. Returning to Account exposes the restored username at byte selection 7:7 with the same owner and a newer revision.

Offsets are UTF-8 bytes at scalar boundaries: `Ada本` ends at byte 6; `Ada本!` and `日😀` each end at byte 7. Each accepted text edit or caret/byte-selection command advances the edit revision; section selection preserves it. The [logical editing contract](../architecture/input.md#implemented-logical-string-editing) owns bounds and rejection rules. TextInput exposes label, committed bytes and selection as ordinary Text/button semantics; it supplies no native editable accessibility adapter or caret geometry.

## Pick a theme preference

The Theme Dropdown sits after the selected category's field and before the summary and Apply/Cancel buttons. Its `system`, `light` and `dark` keys are ordinary saved preferences. `SettingsData::theme` starts at `system`; the application palette remains defined by `application_styles`.

```cpp
ui::Dropdown("Theme", theme,
    {{"system", "System", "settings.theme.system"},
     {"light", "Light", "settings.theme.light"},
     {"dark", "Dark", "settings.theme.dark"}},
    "settings.theme.toggle",
    {.node = {.id = "theme", .classes = {"control"}}, .expanded = theme_open});
```

`theme-trigger` names the current value and `[+]` or `[-]`. Activation toggles the host-owned `theme_open` candidate. Expanded options appear inline as ordinary buttons with IDs such as `theme-option-dark`; the selected label has a `* ` prefix. `FocusNext`/`FocusPrevious` traverse them and the same pane policy reveals them. Keyboard/gamepad `Activate` or pointer release selects an option, closes the choices and restores/reveals trigger focus. Normalized `Cancel` or an unmodified, non-repeating Escape closes the choices without changing the draft. There is no focus trap or overlay; normal navigation can reach the other application controls while choices are open.

The host changes only `next_draft.theme` on selection. It accepts the key and expansion flag after complete staging, preserving username editing, feature state, category and saved copies. Category replacement and scrolling retain the pending preference. Apply copies the whole draft and appends `; theme <key>` to the saved result. The Cancel button restores the last applied theme along with username and feature.

The script adds `?` to the username, opens/dismisses the Dropdown through normalized Cancel and raw Escape (ignoring modified/repeated Escape), selects Dark by logical gamepad activation, switches to About, selects Light by pointer release and Applies once. It then selects Dark and uses the Cancel button to restore Light. Inspection and repeated CPU captures check accepted checkpoints; earlier owned saves stay unchanged. `DropdownProps::disabled` disables its trigger and every option; `DropdownOption::disabled` disables one choice. Invalid selected keys and empty/duplicate option keys return source-located lowering diagnostics, as defined by the [form contract](../architecture/ui-model.md#implemented-c-form-authoring).

## Stage, accept and observe

`prepare` returns a complete candidate only after lowering, command eligibility, tree creation, style resolution, layout, inspection and paint succeed. Until then, the accepted host draft, save records and presentation stay intact. On a returned staging failure, `operate` restores the three session observations to accepted host values and exits with the stage diagnostics. It does not publish the failed candidate or save it. The consumed command sequence and candidate generation counter are not rewound. Allocation failures and arbitrary exceptions have no general rollback guarantee; rejection/retry and stale-owner injection belong to the [Settings regression fixture](../../examples/settings/main.cpp).

The sheet, shaper and lowered source/style inputs are borrowed synchronously during preparation. The accepted presentation owns its command snapshot, tree, resolved styles, layout, inspection and paint. Dispatch/command views borrow that presentation only until their calls return; replacement invalidates old tree handles. `focus.refresh` recovers by explicit author ID after acceptance. Session publication follows successful preparation.

`observe` resolves author IDs from owned inspection and compares selected markers/name, the conditional branch, username/byte selection or Checkbox when present, summary and saved result with host observations. It checks nonempty geometry and revealed keyboard focus in the 640×240 logical viewport, repeats CPU captures at accepted checkpoints, and checks visible branch and draft changes. Offscreen content retains geometry and semantics; an Apply result below the form clip is checked through inspection rather than an expected pixel change. Capture fixes scale one and time zero. The rasterizer outlives its borrowed glyph cache, and the cache outlives the capture adapter; capture returns owned RGBA8 bytes in memory. The [larger fixture](../integrations/settings-app.md) demonstrates dynamic section-list mutation, constrained scrolling, richer styles and replay of accepted views through the same runtime. The two-pane scroll/reveal policy below supplies constrained scrolling for this starter.

## Scroll the sidebar and form

`ui::ScrollView(children, props)` composes a Column with scroll overflow. Set its width and height through `Dimension::points`; its viewport clips descendants. Offsets remain host data. This widget supplies no scrollbar or native input adapter.

`ScrollOffsets` stores applied offsets under `categories` and `settings-form`, independently of transient node indices. `prepare` resolves these IDs in each newly lowered tree and builds the per-node layout offset span. `stage` prepares a complete candidate, refreshes a copied focus dispatcher, and optionally uses `scroll_into_view` to prepare again with the offsets needed to reveal focus. `accept` stores layout's clamped offsets only after staging succeeds, then refreshes focus and pointer state against the accepted tree. Replaced-tree pointer presses are cancelled by the ordinary dispatcher.

Wheel events use `route_scroll` at their normalized pointer position and stage only when offsets change. They preserve logical focus without revealing it, and execute no host command. Logical focus navigation and accepted command replacements reveal focus before acceptance; moving from the sidebar to a field or Apply/Cancel therefore reaches controls outside the initial clip. Pointer dispatch uses that same clipped geometry and activates only on release. Category clicks leave logical focus unchanged; closing Dropdown choices explicitly restores its trigger focus. A native adapter supplies physical events and frame scheduling.

The smoke traverses every control by keyboard while checking that it is revealed, retains all edit/Apply/Cancel checks, and sends oversized positive/negative wheel deltas to each pane. It checks independent offsets, clamping, unchanged focus and application values. After wheel-revealing About, pointer press/release selects About then Account through ordinary commands, retaining the draft and three saved copies. This checkpoint has 24 commands; the subsequent Dropdown sequence brings the final count to 38. Scrolling and navigation add no application commands.
