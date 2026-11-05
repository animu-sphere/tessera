# Select and edit an Inspector item

The [reference application](../../examples/inspector-hierarchy/main.cpp) combines ordinary public C++ widgets for the [Application Experience operation](../roadmap/current.md). It edits host-owned scene-like items without an engine, OpenUSD, source editor or second runtime.

## Build and run

Use the prerequisites and core configuration from [development](development.md):

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64 -DTESSERA_TEST_NATIVE_HOST=ON
cmake --build build --config Debug --target tessera_inspector_hierarchy
ctest --test-dir build -C Debug -R '^inspector_hierarchy$' --output-on-failure --no-tests=error
& .\build\Debug\tessera_inspector_hierarchy.exe --native
ctest --test-dir build -C Debug -R '^inspector_hierarchy_native$' --output-on-failure --no-tests=error
```

Use `Release` for the other configuration. Without arguments the executable runs the window-free consumer smoke. Windows `--native` opens the interactive CPU-rendered application; `--native-smoke` exercises the same window procedure with injected messages and exits. The opt-in native CTest requires a desktop session.

## Operate

- Select Cube or another row in the [TreeView](tree-list.md). Arrows move focus, Enter/Space selects, and Left/Right collapses/expands Scene. Disclosure never changes controlled selection.
- Tab to the [Grid Inspector](grid.md). Edit Display name with committed text, exact Ctrl+A, arrows, Shift selection, Home/End and Backspace/Delete. Text editing consumes supported keys before tree navigation.
- With Display name focused, exact non-repeating Ctrl+C copies the selected committed bytes, Ctrl+X copies and then deletes the selection, and Ctrl+V replaces the selection through the ordinary name-edit command. Windows interactive mode exchanges Unicode text with the OS clipboard. Empty selection copy/cut and empty text paste do nothing. Paste rejects CR/LF, embedded NUL, malformed Unicode and results above 4096 UTF-8 bytes, preserving the accepted name and selection; diagnostics print to stderr and the window remains usable.
- Activate Enabled or adjust Amount with Left/Down, Right/Up, Home/End or a pointer drag. Amount has integer stops from 0 to 100 in steps of 5.
- Select another item and return. Every item retains its own text owner, revision, UTF-8 byte selection, enabled flag and amount. The edited display name also appears in the Hierarchy.
- Wheel either pane or drag the [SplitPane](split-pane.md) boundary. Focus navigation reveals controls; scrolling and pane/window resizing do not change item values. Pane minima are 140/320 logical units plus the 8-unit handle; smaller windows clip overflowing minima.
- Open Item actions, or right-click the selected-item button at the top of the Inspector. Shift+F10 opens the same context actions from that button. Rename focuses the retained name editor; Toggle enabled shares the Checkbox command. Escape/Cancel or an outside press dismisses the menu. These operations need no destructive confirmation, so no Dialog is added.

The context target is the selected-item button, not arbitrary unselected hierarchy rows. No command silently selects a different item on right-click.

## Host and observation boundary

The host owns all item data, selection, disclosure, scroll offsets, input dispatchers and command sequences. Item actions resolve against the accepted semantic tree and command snapshot. A candidate copies host state, applies the public text transform where needed, and stages lowering, eligibility, styles, layout, paint and inspection. Only a successful command outcome accepts that candidate and publishes the session. A stale/foreign text owner or rejected text measurement leaves the accepted values and inspection unchanged; retry uses the retained edit revision.

The window-free smoke covers select/edit/select-another/return, independent item edits, foreign-owner rejection, presentation rejection/retry, collapse, actual wheel offsets, keyboard/pointer pane resizing, Menu/ContextMenu, and shared selection/property observations. It records accepted host-built view replacements and a selection action through ordinary [Replay](../architecture/replay.md), round-trips Replay JSON, compares playback and checks the final selected name, enabled flag and amount. This reproduces the observed views and action, not application command execution, raw text keys or native messages.

The application's synchronous `clipboard_read` and `clipboard_write` callbacks supply owned UTF-8 read results and located failure diagnostics outside core traversal. Copy preserves owner/revision/selection and publishes no UI update. Cut and paste capture the selected item and edit observation before calling the host, recheck focus, eligibility and identity afterward, then stage the existing `item.name` command. Cut deletes only after a write returns no diagnostics. Reentrant host changes cannot redirect the result to another item, selection or focused control. A failed clipboard service, invalid replacement or failed presentation retains the accepted state; presentation errors are bounded to the command result limit while retaining their original diagnostic codes. Clipboard writes and UI publication are separate: if cut copies successfully but its target changes or presentation fails, the clipboard keeps the copied bytes while no deletion is accepted. Retrying uses the retained selection and revision. No asynchronous clipboard protocol or clipboard-result replay encoding is introduced.

Windows hosting reuses the Settings starter's [optional host](../../examples/settings-starter/native_win32.hpp): pointer capture/cancellation, logical coordinate mapping, keyboard/text messages, GDI glyph coverage and CPU presentation. The application-local [clipboard adapter](../../examples/inspector-hierarchy/clipboard_win32.hpp) uses `CF_UNICODETEXT`, strict UTF-16/UTF-8 conversion, bounded reads and explicit Windows memory ownership. Open/format/lock/write failures return diagnostics without throwing through the message loop.

The window-free clipboard checks use injected services for reverse Unicode selections, partial replacement/deletion, modifier/repeat/focus guards, retained independent items, service/validation/presentation failures, retry and reentrant target changes. The injected native smoke checks arrow/Activate selection, text/Checkbox/Slider edits, select/return, right-click Rename, splitter capture and window resize, plus Ctrl+C/X/V routing, suppression of translated control characters/repeats, Unicode cut/paste, rejection recovery and strict Unicode conversion. It substitutes an in-memory clipboard, leaving the user's OS clipboard untouched. Live OS clipboard exchange, physical devices, IME composition, real text shaping, GPU rendering, native editable accessibility and persistence remain unvalidated. Capability claims and validation limits live in [support](../reference/support-matrix.md).
