# Your first app

Start with the build and copy commands in [Getting Started](getting-started.md). Read the [complete first-app source](../../examples/first-app/main.cpp) in two parts: `Field`/`FirstApp` compose the UI; `Presentation`, `prepare`, `Host` and the input loop supply the explicit application host. The public [form contract](../architecture/ui-model.md#implemented-c-form-authoring) owns exact API and lifetime rules.

## Compose a form

`FirstApp` returns an immutable `authoring::Element` made from `Text`, `Checkbox`, `Row` and `Column`. These are ordinary Box/Text compositions lowered into one document. `Field` is a reusable C++ function with typed `FieldProps`, a default gap and ordered child elements. Reusing this function requires no component mounting records, owner registrations or reconciliation tables.

`ui::Value<T>` accepts an exact-type literal, state handle or computed handle. In the source, the Checkbox receives the enabled observation and the summary Text receives a computed string. `Props::id` supplies document-scoped identity; `Props::style` supplies ordinary typed style declarations. Row and Column select their flex direction; gap, padding, background and other declarations resolve through the shared [style cascade](../architecture/styling.md#implemented-prototype-style-resolution). Repeated explicit IDs in one lowered form are invalid.

The Checkbox's `app.toggle` string declares an activate action. It does not register a callback or change its controlled value. The [Checkbox contract](../architecture/ui-model.md#implemented-c-form-authoring) projects it as an ordinary button whose content name includes `[ ]` or `[x]` and its label.

## Keep one session alive

The source creates a session and its named state before composing the form:

```cpp
ui::Session session;
Host host;
const auto enabled = session.state("enabled", host.enabled);
const auto summary = session.computed("summary", [enabled] {
    return enabled.read() ? std::string("Feature enabled") : std::string("Feature disabled");
});
const auto form = FirstApp(enabled, summary);
```

`Host::enabled` is the application's accepted boolean; the session source is its UI observation. The pure calculation reads that observation and derives text. Named sources/calculations belong to one session lifetime, and the returned handles are weak: keeping an Element or handle alive does not keep the session alive. Keep the session on its creating UI thread for the form's entire use. Dynamic keyed component lifetimes use the separate [mounting contract](../architecture/ui-model.md#implemented-component-invocation-mounting).

`session.update(...)` batches writes without flushing or publishing. `session.lower(form, actions)` validates the composition, settles the reactive graph and copies values into a candidate document plus styles/source map. It does not rerun the C++ composition function. Application code calls composition again when it needs a different child structure; literal C++ branches are not reactive conditionals.

## Execute commands after input dispatch

The host registers the action in `ValidationContext` and maps it to the `app.toggle` command descriptor. Each `CommandSnapshot` carries an explicit generation and command state. The example supplies `enabled = true` for that command; changing that observation to false makes `apply_eligibility` disable the control through the ordinary document. Use the authored document as eligibility input every time, so an old disabled result cannot become sticky.

`FocusDispatcher` consumes normalized logical events against the accepted tree, styles and layout. The scripted sequence supplies `FocusNext`, keyboard `Activate`, then gamepad `Activate`. A real host owns physical key/button normalization, monotonic event timestamps and repeat policy; [input](../architecture/input.md#host-boundary) owns that boundary. Merely changing `CommandSource` does not poll a device. Pointer and semantic routes in [Settings](../integrations/settings-app.md#implemented-form-skeleton-boundary) resolve to the same command model.

After dispatch returns, the host resolves each returned action against the current semantic input and command generation. `CommandExecutor` rechecks eligibility and live owner identity immediately before calling `Host::execute`, and consumes one increasing request sequence. `Host` proposes the opposite boolean in memory. The caller writes its observation, prepares the replacement and accepts `Host::enabled` only after that preparation succeeds. No callback runs during core traversal. See the [command execution contract](../architecture/commands.md#implemented-host-execution-and-result-boundary) for failures and once-only requests.

## Prepare and publish one presentation

`prepare` owns the host pipeline:

1. Lower the form and apply command eligibility.
2. Create a tree and resolve its styles.
3. Compute layout with the chosen viewport and text shaper.
4. Capture inspection, including semantic observations, and build paint.
5. Return the complete owned candidate to the caller.

Only then does the caller use `session.publish()` and replace the accepted presentation. Tree, styles, layout, inspection, paint and command snapshot stay together. Their borrowed input views are used synchronously; old tree handles cannot address the replacement. `focus.refresh` recovers the focused control through its stable `toggle` author ID.

Lowering and publication do not roll back application writes. This example restores its one boolean observation if preparation throws, retains the previous host value/presentation, and exits with the error. That bounded failure path is not an external side-effect transaction or arbitrary exception recovery system. The larger [Settings workflow](../integrations/settings-app.md#implemented-form-skeleton-boundary) demonstrates returned candidate rejection, retry and save staging. Its regression-only branches do not belong in a first form's composition.

## Inspect and capture

`summary_name` resolves the `summary` author ID in the owned inspection snapshot and reads its semantic name. No private application or runtime state is needed to observe the accepted text. The example verifies disabled/enabled/disabled observations and focus recovery across replacement.

The CPU capture adapter borrows a glyph cache; that cache borrows the host's `SyntheticGlyphs` rasterizer. Declaration order keeps all three alive in that order. Capture uses the accepted paint list, a fixed 480×180 logical viewport, scale one and animation time zero. It returns owned RGBA8 bytes, checks that toggling changes pixels, repeats the changed capture, and checks that restoring state restores pixels. Glyphs are synthetic bit marks over placeholder metrics. Real-font, GPU and native-window examples have separate [development workflows](development.md); the first app captures only in memory.

## Grow the application

Continue with [Settings](development.md#settings-form-fixture) for controlled TextInput, Checkbox/Apply, section choices, scrolling, class/theme styles and hover/focus/pressed feedback. Its [consumer design](../integrations/settings-app.md) separates application behavior from runtime contracts, and its replay reconstructs accepted views without re-executing the C++ command host. The [testing guide](testing.md) explains how to verify new operations. Proposed API syntax belongs in [proposals](../proposals/cpp-authoring.md); use source-backed contracts when extending this application.
