# Compose renderer telemetry and controls

The [panel](../../examples/renderer-diagnostics/panel.hpp) accepts owned, backend-neutral display data. Its [composition](../../examples/renderer-diagnostics/panel.cpp) uses ordinary Text, Row, Column, Button and Checkbox values. The [host](../../examples/renderer-diagnostics/host.hpp) connects snapshot injection, disclosure, direct/ambient strength, key-direction components, MSAA, outlines, scene-lights, material-view and reset commands, controlled editing, constrained scrolling, focus reveal and synthetic camera arbitration. The [consumer contract](../integrations/renderer-diagnostics.md#implemented-telemetry-panel-boundary) owns data, identity and publication rules; [support](../reference/support-matrix.md#renderer-reset-controls-evidence--2026-10-27) owns configurations and verification evidence.

## Copy, build and run

Use the prerequisites from [Getting Started](getting-started.md#prerequisites). Copy all seven files from the example into an independent source directory:

```powershell
New-Item -ItemType Directory -Path build-diagnostics-source -Force | Out-Null
Copy-Item examples/renderer-diagnostics/CMakeLists.txt, examples/renderer-diagnostics/main.cpp, `
    examples/renderer-diagnostics/panel.hpp, examples/renderer-diagnostics/panel.cpp, `
    examples/renderer-diagnostics/host.hpp, examples/renderer-diagnostics/host.cpp, `
    examples/renderer-diagnostics/sample_data.hpp -Destination build-diagnostics-source
cmake -S build-diagnostics-source -B build-diagnostics -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_SOURCE_DIR=C:/dev/tessera
cmake --build build-diagnostics --config Debug --parallel 2 -- /nodeReuse:false
& .\build-diagnostics\Debug\renderer_diagnostics.exe
ctest --test-dir build-diagnostics -C Debug --output-on-failure --no-tests=error
cmake --build build-diagnostics --config Release --parallel 2 -- /nodeReuse:false
ctest --test-dir build-diagnostics -C Release --output-on-failure --no-tests=error
```

Replace `C:/dev/tessera` with your absolute checkout path. The [copied CMake project](../../examples/renderer-diagnostics/CMakeLists.txt) links only `tessera::core`, inherits its C++20/include requirements and disables optional font/Vulkan discovery. Embedded Tessera defaults omit its development targets. For local-source FetchContent, use a separate binary directory:

```powershell
cmake -S build-diagnostics-source -B build-diagnostics-fetch -G "Visual Studio 18 2026" -A x64 `
    -DTESSERA_SOURCE_DIR=C:/dev/tessera -DTESSERA_IMPORT_MODE=FetchContent
cmake --build build-diagnostics-fetch --config Debug --parallel 2 -- /nodeReuse:false
ctest --test-dir build-diagnostics-fetch -C Debug --output-on-failure --no-tests=error
cmake --build build-diagnostics-fetch --config Release --parallel 2 -- /nodeReuse:false
ctest --test-dir build-diagnostics-fetch -C Release --output-on-failure --no-tests=error
```

Both imports use the existing checkout without network fetching. The [independent consumer driver](../../tests/cmake/run-consumer.cmake) copies these exact seven files. The copied project's CTest executes its small synthetic main; the separate [repository fixture](../../tests/integrations/renderer_diagnostics_tests.cpp) owns failure injection and CPU/replay assertions.

The [main](../../examples/renderer-diagnostics/main.cpp) opens A, navigates to the counter disclosure and collapses it, then injects B/C. It enters `1.5` in the direct-strength field with normalized text/selection keys, refreshes telemetry before Enter/Apply, activates Increase with a gamepad source and invokes Decrease semantically. It repeats entry/Enter/adjustment for ambient strength, accepting `0.75` while preserving direct strength. It then selects MSAA 4 semantically, enables support for 8 and requests 8 through the typed node-free path. It activates Outlines semantically, then sends an equal Boolean node-free set. It then turns Scene lights off semantically and sends an equal Boolean node-free set. It selects Normals semantically, requests Base color through an exact string node-free command. It increases the key-direction z component semantically, enters `-0.5` in the x component with normalized keys and Enter, and requests y `0.25` through the typed node-free path, then refreshes telemetry while retaining every choice. It prints the key direction, accepted material view, scene lights, outlines, samples and numeric/draft values, setting-handler attempts, accepted changes and camera deliveries alongside telemetry. Finally it activates Reset controls semantically and prints the restored initial settings. Its output is:

```text
Key direction -0.5, 0.25, -0.75
Material view base-color
Scene lights off
Outlines on
MSAA 8; supported choices 4
Ambient strength 0.75; draft 0.75
Direct strength 1.5; draft 1.5; setting attempts 17; changes 15; camera events 0
value-frame-summary: 20 ms / 50 FPS / 40 ms
history-summary: History unavailable: no samples
value-gpu-geometry: GPU timestamps unavailable
disclosure-counters: counters (collapsed)
Reset direct 1; ambient 0.5; key direction 0, 0, -1; MSAA 1; outlines off; scene lights on; material view shaded; setting attempts 18; changes 16
Focus key-x-input; offset 932; disclosure requests 1 (synthetic host; no window)
```

These inputs are [declared synthetic data](../../examples/renderer-diagnostics/sample_data.hpp). The program exits nonzero and prints located diagnostics if an operation fails. A real host supplies its own window, normalized input and rendering/text services.

## Compose and refresh the panel

`Snapshot` owns strings, extent, summary measurements, stable named CPU/GPU timings, counters and at most 32 history samples. `Statistics` stores mean/p95/max/stddev in milliseconds. `Timing::milliseconds` being absent requires an explanatory `unavailable` string; a present zero tuple displays numeric zero. Counters retain integer values and explicit units. Frame samples retain increasing nonzero identities independently of their measurements.

`Panel(snapshot, disclosure, numeric, msaa, outlines, scene_lights, ambient, material_view, key_direction)` is a pure C++ composition function. Call `validate(snapshot)` before direct use, or use `Host::open`/`refresh`, which validate before composing. Rows have a 200-point label column and a 484-point value column. The root scroll owner has 12-point padding and gaps. `panel_styles()` supplies reusable panel/disclosure class declarations to the ordinary cascade. The history uses empty colored Columns in an end-aligned Row; its textual summary and geometry derive from the same snapshot. No custom draw callback or chart widget is involved.

For application embedding, copy `panel.hpp/.cpp` and `host.hpp/.cpp`, replace the synthetic data provider with your own immutable `Snapshot`, and call `Host::open(snapshot, text)` on the UI thread. `refresh(next, text)` stages a full replacement and accepts it only after layout, inspection and paint succeed. Sampling, renderer aggregation, device/resource lifetimes and scheduling belong to the caller. The panel receives no renderer pointers. `view()` exposes borrowed coherent inputs for synchronous consumption, plus owned inspection/paint records; references into that view expire on acceptance of a replacement.

`Disclosure`, `Numeric`, `Msaa`, `Outlines`, `SceneLights`, `MaterialView` and `KeyDirection` are host state independent of telemetry. Buttons have explicit expanded/collapsed labels and stable `disclosure-cpu`, `disclosure-gpu`, `disclosure-counters`, `disclosure-controls` IDs. Collapsing removes that group's rows while keeping the injected snapshot. Stable rows use `cpu-<id>`, `gpu-<id>` and `counter-<id>`; their inspectable value Text uses `value-<row-id>`. History samples use `history-frame-<frame-id>`. Reordering measurements does not turn a value or vector index into identity.

## Route input and accept a generation

`route(event, source, text)` wraps `input` for synthetic camera arbitration. It accepts normalized pointer, Scroll and logical navigation/activation events with a nondecreasing microsecond clock. The platform maps physical keys and gamepad buttons to logical events. Focused numeric editing consumes normalized text/editing keys before navigation. Raw Tab/arrows and Enter/Space map once to logical operations when not used by editing. Primary pointer press focuses an eligible activation owner; release activates through the ordinary dispatcher. Scroll routes at the hit point and reads back the applied clamped offset. Logical navigation stages a copied focus dispatcher and reveals the recovered author-ID owner before acceptance. Pointer dispatcher state is cleared on accepted tree replacement; a host input claim survives until release/cancel. Read `last_consumed()` even when a recognized panel event returns an error. The wrapper counts a camera delivery only for a valid unconsumed event.

`invoke(author_id, source, text)` uses ordinary semantic activation and command eligibility. `request`/`execute` expose the same split for a caller that needs to hold a request; a replacement or disabled owner rejects that request under the command contract. One executor consumes increasing sequences across all routes. `eligibility` stages three telemetry disclosure flags; `numeric_eligibility(enabled, text)` stages the direct field flag; the `NumericField` overload selects direct or ambient with its retained draft and full display. Read the outer result and the `CommandExecution::outcome` when calling `execute` directly.

`stage` validates data, lowers the candidate, applies eligibility, builds the tree, resolves styles and computes layout/inspection/paint. A copied focus dispatcher can request a second layout preparation to reveal the focused control. `accept` then replaces snapshot, disclosure, both strength fields, the key-direction components, MSAA/outlines/scene-lights/material-view state, eligibility, applied offset and view, refreshes dispatchers and publishes the session. Refresh preserves an already valid offset and visible focus. Geometry changes clamp/reveal only when needed. Each command handler prepares a candidate while the old view stays alive through executor validation; acceptance follows successful execution.

Returned data/measurement/paint errors retain accepted snapshot, disclosure, focus, offsets, command snapshot and observations. Failed disclosure execution consumes its sequence; retry requires a fresh request. Attempt generations and input timestamps are monotonic bookkeeping and are not rolled back. Allocation failures and arbitrary exceptions have no general rollback guarantee. Setting handlers prepare synthetic values synchronously; external renderer execution requires its own acknowledgement policy.

## Edit and accept direct strength

`NumericControl` is a reusable application composition in `panel.cpp`, using ordinary TextInput and Buttons. `Host::numeric()` observes its accepted value, owned `TextEditState` draft and eligibility. Initial 1, bounds [0,4] and step 0.25 are consumer choices. Direct decimal entry can use other step fractions. Apply accepts the whole bounded decimal string, canonicalizes it and advances the edit revision; invalid/incomplete text stays pending and returns a located error. Decrease/Increase clamp the accepted value and are disabled while draft bytes differ from its canonical text. Cancel restores the accepted text without requesting a setting change. See the [consumer owner](../integrations/renderer-diagnostics.md#implemented-direct-strength-control-boundary) for grammar and lifetime details.

Use `request_edit({owner, revision, operation}, source)` followed by `execute` for semantic selection/insertion, or route committed TextInput and editing keys while `direct-input` is focused. Activating the field only focuses it. Enter applies; Escape or logical Cancel cancels. Gamepad users navigate to operation Buttons and Activate. `invoke("direct-apply", source, text)` and adjustment/Cancel IDs use the same command executor. `request_value("direct", value, source)` provides a typed node-free synthetic setting request and validates identity, bounds and canonical draft size. Read the outer result and execution outcome. `setting_attempts()` includes failed setting handlers, whereas `setting_changes()` counts accepted value changes. Editing, Cancel, refresh and disclosure make no setting change.

Refresh A/B/C, controls collapse and eligibility changes preserve exact pending bytes, selection and revision. Collapse recovers field/operation focus to `disclosure-controls`; expansion retains that header focus. A preparation failure preserves the draft, settings, focus, offset and all accepted view observations. Retry against the accepted view with a fresh request because the executor consumes failed sequences. The [repository fixture](../../tests/integrations/renderer_diagnostics_tests.cpp) owns these assertions and failure injection; the small copied main remains a working host example.

## Edit ambient strength independently

`Host::ambient()` observes the second numeric field, initially `0.5`, with the same [0,4] bounds, 0.25 step, decimal grammar, 32-byte draft limit and Apply/Cancel policy as direct strength. `NumericControl(value, NumericField::ambient)` gives it `ambient-*` author IDs and `ambient.*` actions. See the [consumer owner](../integrations/renderer-diagnostics.md#implemented-ambient-strength-control-boundary) for exact identities, lifetime and staging rules.

Use `request_edit(NumericField::ambient, {owner, revision, operation}, source)` followed by `execute` for revisioned selection/insertion, `request_value("ambient", value, source)` for a typed node-free set, and `numeric_eligibility(NumericField::ambient, enabled, text)` for independent eligibility. The edit owner and revision come from `host->ambient().draft`; a direct owner rejects even when the counters match. The direct helper forms continue to select direct strength. Inspect the outer result and execution outcome on both fields.

Focused normalized text and editing keys route to the selected field. Enter/Escape invoke that field's Apply/Cancel; logical Cancel from its operation Buttons uses the same Cancel owner. Pointer, keyboard/gamepad traversal and semantic activation use `ambient-input`, `ambient-apply`, `ambient-cancel`, `ambient-decrease` and `ambient-increase`. Gamepad direct text requires an injected text source.

Changing or cancelling one field preserves the other's accepted value, exact draft bytes, reverse selection, lifetime owner, revision and eligibility. Different incomplete drafts can coexist; dirty adjustment eligibility is independent. MSAA/capabilities, either toggle, telemetry A/B/C, disable/re-enable and controls collapse/re-expansion preserve both drafts. Collapse from either field or its operations recovers/reveals the controls header. Rejected measurement/paint preparation preserves both fields and the complete accepted presentation, with bounded field/source-located diagnostics; an entered failed command consumes its sequence and a fresh request retries. These host assertions are separate from accepted-view replay.

## Choose MSAA and update capabilities

`MsaaControl` composes the stable `msaa-1`, `msaa-2`, `msaa-4`, `msaa-8` Buttons and `msaa-accepted` Text under controls disclosure. Read `Host::msaa()` for owned accepted samples, supported samples and host eligibility. The selected choice has an explicit label marker; unsupported choices remain visible and disabled. The [consumer contract](../integrations/renderer-diagnostics.md#implemented-msaa-choice-boundary) owns exact validation, retention and request accounting.

Use `invoke("msaa-4", source, text)` for semantic activation or route pointer release, normalized traversal/Navigate and Activate to that same owner. `request_value("msaa", 4, source)` followed by `execute` requests a typed node-free value through the shared executor. Check both the outer result and outcome. Fractional/unknown/unsupported numbers reject; strings and Booleans are not converted. Activating the selected choice accepts a presentation and adds a setting attempt without an accepted-value change.

`msaa_capabilities({1, 2, 4, 8}, true, text)` stages the supported set and eligibility with the full view. It issues no setting request, and a set excluding the accepted value rejects instead of choosing a replacement. Held requests expire on accepted replacement; obtain a fresh one. MSAA changes preserve the exact pending direct-strength draft, including invalid text, reverse selection, owner and revision. Collapse recovers focus to the controls header; capability/eligibility updates recover and reveal ordinary eligible focus. Failed presentation retains all accepted observations; an entered setting request consumes its sequence and retries with a fresh request.

## Toggle outlines

`Panel` composes a controlled Checkbox labeled `Outlines` with stable ID `outlines-toggle` under controls disclosure. `Host::outlines()` observes accepted Boolean and independent eligibility, initially false and true. Its ordinary button semantic name includes `[ ]` or `[x]`. The [consumer contract](../integrations/renderer-diagnostics.md#implemented-outlines-control-boundary) owns validation, retention and accounting.

Use `invoke("outlines-toggle", source, text)` or route pointer release, normalized keyboard/gamepad traversal/navigation and activation to that owner. Parameter-free activation inverts the accepted Boolean. `request_outlines("outlines", true, source)` followed by `execute` requests an exact typed Boolean through the same executor. Check the outer result and outcome; numbers/strings, wrong field identity and activation arguments reject. An equal-value set accepts a presentation with one handler attempt and no value change.

`outlines_eligibility(false, text)` stages eligibility without issuing a setting request. Numeric and MSAA eligibility are independent. Outlines changes, eligibility and telemetry refresh preserve exact pending direct-strength text, selection, owner/revision, accepted strength and MSAA state. Collapse recovers outlines focus to the controls header and preserves settings/drafts. Failed measurement or paint retains every accepted observation, consumes an entered command sequence and permits retry with a fresh request. Panel claims consume disabled hits and release/cancel across replacement, following the shared input route.

## Toggle scene lights

`Panel` composes the controlled `Scene lights` Checkbox after Outlines, with stable ID `scene-lights-toggle`. `Host::scene_lights()` observes its accepted Boolean and independent eligibility, both initially true. The [consumer contract](../integrations/renderer-diagnostics.md#implemented-scene-lights-control-boundary) owns validation, retention and request accounting.

Use `invoke("scene-lights-toggle", source, text)` or route pointer release, raw keyboard traversal/activation or logical gamepad Navigate/Activate to that owner. Parameter-free `scene_lights.toggle` inverts the accepted value. `request_scene_lights("scene_lights", false, source)` followed by `execute` requests an exact Boolean through `scene_lights.set`. Check both the outer result and execution outcome. Numbers/strings, missing/extra fields, wrong identity, activation arguments and stale/foreign requests reject. An equal-value set publishes one presentation with one setting attempt and no accepted value change.

`scene_lights_eligibility(false, text)` stages eligibility without issuing a setting request. Outlines, numeric and MSAA eligibility are independent. Either toggle preserves the other, accepted numeric/MSAA settings and exact pending direct-strength draft bytes, reverse selection, owner and revision, including incomplete `2.`. Refresh and controls collapse/re-expansion preserve these observations; collapse from Scene lights recovers and reveals `disclosure-controls`.

Measurement/paint failure preserves the accepted settings, eligibility, draft and complete view. An entered failed command consumes its sequence and needs a fresh request to retry. Read `last_consumed()` even on input errors so panel-claimed events cannot reach the camera. The repository fixture checks command execution and camera delivery separately from accepted-view CPU/replay reconstruction.

## Select a material view

`MaterialViewControl` composes the stable `material-view-shaded`, `material-view-normals` and `material-view-base-color` Buttons plus `material-view-accepted` Text under controls disclosure. `Host::material_view()` observes the owned accepted string and independent eligibility, initially `shaded` and true. Each Button includes its value and a selected marker when accepted. The [consumer contract](../integrations/renderer-diagnostics.md#implemented-material-view-selection-boundary) owns exact identities, validation and accounting.

Use `invoke("material-view-normals", source, text)` or route pointer release, keyboard traversal/activation or logical gamepad Navigate/Activate to a choice. Each parameter-free activation requests its fixed string. `request_material_view("material_view", "base-color", source)` followed by `execute` requests an exact string through `material_view.set`; inspect the outer result and execution outcome. Other field identities, unknown strings, non-string values, missing/extra fields and activation arguments reject. The `material_view.base-color` action binds to command `material_view.base_color` to respect command identifier grammar. An equal-value choice or set publishes a presentation with one setting attempt and zero value changes.

`material_view_eligibility(false, text)` stages only that eligibility without issuing a setting request. Other control eligibility is independent. Choice, eligibility and telemetry operations preserve both numeric accepted values and exact pending drafts, including selection, owner/revision, plus MSAA and both toggles. Moving focus from a numeric input to a choice does not commit it. Collapse from a material choice recovers/reveals `disclosure-controls`; expansion retains header focus and the accepted choice.

A rejected presentation preserves every accepted setting, both drafts and the complete view. An entered failed setting command counts one attempt, consumes its sequence and retries through a fresh request. The repository fixture verifies fixed-value routing, representative input paths and camera accounting separately from accepted-view CPU/replay reconstruction.

## Edit key direction components

`KeyDirectionControl` composes a `Key direction` label and three ordinary numeric compositions under controls disclosure. `Host::key_direction()` observes the owned `KeyDirection` x/y/z components, each a `Numeric` with its own accepted value, draft lifetime and eligibility, initially (0,0,-1). `Host::numeric(NumericField)` selects any numeric field. Each component accepts finite decimals in [-1,1] with the shared grammar, 0.25 clamped adjustment and Apply/Cancel policy; negative values keep their sign, as in `-0.25`, and negative zero is stored as `0`. At an endpoint the outward adjustment is disabled, so initial z offers Increase only. The [consumer contract](../integrations/renderer-diagnostics.md#implemented-key-direction-control-boundary) owns identities, routing and accounting.

Commands and typed set fields use `key_x`, `key_y` and `key_z`; author IDs use hyphens, such as `key-x-input`, `key-x-apply` and `key-z-increase`, because command segments cannot contain hyphens. `field_name`, `field_id`, `field_label` and `field_bounds` map a `NumericField` to those identities, its label and bounds. Use `request_edit(NumericField::key_x, {owner, revision, operation}, source)` with the owner and revision from `host->key_direction().x.draft`, `request_value("key_y", 0.25, source)` for a typed node-free set, and `numeric_eligibility(NumericField::key_z, enabled, text)` for one component's eligibility. Read the outer result and execution outcome. Another component's or strength field's owner rejects even when revisions match; unknown field identities reject rather than selecting direct strength.

Focused normalized text, editing keys and raw Enter/Escape route to the focused component; logical Cancel from its operation Buttons cancels that component. Pointer, keyboard/gamepad traversal and semantic activation use the same live owners. A component operation changes only that component: the synthetic host does not normalize the vector, rescale peers or reject a zero vector. Other components, both strength drafts and unrelated settings keep their exact observations through component operations, telemetry refresh, eligibility changes and controls collapse; collapse from a component recovers/reveals `disclosure-controls`. A rejected presentation preserves the accepted view, consumes the entered sequence and retries through a fresh request.

## Reset controls

`Panel` composes an ordinary `Reset controls` Button with stable ID `controls-reset` after material view under controls disclosure. Use `invoke("controls-reset", source, text)` or route pointer release, keyboard traversal/activation or logical gamepad Navigate/Activate to it. The single `controls.reset` command restores direct 1, ambient 0.5, key direction (0,0,-1), MSAA 1, outlines off, scene lights on and material view `shaded`, and replaces every numeric draft, including invalid pending text, with its canonical initial text. Each draft keeps its owner and advances its revision once. The [consumer contract](../integrations/renderer-diagnostics.md#implemented-reset-controls-boundary) owns eligibility and accounting.

Reset is available only while every affected control is eligible and the MSAA capabilities include sample count 1; otherwise the whole command is unavailable, and nothing is partially reset. Capabilities and eligibility are host state that reset retains. One reset counts one setting attempt and at most one accepted change; resetting already-initial settings still advances every draft revision. A rejected presentation preserves the complete accepted view and consumes the entered sequence; retry with a fresh request. Collapse from Reset recovers/reveals `disclosure-controls`.

## Inspect and reproduce an accepted view

The repository fixture fixes a 720×160 logical viewport, scale 1.25, locale `en`, placeholder shaping and synthetic scalar glyphs. It resolves exact values and units from owned inspection, checks history sizes/positions and shared clipping/hit geometry, then compares two offscreen runs with independently repeated CPU captures.

Replay starts from the accepted document and resolved styles at offset zero. A normalized Scroll at one microsecond reconstructs the applied offset; a tick at two microseconds fixes the observation clock. This reproduces accepted geometry, semantic records, paint and RGBA8 bytes. It does not serialize snapshots or execute the C++ refresh/disclosure host. Native renderer integration and real-font rendering require their own host and evidence.
