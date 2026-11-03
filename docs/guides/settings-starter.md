# Select, scroll, edit, Apply and Cancel Settings

Extend [your first app](first-app.md) with the [Settings starter](../../examples/settings-starter/main.cpp), its [CMake project](../../examples/settings-starter/CMakeLists.txt) and [Windows host](../../examples/settings-starter/native_win32.hpp). `Field` and `Settings` compose the application; `Host`, `prepare`, `operate` and `input` provide the explicit host boundary. The [form API](../architecture/ui-model.md#implemented-c-form-authoring) and [Settings consumer](../integrations/settings-app.md#implemented-copyable-editapply-starter-boundary) own their contracts.

## Copy, build and run

Use the compiler and CMake prerequisites from [Getting Started](getting-started.md#prerequisites). From the repository root:

```powershell
New-Item -ItemType Directory -Path build-settings-starter-source -Force | Out-Null
Copy-Item examples/settings-starter/main.cpp, examples/settings-starter/CMakeLists.txt, examples/settings-starter/native_win32.hpp -Destination build-settings-starter-source
cmake -S build-settings-starter-source -B build-settings-starter -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_SOURCE_DIR=C:/dev/tessera
cmake --build build-settings-starter --config Debug --parallel 4 -- /nodeReuse:false
& .\build-settings-starter\Debug\settings_starter.exe
ctest --test-dir build-settings-starter -C Debug --output-on-failure --no-tests=error
cmake --build build-settings-starter --config Release --parallel 4 -- /nodeReuse:false
ctest --test-dir build-settings-starter -C Release --output-on-failure --no-tests=error
```

Replace `C:/dev/tessera` with the absolute checkout path. The three copied files are self-contained application sources; they may live outside the checkout. The project consumes only the public `tessera::core` target, inherits C++20 and public includes, and disables optional fonts/Vulkan. On Windows the application also links the Windows SDK's `user32` and `gdi32`. Embedded Tessera defaults exclude its tests/examples; the copied project's own default CTest runs the window-free `settings_starter` sequence.

For local-source FetchContent, configure a separate directory with the same source:

```powershell
cmake -S build-settings-starter-source -B build-settings-starter-fetch -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_SOURCE_DIR=C:/dev/tessera -DTESSERA_IMPORT_MODE=FetchContent
cmake --build build-settings-starter-fetch --config Debug --parallel 4 -- /nodeReuse:false
ctest --test-dir build-settings-starter-fetch -C Debug --output-on-failure --no-tests=error
cmake --build build-settings-starter-fetch --config Release --parallel 4 -- /nodeReuse:false
ctest --test-dir build-settings-starter-fetch -C Release --output-on-failure --no-tests=error
```

No network fetch is needed for either import. Independent checks copy these exact three files through the [consumer driver](../../tests/cmake/run-consumer.cmake). Actual configurations and results belong to [support](../reference/support-matrix.md#settings-native-host-evidence--2026-11-03).

## Run in a Windows window

From the copied project built above:

```powershell
& .\build-settings-starter\Debug\settings_starter.exe --native
& .\build-settings-starter\Debug\settings_starter.exe --native-smoke
```

For the repository target, use `build/Debug/tessera_settings_starter.exe` with the same arguments. `--native` opens an interactive window; `--native-smoke` opens it, exercises the window procedure, verifies application values and presentation, then closes it. Both require a Windows desktop session. Other platforms report that the native host requires Windows; running without arguments keeps the deterministic window-free sequence described below.

- Click a category or control. A primary press focuses it; TextInput clicks focus without executing an empty edit. A dotted outline marks fully visible logical focus.
- Tab and Shift+Tab traverse all controls and reveal them inside each pane. Enter or Space activates buttons, Checkboxes and Dropdown choices. In Username, Space inserts text and Enter leaves the draft unchanged.
- Type in Username. Control+A selects the whole field; Left/Right, Home/End, Shift-selection, Backspace and Delete use the existing revisioned editing commands. Committed UTF-16 `WM_CHAR` pairs become one UTF-8 insertion. Clipboard and composition/IME integration are outside this host.
- Wheel over either pane to scroll independently; horizontal wheel messages scroll horizontally. Choose Theme inline, or Escape to dismiss its choices. On Volume, arrows step and Home/End select limits; drag its track to adjust it.
- Apply appends one in-memory settings copy. Cancel restores the most recently applied copy while retaining the selected category. Saves last only for this process.

The host presents the ordinary CPU capture through a GDI DIB and supplies readable fixed-cell GDI glyph coverage for the existing placeholder scalar metrics. It scales and centers the fixed 640×240 logical canvas inside the client area; resizing and DPI changes retain that layout and use the same mapping for pointer input. It does not perform native text shaping or make portable font-image claims. Native pointer capture continues Slider drags outside the window; capture loss, focus loss, cancellation and resize end the gesture while retaining its last accepted value. Every UI mutation uses the same `input`, staging and command path as the scripted example. Win32 types remain in the application's host header.

To register the native smoke with CTest, configure either the copied project or repository build with `-DTESSERA_TEST_NATIVE_HOST=ON`, rebuild, then run:

```powershell
ctest --test-dir build-settings-starter -C Debug -R '^settings_starter_native$' --output-on-failure --no-tests=error
```

This opt-in test has the `native` label and a 60-second timeout. It checks window presentation, pointer focus, committed text including a surrogate pair, Tab reachability, independent wheel scrolling, Dropdown choice, keyboard and captured Slider editing, capture loss, Apply/Cancel retention, activation repeat suppression and resized pointer mapping. Default headless/import tests do not require a desktop.

## Run the scripted sequence

The program supplies normalized input itself. Each checkpoint prints `Section <id>`, the `User ...` summary and the saved result, in this order:

| Checkpoint | Section | Username / feature | Saved result |
| --- | --- | --- | --- |
| Edit and toggle before first Apply | feature | Adadiscard日😀 / enabled | Not saved |
| Keyboard Cancel | feature | Ada / disabled | Not saved |
| Initial | account | Ada / disabled | Not saved |
| Insert `本` | account | Ada本 / disabled | Not saved |
| Select Feature | feature | Ada本 / disabled | Not saved |
| Toggle | feature | Ada本 / enabled | Not saved |
| Gamepad Apply | feature | Ada本 / enabled | Saved 1: Ada本; feature enabled; theme system; volume 50 |
| Select About | about | Ada本 / enabled | Saved 1: Ada本; feature enabled; theme system; volume 50 |
| Return to Account | account | Ada本 / enabled | Saved 1: Ada本; feature enabled; theme system; volume 50 |
| Insert `!` | account | Ada本! / enabled | Saved 1: Ada本; feature enabled; theme system; volume 50 |
| Keyboard Apply | account | Ada本! / enabled | Saved 2: Ada本!; feature enabled; theme system; volume 50 |
| Control+A | account | Ada本! / enabled, selection 0:7 | Saved 2: Ada本!; feature enabled; theme system; volume 50 |
| Replace with `日😀` | account | 日😀 / enabled | Saved 2: Ada本!; feature enabled; theme system; volume 50 |
| Keyboard Apply | account | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system; volume 50 |
| Edit and toggle after Apply | feature | 日😀! / disabled | Saved 3: 日😀; feature enabled; theme system; volume 50 |
| Gamepad Cancel | feature | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system; volume 50 |
| Return to Account | account | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system; volume 50 |
| Wheel each pane to both limits | account | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system; volume 50 |
| Pointer About then Account | account | 日😀 / enabled | Saved 3: 日😀; feature enabled; theme system; volume 50 |
| Pending username and Dark preference | account | 日😀? / enabled | Saved 3: 日😀; feature enabled; theme system; volume 50 |
| About, pointer Light, Apply | about | 日😀? / enabled | Saved 4: 日😀?; feature enabled; theme light; volume 50 |
| Select Dark, then Cancel | about | 日😀? / enabled | Saved 4: 日😀?; feature enabled; theme light; volume 50 |

The numeric sequence then focuses Volume, checks endpoints/steps and pointer dragging, Applies volume 75, edits both volume and username across categories, and Cancels back to that fifth save. Earlier saves remain unchanged. It ends with:

```text
5 owned saves; 53 host commands; 640x240 RGBA8 capture in memory (placeholder text, synthetic glyphs; no window)
```

The process exits nonzero with diagnostics on failure. Running without arguments is window-free and uses placeholder text and synthetic glyph marks; the Windows modes above supply the window, physical input normalization and display services.

## Compose the application and its styles

`Settings` accepts the selected section, theme key, expansion flag, integer volume and four controlled observations: username edit state, enabled boolean, derived summary string and saved-result string. Iteration over the fixed `account`, `feature`, `about` list creates ordinary Buttons with `section-<id>` identities; `* ` marks the selected choice and `selected-section` names it. A C++ branch composes only Account's TextInput, Feature's Checkbox or About's help. A fixed title sits above a Row containing two `ScrollView` panes: the 140×64 category sidebar and 440×144 form. The form contains the selected-section label, conditional branch, Theme Dropdown, summary, Apply/Cancel Row, saved result and Volume Slider. Both panes use 12-unit gaps in a 640×240 logical viewport. `Field` is a reusable typed function with a default gap and ordered children. Explicit IDs identify `username`, `toggle`, `section-help`, `summary`, `apply`, `cancel` and `save-result` across full-tree replacement.

`application_styles` keeps reusable form, control and status class declarations separate from composition. `Props::classes` attaches them; Apply supplies a typed instance background override. `prepare` passes the sheet and lowered instance declarations to the shared resolver. Change that palette to restyle the application through the existing cascade. Theme switching and interaction-state rules are demonstrated in the [larger Settings fixture](development.md#settings-form-fixture).

One session owns named `username`, `enabled` and `save-result` observations. A pure computed summary reads username and enabled. The form holds weak handles, so the session must outlive all lowering and publication on its creating UI thread. The `form(selected, theme, open)` lambda explicitly calls `Settings` to compose a candidate Element tree before each `prepare`; `session.lower` settles and copies values without rerunning C++ branches. Removing a branch does not remove host/session state, and this composition needs no per-section component mount or keyed local state.

## Own the draft and saved copies in the host

`Host::username` owns the accepted `TextEditState`: a never-reused nonzero owner, revision, committed UTF-8 bytes and byte anchor/caret. `Host::draft` owns `{username, feature, theme, volume}` and `Host::selected` owns section selection independently of text selection. The session values observe that host state; they do not own application saving. These lifetimes span all branch replacements, so returning to Account restores the exact edit owner, revision and selection.

`Host::execute` proposes copies in `next_username`, `next_draft`, `next_result`, `next_selected` and `next_theme_open`. Editing calls the pure `edit_text` transform with an owned request matching the accepted owner/revision. Toggling changes the candidate boolean; section commands change only candidate selection. Apply formats the next save result. Only after the whole candidate presentation succeeds does `operate` append an owned `SettingsData` copy to `saves`, accept host values and publish. Section changes append no save, and further draft edits cannot mutate an earlier record or its displayed result. This synchronous in-memory operation has no persistence or asynchronous/fallible save protocol.

`Host::applied` starts with the initial draft and changes only when an Apply presentation is accepted. Cancel restores that copy, preserving the selected section, saved copies and saved-result Text. Two pure `edit_text` transforms select all current bytes and replace them with the applied username; the owner stays the same, revision advances twice, and selection collapses at the restored buffer end. Both transforms remain local until preparation succeeds. Cancel therefore also works while Account is absent.

The host registers ordinary action/command mappings for editing, toggling, Apply/Cancel, section selection, the Dropdown and Volume, and publishes explicit eligibility for each presentation. `operate` resolves the live action owner against the accepted command generation and semantic input, then uses one executor and increasing sequence stream. Inspecting an outer execution result alone is insufficient: the host checks its outcome before staging. Each logical keyboard/gamepad Apply or Cancel uses its ordinary descriptor and host operation after dispatch returns.

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

## Adjust a numeric preference

Volume sits at the end of the form, after the saved result, and remains present across category changes. Its integer draft starts at 50, with range 0 through 100 in steps of 5:

```cpp
constexpr ui::SliderRange volume_range{0, 100, 5};
ui::Slider("Volume", volume, volume_range, "settings.volume",
    {.node = {.id = "volume", .classes = {"control"}}});
```

Focus navigation reveals the complete control. While Volume holds focus, unmodified Left/Down decrease one step, Right/Up increase one step, and Home/End select the endpoints. Repeated keys follow the same step policy. Bounds clamp without executing redundant commands; modified keys and keys focused elsewhere do not edit volume. Ordinary logical or semantic activation increases one step up to maximum.

A primary press on the visible track immediately selects the nearest stop and focuses Volume. The host uses ordinary clipped `hit_test` before taking capture. `volume-track` supplies geometry: normalize X between thumb centers at track origin plus 4 and track right minus 4, then call `ui::slider_value_at`. A host-owned pointer identity survives full-tree replacements; moves and primary release continue to resolve the current `volume` command owner and clamp outside-track positions. Other pointers, secondary buttons and additional presses cannot modify that capture. `PointerCancel` ends capture while retaining the last accepted draft; its later release does nothing. Non-pointer navigation/editing and a wheel offset change also end capture. A native host must supply OS capture/loss events; this script only supplies normalized events.

Each changed value uses `settings.volume`, one sequence and the same full-presentation staging as other edits. The optional owned request carries the proposed integer; a payload-free activation proposes one increment. Invalid ranges, off-step or out-of-range values reject with actionable diagnostics before candidate acceptance. The smoke stages volume 101 and checks that accepted host values, saves and presentation remain unchanged. The owner tests cover source locations, invalid ranges, step snapping, nonfinite positions and full integer endpoints.

Apply copies volume with the rest of the draft and appends `; volume <value>` to the result. Cancel restores it from the applied copy, retaining category selection and saved records. Category/Dropdown changes and numeric editing preserve the independent username edit owner/revision/selection. The final sequence checks keyboard limits/repeat/modifiers, visible pixel changes, primary drag across replacements, outside-track clamping, foreign-pointer/secondary-release rejection, capture cancellation, fifth Apply at 75 and Cancel of pending numeric/text edits.

## Stage, accept and observe

`prepare` returns a complete candidate only after lowering, command eligibility, tree creation, style resolution, layout, inspection and paint succeed. Until then, the accepted host draft, save records and presentation stay intact. On a returned staging failure, `operate` restores the three session observations to accepted host values and exits with the stage diagnostics. It does not publish the failed candidate or save it. The consumed command sequence and candidate generation counter are not rewound. Allocation failures and arbitrary exceptions have no general rollback guarantee; rejection/retry and stale-owner injection belong to the [Settings regression fixture](../../examples/settings/main.cpp).

The sheet, shaper and lowered source/style inputs are borrowed synchronously during preparation. The accepted presentation owns its command snapshot, tree, resolved styles, layout, inspection and paint. Dispatch/command views borrow that presentation only until their calls return; replacement invalidates old tree handles. `focus.refresh` recovers by explicit author ID after acceptance. Session publication follows successful preparation.

`observe` resolves author IDs from owned inspection and compares selected markers/name, the conditional branch, username/byte selection or Checkbox when present, summary and saved result with host observations. It checks nonempty geometry and revealed keyboard focus in the 640×240 logical viewport, repeats CPU captures at accepted checkpoints, and checks visible branch and draft changes. Offscreen content retains geometry and semantics; an Apply result below the form clip is checked through inspection rather than an expected pixel change. Capture fixes scale one and time zero. The rasterizer outlives its borrowed glyph cache, and the cache outlives the capture adapter; capture returns owned RGBA8 bytes in memory. The [larger fixture](../integrations/settings-app.md) demonstrates dynamic section-list mutation, constrained scrolling, richer styles and replay of accepted views through the same runtime. The two-pane scroll/reveal policy below supplies constrained scrolling for this starter.

## Scroll the sidebar and form

`ui::ScrollView(children, props)` composes a Column with scroll overflow. Set its width and height through `Dimension::points`; its viewport clips descendants. Offsets remain host data. This widget supplies no scrollbar or native input adapter.

`ScrollOffsets` stores applied offsets under `categories` and `settings-form`, independently of transient node indices. `prepare` resolves these IDs in each newly lowered tree and builds the per-node layout offset span. `stage` prepares a complete candidate, refreshes a copied focus dispatcher, and optionally uses `scroll_into_view` to prepare again with the offsets needed to reveal focus. `accept` stores layout's clamped offsets only after staging succeeds, then refreshes focus and pointer state against the accepted tree. Replaced-tree pointer presses are cancelled by the ordinary dispatcher.

Wheel events use `route_scroll` at their normalized pointer position and stage only when offsets change. They preserve logical focus without revealing it, and execute no host command. Logical focus navigation and accepted command replacements reveal focus before acceptance; moving from the sidebar to a field or Apply/Cancel therefore reaches controls outside the initial clip. Button pointer dispatch uses that same clipped geometry, focuses its owner on a primary press and activates only on release; Slider track input follows the numeric policy above. Closing Dropdown choices explicitly restores its trigger focus. The Windows adapter supplies physical events and frame scheduling.

The smoke traverses every control by keyboard while checking that it is revealed, retains all edit/Apply/Cancel checks, and sends oversized positive/negative wheel deltas to each pane. It checks independent offsets, clamping, unchanged focus and application values. After wheel-revealing About, pointer press/release selects and focuses About; scrolling the sidebar back reveals Account for the return click. Both commands retain the draft and three saved copies. This checkpoint has 24 commands; the subsequent Dropdown sequence brings the count to 38; numeric editing and its Apply/Cancel sequence bring the final count to 53. Scrolling and navigation add no application commands.
