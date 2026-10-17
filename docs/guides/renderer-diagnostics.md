# Compose and host renderer telemetry

The [panel](../../examples/renderer-diagnostics/panel.hpp) accepts owned, backend-neutral display data. Its [composition](../../examples/renderer-diagnostics/panel.cpp) uses ordinary Text, Row, Column and Button values. The [host](../../examples/renderer-diagnostics/host.hpp) connects snapshot injection, disclosure commands, constrained scrolling and focus reveal. The [consumer contract](../integrations/renderer-diagnostics.md#implemented-telemetry-panel-boundary) owns data, identity and publication rules; [support](../reference/support-matrix.md#renderer-telemetry-evidence--2026-10-17) owns configurations and verification evidence.

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

The [main](../../examples/renderer-diagnostics/main.cpp) opens snapshot A, navigates to the lower counter disclosure with three logical focus steps, collapses it with a gamepad-sourced activation, then injects B and C. Its output is:

```text
value-frame-summary: 20 ms / 50 FPS / 40 ms
history-summary: History unavailable: no samples
value-gpu-geometry: GPU timestamps unavailable
disclosure-counters: counters (collapsed)
Focus disclosure-counters; offset 452; disclosure requests 1 (synthetic host; no window)
```

These inputs are [declared synthetic data](../../examples/renderer-diagnostics/sample_data.hpp). The program exits nonzero and prints located diagnostics if an operation fails. A real host supplies its own window, normalized input and rendering/text services.

## Compose and refresh the panel

`Snapshot` owns strings, extent, summary measurements, stable named CPU/GPU timings, counters and at most 32 history samples. `Statistics` stores mean/p95/max/stddev in milliseconds. `Timing::milliseconds` being absent requires an explanatory `unavailable` string; a present zero tuple displays numeric zero. Counters retain integer values and explicit units. Frame samples retain increasing nonzero identities independently of their measurements.

`Panel(snapshot, disclosure)` is a pure C++ composition function. Call `validate(snapshot)` before direct use, or use `Host::open`/`refresh`, which validate before composing. Rows have a 200-point label column and a 484-point value column. The root scroll owner has 12-point padding and gaps. `panel_styles()` supplies reusable panel/disclosure class declarations to the ordinary cascade. The history uses empty colored Columns in an end-aligned Row; its textual summary and geometry derive from the same snapshot. No custom draw callback or chart widget is involved.

For application embedding, copy `panel.hpp/.cpp` and `host.hpp/.cpp`, replace the synthetic data provider with your own immutable `Snapshot`, and call `Host::open(snapshot, text)` on the UI thread. `refresh(next, text)` stages a full replacement and accepts it only after layout, inspection and paint succeed. Sampling, renderer aggregation, device/resource lifetimes and scheduling belong to the caller. The panel receives no renderer pointers. `view()` exposes borrowed coherent inputs for synchronous consumption, plus owned inspection/paint records; references into that view expire on acceptance of a replacement.

`Disclosure` is host state independent of telemetry. Buttons have explicit expanded/collapsed labels and stable `disclosure-cpu`, `disclosure-gpu`, `disclosure-counters` IDs. Collapsing removes that group's rows while keeping the injected snapshot. Stable rows use `cpu-<id>`, `gpu-<id>` and `counter-<id>`; their inspectable value Text uses `value-<row-id>`. History samples use `history-frame-<frame-id>`. Reordering measurements does not turn a value or vector index into identity.

## Route input and accept a generation

`input(event, source, text)` accepts normalized pointer, Scroll and logical navigation/activation events with a nondecreasing microsecond clock. The platform maps physical keys and gamepad buttons to logical events. Raw keys/text return unconsumed. Pointer activation occurs on release through the ordinary dispatcher; this host has no pointer-press focus policy. Scroll routes at the hit point and reads back the applied clamped offset. Logical navigation stages a copied focus dispatcher and reveals the recovered author-ID owner before acceptance. Pointer state is cleared on accepted tree replacement.

`invoke(author_id, source, text)` uses ordinary semantic activation and command eligibility. `request`/`execute` expose the same split for a caller that needs to hold a request; a replacement or disabled owner rejects that request under the command contract. One executor consumes increasing sequences across all routes. `eligibility` stages three host capability flags together with the display. Read the outer result and the `CommandExecution::outcome` when calling `execute` directly.

`stage` validates data, lowers the candidate, applies eligibility, builds the tree, resolves styles and computes layout/inspection/paint. A copied focus dispatcher can request a second layout preparation to reveal the focused control. `accept` then replaces snapshot, disclosure, eligibility, applied offset and view, refreshes dispatchers and publishes the session. Refresh preserves an already valid offset and visible focus. Geometry changes clamp/reveal only when needed. Each command handler prepares a candidate while the old view stays alive through executor validation; acceptance follows successful execution.

Returned data/measurement/paint errors retain accepted snapshot, disclosure, focus, offsets, command snapshot and observations. Failed disclosure execution consumes its sequence; retry requires a fresh request. Attempt generations and input timestamps are monotonic bookkeeping and are not rolled back. Allocation failures and arbitrary exceptions have no general rollback guarantee. This synchronous synthetic host executes only disclosure commands; it supplies no external renderer execution or asynchronous acknowledgement policy.

## Inspect and reproduce an accepted view

The repository fixture fixes a 720×160 logical viewport, scale 1.25, locale `en`, placeholder shaping and synthetic scalar glyphs. It resolves exact values and units from owned inspection, checks history sizes/positions and shared clipping/hit geometry, then compares two offscreen runs with independently repeated CPU captures.

Replay starts from the accepted document and resolved styles at offset zero. A normalized Scroll at one microsecond reconstructs the applied offset; a tick at two microseconds fixes the observation clock. This reproduces accepted geometry, semantic records, paint and RGBA8 bytes. It does not serialize snapshots or execute the C++ refresh/disclosure host. Native renderer integration and real-font rendering require their own host and evidence.
