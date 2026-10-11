#include <tessera/ui/component.hpp>
#include <tessera/ui/keyed.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/commands/commands.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include <tessera/style/style_sheet.hpp>
#include "../check.hpp"
#include <iostream>

using namespace tessera;
using namespace std::chrono_literals;
using tessera::test::check;
using tessera::test::has;

static_assert(std::is_constructible_v<ComponentActionValue, Signal<std::string>>);
static_assert(std::is_constructible_v<ComponentActionValue, Computed<std::string>>);
static_assert(!std::is_constructible_v<ComponentActionValue, Signal<bool>>);
static_assert(!std::is_constructible_v<ComponentActionValue, Property>);

namespace {
const ValidationContext context{{"inspect-item", "open-item", "close-panel"}};
ComponentDescription control() {
    ComponentDescription c;
    c.kind = "inventory-control";
    c.document.root.id = "control";
    c.document.root.properties["focusable"] = true;
    c.document.extensions["host:panel"].value = std::string("preserved");
    UiNode label; label.kind = NodeKind::text; label.id = "label";
    label.properties["text"] = std::string("Inventory");
    c.document.root.children = {label};
    c.actions = {{"submit", {}}, {"back", "close-panel"}};
    c.events = {{"submit", "control", "activate"}, {"back", "control", "cancel"}};
    return c;
}
ComponentArguments arguments() {
    ComponentArguments a; a.actions["submit"] = std::string("inspect-item"); return a;
}

void presentation_and_command_routes() {
    ReactiveRuntime r;
    auto action = r.signal(r.root(), "action", std::string("inspect-item"));
    int evaluations = 0;
    auto derived = r.computed<std::string>(r.root(), "action-name", [&] { ++evaluations; return action.read(); });
    auto a = arguments(); a.actions["submit"] = derived;
    check(has(lower_component(r, control(), a, context).diagnostics, "reactive_unsettled", "/reactive/root/values/action-name") &&
          evaluations == 0, "Event lowering cannot evaluate unsettled action arguments");
    CommandDescriptor inspect; inspect.id = "inventory.inspect"; inspect.label = "Inspect";
    auto open = inspect; open.id = "inventory.open"; open.label = "Open";
    const auto registry = CommandRegistry::create({inspect, open}, {{"inspect-item", inspect.id}, {"open-item", open.id}});
    check(bool(registry), "Commands register through their ordinary boundary");
    std::uint64_t generation = 0, sequence = 0;
    for (const auto& host_action : {std::string("inspect-item"), std::string("open-item")}) {
        action.write(host_action); r.flush();
        const auto c = control();
        auto lowered = lower_component(r, c, a, context);
        auto expected = c.document;
        expected.root.events = {{"activate", host_action}, {"cancel", "close-panel"}};
        check(lowered && lowered.value->document == expected && c.document == control().document &&
              lowered.value->stages == (PropertyStages::input | PropertyStages::semantics),
              "Action arguments/defaults lower ordinary bindings with input and semantic stages only");
        auto encoded = save_document(lowered.value->document, context);
        auto decoded = encoded ? load_document(*encoded.value, context) : Result<UiDocument>{};
        check(decoded && *decoded.value == expected, "Event lowering preserves metadata and JSON v1 semantics");
        auto same = c; same.document = expected;
        check(lower_component(r, same, a, context).value->stages == PropertyStages::none,
              "Writing identical events does not add affected stages");
        for (bool enabled : {true, false}) {
            auto commands = CommandSnapshot::publish(*registry.value, ++generation, CommandMode::production,
                {{inspect.id, {enabled}}, {open.id, {enabled}}});
            check(bool(commands), "Eligibility observations publish");
            auto document = commands.value->apply_eligibility(lowered.value->document, context);
            auto reference = commands.value->apply_eligibility(expected, context);
            check(document && reference && *document.value == *reference.value, "Component events share command eligibility lowering");
            auto tree = UiTree::create(*document.value, context), reference_tree = UiTree::create(*reference.value, context);
            check(tree && reference_tree, "Both presentation candidates validate");
            const StyleSheet sheet;
            auto styles = resolve_styles({tree.value->get(), &sheet});
            auto reference_styles = resolve_styles({reference_tree.value->get(), &sheet});
            check(styles && reference_styles && *styles.value == *reference_styles.value, "Styles agree with authored reference");
            PlaceholderTextShaper text;
            auto layout = compute_layout({tree.value->get(), *styles.value, {240, 80}, &text});
            auto reference_layout = compute_layout({reference_tree.value->get(), *reference_styles.value, {240, 80}, &text});
            check(layout && reference_layout && layout.value->boxes.size() == reference_layout.value->boxes.size(), "Geometry computes");
            for (std::size_t i = 0; i < layout.value->boxes.size(); ++i) {
                auto box = layout.value->boxes[i]; box.node = reference_layout.value->boxes[i].node;
                check(box == reference_layout.value->boxes[i], "Event composition preserves full-tree geometry");
            }
            const HitTestInput hit{tree.value->get(), *styles.value, &*layout.value};
            const SemanticInput ui{hit.tree, hit.styles, hit.layout};
            auto semantics = capture_semantics(ui, generation);
            auto reference_semantics = capture_semantics({reference_tree.value->get(), *reference_styles.value, &*reference_layout.value}, generation);
            auto paint = build_paint_list({hit.tree, hit.styles, hit.layout, &text});
            auto reference_paint = build_paint_list({reference_tree.value->get(), *reference_styles.value, &*reference_layout.value, &text});
            check(semantics && reference_semantics && *semantics.value == *reference_semantics.value &&
                  paint && reference_paint && *paint.value == *reference_paint.value, "Semantic and paint results agree");
            PointerDispatcher pointer;
            check(bool(pointer.dispatch(hit, {1us, PointerDown{{1}, {10, 10}}})), "Pointer press dispatches");
            auto released = pointer.dispatch(hit, {2us, PointerUp{{1}, {10, 10}}});
            check(released && released.value->actions.size() == (enabled ? 1u : 0u), "Pointer activation follows command eligibility");
            const auto target = *(*tree.value)->find("control");
            auto semantic = request_semantic_action(ui, target, "activate");
            check(bool(semantic) == enabled, "Semantic activation follows the same eligibility");
            if (enabled) {
                const auto accept = [&](const ActionRequest& request, CommandSource source) {
                    auto invocation = commands.value->resolve_action(ui, request, generation, {}, source, ++sequence);
                    check(invocation && invocation.value->record().command == (host_action == "inspect-item" ? inspect.id : open.id) &&
                          invocation.value->record().owner == released.value->actions[0], "Every source resolves the changed action through one command/owner");
                };
                accept(released.value->actions[0], CommandSource::pointer);
                accept(*semantic.value, CommandSource::accessibility);
                for (auto source : {CommandSource::keyboard, CommandSource::gamepad}) {
                    FocusDispatcher focus;
                    check(bool(focus.dispatch(hit, {1us, FocusNext{}})), "Logical focus enters the component");
                    auto activated = focus.dispatch(hit, {2us, Activate{}});
                    check(activated && activated.value->actions == released.value->actions, "Logical activation agrees with pointer");
                    accept(activated.value->actions[0], source);
                    auto cancelled = focus.dispatch(hit, {3us, Cancel{}});
                    check(cancelled && cancelled.value->actions.size() == 1 && cancelled.value->actions[0].action == "close-panel",
                          "Declared cancel defaults use ordinary focus dispatch");
                }
            } else {
                FocusDispatcher focus;
                check(bool(focus.dispatch(hit, {1us, FocusNext{}})), "Disabled focus navigation is valid");
                auto activated = focus.dispatch(hit, {2us, Activate{}});
                check(activated && activated.value->actions.empty(), "Disabled logical activation requests no action");
            }
        }
    }
    check(evaluations == 2, "Event lowering never recomputes settled action values");
}

void schema_arguments_and_bounds() {
    ReactiveRuntime r; auto c = control(); auto a = arguments();
    r.begin_batch(); // Schema and literal errors precede reactive entry.
    a.actions.clear(); a.actions["typo/~"] = std::string("inspect-item");
    auto invalid = lower_component(r, c, a, context);
    check(!invalid && has(invalid.diagnostics, "component_missing_action", "/actions/submit") &&
          has(invalid.diagnostics, "component_unknown_action", "/actions/typo~1~0"), "Action arguments have exact names and escaped paths");
    auto bad = c; bad.actions.push_back(bad.actions[0]); bad.events.push_back(bad.events[0]);
    bad.events[1] = {"missing", "absent", "click"};
    invalid = lower_component(r, bad, arguments(), context);
    check(has(invalid.diagnostics, "component_duplicate_action", "/component/actions/2/name") &&
          has(invalid.diagnostics, "component_duplicate_event", "/component/events/2/event") &&
          has(invalid.diagnostics, "component_unknown_action", "/component/events/1/action") &&
          has(invalid.diagnostics, "component_target", "/component/events/1/target") &&
          has(invalid.diagnostics, "unknown_event", "/component/events/1/event"), "Invalid declarations reject before runtime entry");
    bad = c; bad.actions[1].default_action = "unregistered";
    a = arguments(); a.actions["back"] = std::string("close-panel");
    check(has(lower_component(r, bad, a, context).diagnostics, "unknown_action", "/component/actions/1/default"),
          "Invalid defaults reject even when overridden");
    a = arguments(); a.actions["submit"] = std::string("unregistered");
    check(has(lower_component(r, c, a, context).diagnostics, "unknown_action", "/actions/submit"), "Literal actions require host registration before runtime entry");
    bad = c; bad.actions[0].name = std::string("\xff", 1);
    check(has(lower_component(r, bad, arguments(), context).diagnostics, "component_name", "/component/actions/0/name"), "Declared names must be bounded UTF-8");
    a.actions["submit"] = std::string("bad\nname");
    check(has(lower_component(r, c, a, context).diagnostics, "component_name", "/actions/submit"), "Host action arguments cannot contain controls");
    a.actions["submit"] = std::string(257, 'a');
    check(has(lower_component(r, c, a, context).diagnostics, "component_name", "/actions/submit"), "Host action names obey the component name bound");
    r.end_batch();
    check(has(lower_component(r, c, arguments()).diagnostics, "unknown_action", "/component/actions/1/default"), "An empty host context grants no actions");
    bad = c; bad.actions.resize(max_component_actions + 1);
    check(has(lower_component(r, bad, arguments(), context).diagnostics, "component_limit", "/component"), "Action declarations are bounded");
    a = arguments(); for (std::size_t i = 0; i < max_component_actions; ++i) a.actions["action" + std::to_string(i)] = std::string("inspect-item");
    check(has(lower_component(r, c, a, context).diagnostics, "component_limit", "/component"), "Action arguments are bounded before per-entry work");
    bad = c; bad.properties.resize(max_property_bindings); bad.events.resize(1);
    check(has(lower_component(r, bad, arguments(), context).diagnostics, "component_limit", "/component"), "Property and event mappings share a budget");
    bad = c; bad.events.resize(max_property_bindings + 1);
    check(has(lower_component(r, bad, arguments(), context).diagnostics, "component_limit", "/component"), "Event-only mapping counts are bounded");
    bad = c; bad.actions.clear();
    for (std::size_t i = 0; i < max_component_actions; ++i)
        bad.actions.push_back({i == 0 ? "submit" : i == 1 ? "back" : "action" + std::to_string(i), "inspect-item"});
    check(bool(lower_component(r, bad, {}, context)), "Exactly 256 defaulted actions are accepted");
    bad = c; bad.events.clear(); bad.document.root.children.clear();
    for (std::size_t i = 0; i < max_property_bindings / 2; ++i) {
        UiNode target; target.id = "target" + std::to_string(i);
        bad.document.root.children.push_back(target);
        bad.events.push_back({"submit", *target.id, "activate"});
        bad.events.push_back({"back", *target.id, "cancel"});
    }
    check(bool(lower_component(r, bad, arguments(), context)), "Exactly 4096 event mappings are accepted");
    c.props = {{"submit", PropertyType::string, Property{std::string("Caption")}}};
    c.properties = {{"submit", "label", "text"}};
    auto separate = lower_component(r, c, arguments(), context);
    check(separate && separate.value->document.root.children[0].properties.at("text") == Property{std::string("Caption")} &&
          separate.value->document.root.events.at("activate") == "inspect-item", "Prop and action declarations have separate namespaces");
    c = control(); c.events[0].target = "label";
    auto text_event = lower_component(r, c, arguments(), context);
    check(text_event && text_event.value->document.root.children[0].events.at("activate") == "inspect-item", "Text targets accept event mappings");
    c = control(); UiNode slot; slot.id = "slot"; c.document.root.children.push_back(slot);
    c.slots = {{"body", "slot"}}; a = arguments();
    UiNode content; content.id = "slotted"; content.events["activate"] = "open-item"; a.slots["body"] = {content};
    auto slotted = lower_component(r, c, a, context);
    check(slotted && slotted.value->document.root.children[1].children[0] == content, "Parent event mappings leave slot events opaque");
    c.events[0].target = "slotted";
    check(has(lower_component(r, c, a, context).diagnostics, "component_target", "/component/events/0/target"), "Event mappings cannot address slot content");
}

void weak_lifetimes_faults_and_publication() {
    ReactiveRuntime r; KeyedCollection rows(r.owner(r.root(), "rows"));
    const auto first = rows.reconcile(r, {{"item", "inventory-control"}});
    check(bool(first), "Component owner reconciles");
    const auto owner = first.value->instances[0].owner;
    auto action = r.signal(owner, "action", std::string("inspect-item"));
    int notifications = 0;
    r.effect<std::string>(owner, "action-observer", EffectPhase::notify, [action] { return action.read(); },
                         [&](const std::string&) { ++notifications; });
    r.flush(); auto a = arguments(); a.actions["submit"] = action;
    const auto accepted = lower_component(r, control(), a, context);
    check(accepted && r.deliver_effects(EffectPhase::notify).empty() && notifications == 0, "Lowering executes no host callbacks");
    check(r.publish() == 1 && r.deliver_effects(EffectPhase::notify).empty() && notifications == 1, "Host explicitly publishes accepted event candidates");
    action.write("unregistered"); r.flush();
    check(has(lower_component(r, control(), a, context).diagnostics, "unknown_action", "/actions/submit") &&
          accepted.value->document.root.events.at("activate") == "inspect-item" &&
          r.deliver_effects(EffectPhase::notify).empty() && notifications == 1, "Rejected reactive actions retain presentation and publish no effects");
    action.write("open-item"); r.flush();
    check(lower_component(r, control(), a, context) && r.publish() == 1 && r.deliver_effects(EffectPhase::notify).empty() && notifications == 2,
          "Corrected action values can publish");
    const auto reordered = rows.reconcile(r, {{"other", "inventory-control"}, {"item", "inventory-control"}});
    check(reordered && reordered.value->instances[1].lifetime == first.value->instances[0].lifetime && lower_component(r, control(), a, context),
          "Compatible reorder preserves action argument lifetime");
    rows.reconcile(r, {{"item", "replacement"}}); r.flush();
    check(has(lower_component(r, control(), a, context).diagnostics, "disposed_reactive_value", "/actions/submit"),
          "Kind replacement cannot revive an action reference through the same author ID");
    ReactiveRuntime foreign; a.actions["submit"] = foreign.signal(foreign.root(), "action", std::string("inspect-item"));
    check(has(lower_component(r, control(), a, context).diagnostics, "foreign_reactive_runtime", "/actions/submit"), "Actions cannot borrow another runtime");
    auto boundary = r.boundary(r.root(), "fault");
    a.actions["submit"] = r.computed<std::string>(boundary, "action", []() -> std::string { throw std::runtime_error("missing action"); });
    check(r.flush().size() == 1, "Action derivation faults are contained");
    const auto failed = lower_component(r, control(), a, context);
    check(!failed && (has(failed.diagnostics, "reactive_unsettled", "/actions/submit") ||
          has(failed.diagnostics, "reactive_faulted_snapshot", "/actions/submit")), "Faulted action snapshots reject without retry");
    auto c = control(); c.actions.push_back({"unused", {}}); a = arguments();
    a.actions["unused"] = action;
    check(has(lower_component(r, c, a, context).diagnostics, "disposed_reactive_value", "/actions/unused"), "Unmapped declared actions are also checked");
    r.begin_batch();
    check(has(lower_component(r, control(), arguments(), context).diagnostics, "reactive_open_batch", "/reactive"), "Literal action candidates require a settled boundary");
    r.end_batch();
}
} // namespace

int main() {
    try {
        presentation_and_command_routes(); schema_arguments_and_bounds(); weak_lifetimes_faults_and_publication();
        std::cout << "Component event checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
