# Snapshot bundles and CLI

> [!IMPORTANT]
> **Maturity: Proposed — Direction.** Open decisions are Exploration; do not implement them as settled APIs.
> **Default scheduling: Unscheduled. Implementation authority: None.**
> Implement only behavior explicitly required by [current work](../roadmap/current.md) or an accepted task linked there. A reference activates no adjacent mechanisms; follow the [agent policy](../agent-policy.md).

## Proposed runner and snapshot bundle

Separate interactive application execution from deterministic one-shot observation. An offscreen runner owns host adapters for viewport/scale, time/frame count, normalized input, fixture resources, and declared application state. It must not require an OS window. The [implemented prototype](../architecture/inspection.md#implemented-prototype-offscreen-runner) covers recorded viewport/scale, locale, text-service identity, input, and declared completed-frame capture. Screenshots come from an explicitly declared backend: the [CPU reference backend](../architecture/rendering.md#implemented-scalar-cpu-reference-backend) supplies deterministic visual fixtures, and GPU capture requires a declared device/driver. Neither substitutes automatically for the other.

[Replay](../architecture/replay.md) owns reproducible inputs and action order. A snapshot is an observation of one settled generation, while a replay describes progression between generations. Snapshot bundle versioning is independent of UI-document, semantic, and replay versions. Proposed bundle contents:

| Artifact | Content and owner |
| --- | --- |
| Manifest | Bundle version, generation, fixture/input references, viewport/scale, locale, theme, font/asset identities, capture capabilities and rendering environment |
| Screenshot | Optional completed frame readback; [rendering](../architecture/rendering.md) owns target/color/lifetime rules |
| Tree | Semantic projection plus explicitly identified runtime inspection records |
| UI | Backend-neutral accepted document, with explicit references to any component/state fixture inputs needed to reproduce it |
| State | Host-declared reproducible input state, excluding private or sensitive slots |
| Layout | Logical geometry and available clip/scroll information from [layout](../architecture/layout.md) |
| Render | Owned command summary and backend counters; no SDK objects, addresses, or GPU handles |
| Diagnostics | Structured issues associated with the captured generation |
| Invariants | Owned declarations, actual observations, outcomes, and located diagnostics from the same generation |
| Sources | Owned source map and property/component provenance with explicit unavailable origins |
| Metrics | Allocation/memory and render counters when available, plus optional CPU/GPU timing with measurement conditions |

Capture CPU observations coherently, then associate completed readback with that same generation/frame. Do not combine a later semantic tree with an earlier image. Missing artifacts carry an explicit reason; a core-only snapshot needs no screenshot. Names and encoding of bundle members remain open until bounded validation, version rejection, and round trips are specified.

A small inspection/render CLI consumer loads a fixture, replays declared interactions, captures one coherent generation, and emits versioned machine-readable artifacts for CI, Path-finder, and agents. Select the scalar CPU renderer explicitly as the deterministic reference by default; unavailable requested backends are diagnosed under the runner boundary. Compare Vulkan output against the same scene under declared device/format tolerances. Inspection, rendering, bundle verification, and bundle comparison are intended operations, without committing to executable names, switches, or artifact filenames.

## Promotion criteria

Promote a bundle format or CLI only when a consumer must persist or exchange a reproducible capture beyond the in-process runner. Bound the required artifacts and validate unsupported versions/missing data without scheduling a transport stack.
