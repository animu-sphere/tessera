# Support matrix

Current capability scope and validation limits. Detailed historical checks remain in the [dated evidence record](https://github.com/animu-sphere/tessera/blob/cf86d0992d0f81f606b557e46ad0ddcba758eaf7/docs/reference/support-matrix.md); links into that revision preserve the original environments, results and caveats. Fresh v0.6.0 core Debug/Release, GPU, font and injected native-smoke validation is recorded in [release PR #152](https://github.com/animu-sphere/tessera/pull/152). Routine new results belong in PR/CI.

## Evidence vocabulary

| Status | Meaning |
| --- | --- |
| Planned | Design only |
| Implemented | Code exists; validation is stated separately |
| Validated | Recorded checks establish the stated scope |
| Unsupported | Explicitly rejected or excluded |

Missing evidence means unvalidated, not failed. Capabilities without a row have no claim here.

## Configuration status

| Configuration | Validated scope and limits |
| --- | --- |
| Core | Windows x64/MSVC Debug/Release, including widget owner checks and four independent add_subdirectory imports; no external runtime dependencies |
| Vulkan | One Windows x64 RTX A5000 configuration, offscreen fixtures and Win32 menu smokes; no hosted GPU CI or other-driver/platform claim |
| Fonts | HarfBuzz 10.2.0 / FreeType 2.13.3, vcpkg classic `x64-windows`, committed Noto fixtures; other acquisitions/triplets unvalidated |
| Native input | Win32 menu: one mouse, USB DualSense and original Bluetooth Joy-Con L/R pair; Settings host uses injected window-message smokes |
| Other platforms | Linux, macOS and other Windows toolchains unvalidated |
| Distribution | Source only; no install/export package or prebuilt binaries; [release records](../releases/README.md) |

## Capability status

Core rows refer to the Windows configuration above. A Debug-only owner check is not upgraded by a GPU consumer capture. Shared contracts and usage are linked rather than repeated.

| Area | Scope and limits | Details |
| --- | --- | --- |
| Hierarchy / Inspector application | Validated core Debug/Release and injected Windows CPU host: per-item name/enabled/amount edits, select/return, TreeView disclosure, scrolling, SplitPane, item menus and inspection/semantic/replay outcomes. Replay records host-built views, not application execution; clipboard scope is below. No physical-device, IME, real-shaping, GPU, native editable accessibility or persistence claim | [guide](../guides/inspector-hierarchy.md) |
| Inspector clipboard | Validated Debug/Release window-free and injected Windows checks: synchronous host-service Ctrl+C/V for the focused name editor, Unicode conversion, rejection/retry and changed-target guards. Win32 Unicode clipboard adapter implemented; checks use injected services and leave the OS clipboard untouched. Live OS clipboard exchange unvalidated; cut, asynchronous results and clipboard replay not implemented | [host boundary](../guides/inspector-hierarchy.md#host-and-observation-boundary) |
| Latest UI GPU regression | Validated Debug/Release: seven applications, 31 states at four scales; full GPU reference/batch/repeat, CPU comparison within 2/255 outside bounded raster edges (>95% retained). Placeholder text, no latest-widget native/real-font or performance claim | [PR #148](https://github.com/animu-sphere/tessera/pull/148), [workflow](../guides/development.md#latest-ui-gpu-regression) |
| Menu / ContextMenu | Validated core Debug/Release, single-level controlled command menus; no submenu/menu bar, richer items, Portal or native input | [PR #147](https://github.com/animu-sphere/tessera/pull/147), [guide](../guides/menu-context.md) |
| TreeView / ListView | Validated core Debug/Release, controlled single selection and disclosure; no multiple selection, virtualization or native input | [PR #146](https://github.com/animu-sphere/tessera/pull/146), [guide](../guides/tree-list.md) |
| Modal / Dialog | Validated core Debug/Release, one root-modal confirmation; no nested/stacked modals, modal-anchored Popups, Portal or native input | [PR #145](https://github.com/animu-sphere/tessera/pull/145), [guide](../guides/modal-dialog.md) |
| Popup / Tooltip | Validated core Debug/Release, root-sibling anchors and hover/focus help; no generalized Portal, modal scope or native input | [PR #144](https://github.com/animu-sphere/tessera/pull/144), [guide](../guides/popup-tooltip.md) |
| Grid | Validated core Debug/Release, explicit row-major tracks and Inspector edits; no spanning/implicit tracks or native input | [guide](../guides/grid.md), [contract](../architecture/layout.md#implemented-grid-placement) |
| Stack | Validated core Debug/Release, in-parent overlay toolbar; no Portal or native input | [guide](../guides/stack.md), [contract](../architecture/layout.md#implemented-stack-placement) |
| SplitPane | Validated core Debug/Release, scripted/native Settings resizing; no numeric accessibility range/orientation, screen-reader adapter or docking | [guide](../guides/split-pane.md) |
| Vertical scrollbar | Validated core Debug/Release, controlled sibling ScrollView composition, four-scale geometry/input/CPU/inspection/replay and scripted Settings commands; no horizontal control, track paging, numeric accessibility range or dedicated native/GPU scrollbar evidence | [guide](../guides/scrollbar.md) |
| Slider / Dropdown | Validated core Debug/Release, controlled preferences and both local-source imports; inline Dropdown choices, discrete integer Slider | [starter](../guides/settings-starter.md) |
| Settings starter | Validated Debug/Release, copied three-file app, drafts/sections/scrolling/Apply/Cancel and optional Windows CPU host; no persistence, IME/clipboard or native editable accessibility | [guide](../guides/settings-starter.md) |
| First app | Validated core Debug/Release and both local imports; synthetic CPU capture, no window, real font or device session | [guide](../guides/first-app.md) |
| Renderer telemetry / settings | Validated core Debug/Release synthetic panel, including material-view, key-direction and reset extensions, and both local imports; no native renderer/device integration | [guide](../guides/renderer-diagnostics.md) |
| C++ form authoring / Settings form | Validated core Debug/Release, controlled form, reusable styles and interaction feedback; one session, no persistence, dynamic mounting, editable semantics, native host or real-font capture | [authoring](../architecture/ui-model.md#implemented-c-form-authoring) |
| UI tree / serialization | Validated Box/Text documents, JSON v1, metadata and handles | [UI model](../architecture/ui-model.md), [format](../../formats/tessera-ui/README.md) |
| Reactive graph / cleanup / effects | Validated core, explicit host flush/publication/delivery and contained calculation faults; no presentation fallback | [reactive runtime](../architecture/reactive-runtime.md) |
| Keyed reconciliation | Validated core, sibling-key owner reuse/disposal; no serialized keys | [UI model](../architecture/ui-model.md) |
| Component mounting / local state | Validated core Debug/Release, keyed and invocation-tree owners, fresh setup and state-schema retention; no declared output signals, fallback or reload migration | [UI model](../architecture/ui-model.md) |
| Typed property bindings | Validated core, settled weak values and full-tree candidates; no style/event bindings, incremental stages or automatic publication | [UI model](../architecture/ui-model.md) |
| Declared cleanup / effects | Validated core Debug/Release, fresh-lifetime registration and explicit delivery; no declaration origins, setup-time cleanup factory, automatic publication or reload migration | [UI model](../architecture/ui-model.md) |
| Declared async / typed async observations | Validated core Debug/Release, primitive payloads and owner-bound detachment; no provider execution, resource payloads, automatic supersession/cancel delivery, recording or reload migration | [UI model](../architecture/ui-model.md) |
| Component descriptions / composition | Validated core, props, slots, actions, styles and bounded nested lowering; lowering neither mounts nor serializes components | [UI model](../architecture/ui-model.md) |
| Component source mapping | Validated core Debug/Release, template/invocation/argument origins; no default-declaration or individual style-field spans or source authentication | [UI model](../architecture/ui-model.md) |
| Source-aware diagnostics | Validated core Debug/Release, owned locations across validation/lowering/staging; no encoded diagnostics or source authentication | [inspection](../architecture/inspection.md) |
| Source consumption | Validated Debug/Release static core and optional Vulkan linkage through add_subdirectory/local FetchContent; automatic checks now cover add_subdirectory only | [procedure](../guides/development.md#source-consumption-workflow) |
| Core PR CI | Contracts with examples OFF; API/build/unknown changes add minimal core import. Full Debug/Release examples/imports remain in integration; no hosted speedup claim | [procedure](../guides/development.md#ci-scopes-and-reproducible-measurements) |
| Commands / execution | Validated in-process synchronous execution, eligibility and once-only sequences; no async executor, command recording format or palette | [commands](../architecture/commands.md) |
| Keyboard / gamepad shortcuts | Validated core normalized chords and scopes; no native shortcut integration, multi-key sequences or recording schema | [commands](../architecture/commands.md) |
| Edit transactions / gestures | Validated core synchronous host-owned history and gestures; no native capture adapter, automatic owner subscription, cross-command joining, async edits or serialized tokens | [commands](../architecture/commands.md) |
| Property reflection | Validated Box/Text metadata and one authored-string editor; no generalized range/enum editor reflection | [UI model](../architecture/ui-model.md), [editor](../integrations/path-finder.md) |
| In-process editor panels | Validated core Debug/Release scripted Inspector/reload/history and CPU/replay; no generalized topology migration, native editor or real-font capture | [Path-finder](../integrations/path-finder.md) |
| Layout | Validated single-line Flex, clips, scrolling and assigned-width text wrapping; Stack/Grid/SplitPane scope above. No flex-item wrapping or absolute positioning | [layout](../architecture/layout.md) |
| Stylesheets / pseudo states | Validated host-built C++ resolver; no authored syntax or automatic invalidation | [styling](../architecture/styling.md) |
| Theme variables | Validated core Debug/Release, typed host-built themes and full-tree presentation; no subtree providers, authored syntax or automatic invalidation | [styling](../architecture/styling.md) |
| Paint / Vulkan | Validated offscreen primitives, images, batching, real Latin/Japanese glyph atlas and bounded CPU comparisons; GPU execution requires the stated device configuration | [rendering](../architecture/rendering.md) |
| Look extensions | Planned only: gradients, shadow/glow, additional analytic/SDF shapes, procedural Slang fills, custom SDF/motion and layer effects. Existing rounded rectangles/borders and build-time Slang shaders do not establish these extension capabilities | [proposal](../proposals/look.md) |
| Scalar CPU renderer | Validated RGBA8 sRGB, transforms/clips/images and grayscale atlas capture; no direct glyph rasterizer, placeholder substitution, workers or performance claim | [rendering](../architecture/rendering.md#implemented-scalar-cpu-reference-backend) |
| Win32 Vulkan menu | Validated one FIFO-swapchain configuration, placeholder/explicit real fonts and physical controller sessions; not a broad Windows claim | [procedure](../guides/development.md#win32-vulkan-menu-host) |
| Pointer / focus / keyboard / gamepad | Validated synthetic dispatch/recovery/scroll, Win32 normalization and the limited physical sessions above; newer modal/menu scope is stated separately | [input](../architecture/input.md) |
| Compatible pointer snapshots | Validated core Debug/Release: opt-in stable author-ID/kind/binding continuity, stationary hover, cancellation, Replay v1/v2 and window-free Inspector consumer; independent Debug core import. Default tree-identity policy unchanged; hydra-toon/native integration not recertified | [input](../architecture/input.md#implemented-pointer-dispatch), [Inspector](../guides/grid.md) |
| Text shaping / fallback | Placeholder implemented; optional HarfBuzz/Noto shaping, bounded LTR wrapping and host font aliases validated; broader Unicode line breaking unvalidated | [text](../architecture/text.md) |
| Glyph raster / cache / atlas | Validated FreeType grayscale references, bounded cache and frame-local atlas | [text](../architecture/text.md) |
| Font profiles | Validated sealed profiles, JSON v1/digest identification and real-font runner frames; no capture-bundle identification or dynamic font loading | [text](../architecture/text.md) |
| Image binding | Validated Vulkan/CPU sampling, crop/tint and straight/premultiplied alpha; CPU owns bounded sRGB/UNORM copies, no application asset API | [rendering](../architecture/rendering.md) |
| Semantics / actions | Validated in-process generation-aware projection and Semantic JSON v1; newer collection state/dialog/menu roles included; no platform adapter | [semantics](../architecture/semantics.md) |
| Replay | Validated in-process v1 playback/comparison/sessions and explicit v2 boolean host state; no recorder, provider scheduler or animation evaluation | [replay](../architecture/replay.md) |
| Inspection / source mapping | Validated in-process ID/role-name/path/point targets and windowless sessions; no component-key selector or transport | [inspection](../architecture/inspection.md) |
| UI invariants | Validated core Debug/Release read-only generation assertions; no scenario encoding, runner/bundle integration, group selectors or performance predicates | [inspection](../architecture/inspection.md) |
| Offscreen runner / capture | Validated explicit Replay v1/v2 adapters, CPU and placeholder/real-font Vulkan capture; no backend substitution, PNG capture or CLI | [inspection](../architecture/inspection.md) |
| DPI / coordinates | Validated continuous geometry, fractional Vulkan scales and Win32 DPI mapping; no pixel snapping | [rendering](../architecture/rendering.md) |
| Editing / IME / clipboard | Validated core Debug/Release bounded single-line UTF-8 scalar editing and revision checks; no grapheme/BiDi caret, composition, clipboard or serialized edit events | [input](../architecture/input.md) |

No CLI, RPC/MCP transport, WebGPU/browser host or snapshot bundle is implemented. Future work is in [backlog](../roadmap/backlog.md). Vulkan submission counts are not timing evidence.
