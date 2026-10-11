#include <tessera/commands/commands.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/commands/transactions.hpp>
#include <tessera/commands/gestures.hpp>
#include <tessera/commands/shortcuts.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/reactive/runtime.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/ui/keyed.hpp>
#include <tessera/ui/component.hpp>
#include <iostream>
#include <map>

using namespace tessera;
using namespace std::chrono_literals;

struct PanelObservation {
    CommandState command;
    CommandObjectIds objects;
    std::optional<std::string> selection;
    bool operator==(const PanelObservation&) const = default;
};

struct PanelHost final : TransactionHost {
    std::optional<std::string> inspected_item;
    double amount = 1, previous_amount = 0;
    int history_entries = 0, rollbacks = 0;
    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        inspected_item = std::get<CommandObjectId>(invocation.arguments.at("item")).value;
        return {CommandOutcomeStatus::success, {{"item", CommandObjectId{*inspected_item}}}, {}};
    }
    CommandOutcome begin(const TransactionRecord&) override { previous_amount = amount; return {}; }
    CommandOutcome execute_edit(const CommandInvocationRecord& invocation, const TransactionRecord&) override {
        amount = std::get<double>(invocation.arguments.at("amount")); return {};
    }
    CommandOutcome commit(const TransactionRecord&) override { ++history_entries; return {}; }
    CommandOutcome rollback(const TransactionRecord&, TransactionCancel) override {
        amount = previous_amount; ++rollbacks; return {};
    }
};

int main() {
    CommandDescriptor inspect;
    inspect.id = "inventory.inspect";
    inspect.label = "Inspect selected item";
    inspect.category = "Inventory";
    CommandParameter item;
    item.name = "item"; item.type = CommandParameterType::object_id;
    inspect.parameters = {item};
    inspect.results = {item};
    CommandDescriptor edit; edit.id = "inventory.set_amount"; edit.label = "Set amount";
    CommandParameter amount_parameter; amount_parameter.name = "amount"; amount_parameter.type = CommandParameterType::number;
    amount_parameter.minimum = 0; amount_parameter.maximum = 100;
    edit.parameters = {item, amount_parameter};
    const auto registry = CommandRegistry::create({inspect, edit}, {{"inspect-item", inspect.id}});
    if (!registry) return 1;

    ComponentDescription panel_component;
    panel_component.kind = "tool-panel";
    panel_component.document.root.id = "tool-panel";
    UiNode button;
    button.id = "inspect"; button.properties["focusable"] = true;
    UiNode label;
    label.kind = NodeKind::text; label.properties["text"] = inspect.label;
    button.children.push_back(label); panel_component.document.root.children.push_back(button);
    panel_component.actions = {{"inspect", {}}};
    panel_component.events = {{"inspect", "inspect", "activate"}};
    const ValidationContext context{{"inspect-item"}};
    PlaceholderTextShaper text;
    std::uint64_t sequence = 0;
    PanelHost command_host; // Application state/history stay in the host.
    CommandTransactions executor; // Non-editing and editing commands share sequences.

    std::optional<std::string> host_selection;
    ReactiveRuntime runtime;
    auto selected_item = runtime.signal(runtime.root(), "selection", host_selection);
    auto panel_owner = runtime.owner(runtime.root(), "panel");
    auto inspect_action = runtime.signal(panel_owner, "inspect-action", std::string("inspect-item"));
    ComponentArguments panel_arguments;
    panel_arguments.actions["inspect"] = inspect_action;
    bool selection_observer_connected = true; // Synthetic host subscription lifetime.
    runtime.on_cleanup(panel_owner, "selection-observer", [&] { selection_observer_connected = false; });
    int evaluations = 0;
    const auto panel = runtime.computed<PanelObservation>(panel_owner, "panel", [&] {
        ++evaluations;
        const auto selection = selected_item.read();
        return PanelObservation{{selection.has_value(), false, selection ? "" : "Select an item."},
            selection ? CommandObjectIds{*selection} : CommandObjectIds{}, selection};
    });
    // Item details fail locally: a missing host record faults only this boundary,
    // and the host presents a fallback while the rest of the panel publishes.
    const std::map<std::string, std::string> host_records{{"item-3", "Rope"}};
    auto details_owner = runtime.boundary(panel_owner, "details");
    const auto details = runtime.computed<std::string>(details_owner, "details", [&] {
        const auto selection = selected_item.read();
        if (!selection) return std::string("No item");
        const auto record = host_records.find(*selection);
        if (record == host_records.end()) throw std::runtime_error("No host record for " + *selection);
        return record->second;
    });
    // Synthetic host status line; it only observes published generations.
    std::string status_line;
    int status_detached = 0;
    runtime.effect<std::optional<std::string>>(panel_owner, "status", EffectPhase::notify,
        [&] { return selected_item.read(); }, [&](const std::optional<std::string>& selection) {
            status_line = selection ? "Selected " + *selection : "No selection";
            return [&status_detached] { ++status_detached; };
        });

    std::uint64_t generation = 0;
    for (const bool selected : {false, true, false}) {
        runtime.batch([&] {
            host_selection = selected ? std::optional<std::string>{"item-7"} : std::nullopt;
            // Publish an owned host-state observation at the update point.
            selected_item.write(host_selection);
        });
        const auto faults = runtime.flush();
        if (faults.size() != (selected ? 1u : 0u) || runtime.faulted(details_owner) != selected) return 13;
        const auto details_text = runtime.faulted(details_owner) ? std::string("Details unavailable") : details.read();
        const auto observation = panel.read();
        ++generation; // Host-owned UI generation, distinct from reactive revisions.
        const auto commands = CommandSnapshot::publish(*registry.value, generation, CommandMode::production,
            {{inspect.id, observation.command}}, observation.objects);
        if (!commands) return 2;
        const auto composed = lower_component(runtime, panel_component, panel_arguments, context);
        if (!composed) return 27;
        const auto document = commands.value->apply_eligibility(composed.value->document, context);
        if (!document) return 3;
        auto tree = UiTree::create(*document.value, context);
        if (!tree) return 4;
        std::vector<ResolvedStyle> styles((*tree.value)->size());
        styles[1].height = Dimension::points(32);
        const auto layout = compute_layout({tree.value->get(), styles, {240, 80}, &text});
        if (!layout) return 5;
        const SemanticInput ui{tree.value->get(), styles, &*layout.value};
        const auto semantics = build_semantic_tree(ui);
        if (!semantics || semantics.value->nodes[1].enabled != selected) return 6;
        // The full candidate is valid: publish it, then deliver post-publication effects.
        if (runtime.publish() != 1 || !runtime.deliver_effects(EffectPhase::notify).empty()) return 12;

        FocusDispatcher focus;
        const HitTestInput hit{tree.value->get(), styles, &*layout.value};
        if (!focus.dispatch(hit, {1us, FocusNext{}})) return 7;
        const auto activated = focus.dispatch(hit, {2us, Activate{}});
        if (!activated || activated.value->actions.size() != (selected ? 1u : 0u)) return 8;
        for (const auto& action : activated.value->actions) {
            const auto invocation = commands.value->resolve_action(ui, action, generation,
                {{"item", CommandObjectId{*observation.selection}}}, CommandSource::keyboard, ++sequence);
            if (!invocation) return 9;
            // Dispatch has returned. The adapter rechecks, consumes the request once,
            // and validates the declared result from this non-editing host command.
            const auto executed = executor.execute(*commands.value, *invocation.value, command_host, &ui);
            if (!executed || executed.value->outcome.status != CommandOutcomeStatus::success ||
                executed.value->outcome.values.at("item") != invocation.value->record().arguments.at("item")) return 9;
        }
        const std::vector<ShortcutScope> shortcut_scopes{{(*tree.value)->root(), {
            {{Key::i, {false, true}}, inspect.id, ActionRequest{"activate", "inspect-item", *(*tree.value)->find("inspect")}}
        }, {
            {{GamepadButton::west, gamepad_button_bit(GamepadButton::left_shoulder)}, inspect.id,
                ActionRequest{"activate", "inspect-item", *(*tree.value)->find("inspect")}}
        }}};
        const auto shortcuts = CommandShortcuts::create(*commands.value, **tree.value, shortcut_scopes);
        if (!shortcuts) return 25;
        const SemanticInput shortcut_ui{tree.value->get(), styles, &*layout.value, activated.value->focused};
        const auto shortcut = shortcuts.value->dispatch(*commands.value, shortcut_ui,
            {{3us, KeyDown{Key::i, {false, true}}}, generation, ++sequence,
                observation.selection ? CommandArguments{{"item", CommandObjectId{*observation.selection}}} : CommandArguments{}});
        if (!shortcut || shortcut.value->invocation.has_value() != selected) return 25;
        if (shortcut.value->invocation) {
            const auto executed = executor.execute(*commands.value, *shortcut.value->invocation, command_host, &shortcut_ui);
            if (!executed || executed.value->outcome.status != CommandOutcomeStatus::success) return 25;
        }
        const auto gamepad_shortcut = shortcuts.value->dispatch(*commands.value, shortcut_ui,
            GamepadShortcutRequest{{GamepadButton::west, gamepad_button_bit(GamepadButton::left_shoulder)},
                generation, ++sequence,
                observation.selection ? CommandArguments{{"item", CommandObjectId{*observation.selection}}} : CommandArguments{}});
        if (!gamepad_shortcut || gamepad_shortcut.value->invocation.has_value() != selected) return 26;
        if (gamepad_shortcut.value->invocation) {
            const auto& record = gamepad_shortcut.value->invocation->record();
            if (record.source != CommandSource::gamepad || record.command != shortcut.value->invocation->record().command ||
                record.arguments != shortcut.value->invocation->record().arguments ||
                record.owner != shortcut.value->invocation->record().owner) return 26;
            const auto executed = executor.execute(*commands.value, *gamepad_shortcut.value->invocation, command_host, &shortcut_ui);
            if (!executed || executed.value->outcome.status != CommandOutcomeStatus::success) return 26;
        }
        const auto info = commands.value->lookup(inspect.id);
        std::cout << "Generation " << generation << ": " << info.value->descriptor.label << " enabled=" << selected;
        if (!selected) std::cout << " (" << info.value->state.disabled_reason << ')';
        std::cout << " shortcut=" << shortcut.value->consumed;
        std::cout << " gamepad-shortcut=" << gamepad_shortcut.value->consumed;
        std::cout << " status=\"" << status_line << "\" details=\"" << details_text << '"';
        for (const auto& fault : faults) std::cout << " fault=" << fault.code << '@' << fault.path;
        std::cout << '\n';
    }
    // Inventory rows: keys keep each row's owner, local state, bindings, and focus
    // across host reorders; removal closes the row and reinsertion starts fresh.
    struct Row { Signal<std::string> name; Signal<bool> pinned; Computed<std::string> label; };
    KeyedCollection rows(runtime.owner(panel_owner, "rows"));
    std::map<std::uint64_t, Row> row_state; // By instance lifetime.
    int label_evaluations = 0;
    std::vector<std::string> unsubscribed;
    FocusDispatcher row_focus;
    std::uint64_t lantern = 0;
    using Items = std::vector<std::pair<std::string, std::string>>;
    const std::vector<Items> inventory{{{"item-3", "Rope"}, {"item-7", "Lantern"}, {"item-9", "Map"}},
        {{"item-9", "Map"}, {"item-7", "Lantern"}, {"item-3", "Rope"}}, {{"item-9", "Map"}, {"item-3", "Rope"}},
        {{"item-7", "Lantern"}, {"item-9", "Map"}, {"item-3", "Coiled rope"}}};
    for (const auto& items : inventory) {
        std::vector<KeyedChild> children;
        for (const auto& [id, name] : items) children.push_back({id, "item-row"});
        const auto update = rows.reconcile(runtime, children);
        if (!update) return 14;
        for (const auto i : update.value->created) {
            const auto& row = update.value->instances[i];
            const auto name = runtime.signal(row.owner, "name", items[i].second);
            const auto pinned = runtime.signal(row.owner, "pinned", false); // Local view state.
            const auto label = runtime.computed<std::string>(row.owner, "label", [&label_evaluations, name, pinned] {
                ++label_evaluations;
                return name.read() + (pinned.read() ? " (pinned)" : "");
            });
            row_state[row.lifetime] = {name, pinned, label};
            runtime.on_cleanup(row.owner, "thumbnail", [&unsubscribed, key = row.key] { unsubscribed.push_back(key); });
        }
        runtime.batch([&] { // Host item data reaches each row as a prop.
            for (std::size_t i = 0; i < items.size(); ++i) row_state[rows.instances()[i].lifetime].name.write(items[i].second);
        });
        if (!runtime.flush().empty()) return 15;
        ComponentDescription list_component;
        list_component.kind = "inventory";
        list_component.document.root.id = "inventory";
        list_component.slots = {{"rows", "inventory", NodeKinds::text}};
        ComponentArguments list_arguments;
        for (const auto& row : rows.instances()) {
            ComponentDescription item_component;
            item_component.kind = row.kind;
            auto& node = item_component.document.root;
            node.kind = NodeKind::text; node.id = row.key;
            node.properties["text"] = std::string{}; // Valid template placeholder.
            item_component.props = {{"caption", PropertyType::string, {}},
                {"focusable", PropertyType::boolean, Property{true}}};
            item_component.properties = {{"caption", row.key, "text"}, {"focusable", row.key, "focusable"}};
            ComponentArguments item_arguments;
            item_arguments.props["caption"] = row_state[row.lifetime].label;
            const auto item = lower_component(runtime, item_component, item_arguments);
            if (!item) return 21;
            list_arguments.slots["rows"].push_back(item.value->document.root);
        }
        const auto bound = lower_component(runtime, list_component, list_arguments);
        if (!bound) return 22;
        const auto& list = bound.value->document;
        auto tree = UiTree::create(list);
        if (!tree) return 16;
        const std::vector<ResolvedStyle> styles((*tree.value)->size());
        const auto layout = compute_layout({tree.value->get(), styles, {240, 120}, &text});
        if (!layout) return 17;
        const HitTestInput hit{tree.value->get(), styles, &*layout.value};
        const auto focused = lantern == 0 ? row_focus.focus(hit, *(*tree.value)->find("item-7")) : row_focus.refresh(hit);
        if (!focused || !focused.value->focused || runtime.publish() != 0) return 18;
        if (!runtime.deliver_effects(EffectPhase::notify).empty() || !runtime.deliver_cleanups().empty()) return 19;
        if (lantern == 0) {
            lantern = rows.find("item-7")->lifetime;
            row_state[lantern].pinned.write(true); // A user toggle on this row, flushed next generation.
        }
        std::cout << "Rows:";
        for (const auto& node : list.root.children) std::cout << ' ' << *node.id << "=\"" << std::get<std::string>(node.properties.at("text")) << '"';
        std::cout << " focus=" << *(*tree.value)->get(*focused.value->focused)->id << '\n';
    }
    // Reorder kept the pinned Lantern row and its focus; removal unsubscribed it,
    // and reinsertion created a new lifetime, so a late result for the old row is stale.
    const auto* lantern_row = rows.find("item-7");
    if (!lantern_row || rows.current("item-7", lantern) || row_state[lantern_row->lifetime].pinned.read() ||
        unsubscribed != std::vector<std::string>{"item-7"} || label_evaluations != 6) return 20;

    // One application gesture spans three reactive batches. Each edit lowers a
    // complete document and evaluates the ordinary presentation pipeline; only
    // explicit completion adds an application history entry.
    const auto amount_view = runtime.signal(panel_owner, "amount", command_host.amount);
    const auto amount_label = runtime.computed<std::string>(panel_owner, "amount-label", [&] {
        return "Amount: " + std::to_string(static_cast<int>(amount_view.read()));
    });
    const auto edit_snapshot = [&] {
        return *CommandSnapshot::publish(*registry.value, ++generation, CommandMode::production,
            {{edit.id, {true}}}, {"item-7"}).value;
    };
    const auto edit_request = [&](const CommandSnapshot& commands, double value) {
        return *commands.resolve({commands.generation(), edit.id,
            {{"item", CommandObjectId{"item-7"}}, {"amount", value}}, CommandSource::pointer, ++sequence}).value;
    };
    const auto present_amount = [&] {
        runtime.batch([&] { amount_view.write(command_host.amount); });
        if (!runtime.flush().empty()) return false;
        ComponentDescription caption; caption.kind = "amount"; caption.document.root.kind = NodeKind::text;
        caption.document.root.id = "amount"; caption.document.root.properties["text"] = std::string{};
        caption.props = {{"caption", PropertyType::string, {}}}; caption.properties = {{"caption", "amount", "text"}};
        ComponentArguments args; args.props["caption"] = amount_label;
        const auto lowered = lower_component(runtime, caption, args);
        if (!lowered) return false;
        const auto tree = UiTree::create(lowered.value->document);
        if (!tree) return false;
        const std::vector<ResolvedStyle> styles((*tree.value)->size());
        const auto layout = compute_layout({tree.value->get(), styles, {240, 80}, &text});
        if (!layout) return false;
        const SemanticInput ui{tree.value->get(), styles, &*layout.value};
        if (!build_semantic_tree(ui) || !build_paint_list({tree.value->get(), styles, &*layout.value, &text})) return false;
        if (runtime.publish() != 0 || !runtime.deliver_effects(EffectPhase::notify).empty()) return false;
        std::cout << "Edit generation " << generation << ": " << amount_label.read() << '\n';
        return true;
    };
    auto commands = edit_snapshot(); auto command = edit_request(commands, 2);
    CommandGestures gestures(executor);
    EditGesture gesture{1, PointerId{7}, PointerButton::primary, {"Change item amount", "amount", {"item-7"}, {}}};
    for (const double value : {2.0, 3.0, 4.0}) {
        if (value != 2) { commands = edit_snapshot(); command = edit_request(commands, value); }
        const auto result = gestures.edit(gesture, commands, command, command_host);
        if (!result || result.value->outcome.status != CommandOutcomeStatus::success || !present_amount()) return 24;
    }
    const auto committed = gestures.handle_input(commands, {std::chrono::microseconds{1}, PointerUp{{7}, {10, 10}}}, command_host);
    if (!committed || !committed.value->consumed || !committed.value->closure ||
        committed.value->closure->status != CommandOutcomeStatus::success || command_host.history_entries != 1) return 25;
    commands = edit_snapshot(); command = edit_request(commands, 99);
    gesture.id = 2;
    const auto changed = gestures.edit(gesture, commands, command, command_host);
    if (!changed || changed.value->outcome.status != CommandOutcomeStatus::success || !present_amount()) return 26;
    const auto cancelled = gestures.handle_input(commands, {std::chrono::microseconds{2}, KeyDown{Key::escape}}, command_host);
    ++generation; // Rollback settles before the restoration's later presentation.
    if (!cancelled || !cancelled.value->consumed || !cancelled.value->closure ||
        cancelled.value->closure->status != CommandOutcomeStatus::success || command_host.amount != 4 ||
        command_host.history_entries != 1 || command_host.rollbacks != 1 || !present_amount()) return 27;
    std::cout << "One committed edit gesture; cancelled edit restored amount without another history entry.\n";

    if (command_host.inspected_item != "item-7" || evaluations != 3 || status_detached != 2) return 10;
    panel_owner.dispose();
    runtime.flush();
    if (!selection_observer_connected || !runtime.deliver_cleanups().empty() || selection_observer_connected ||
        status_detached != 3 || unsubscribed.size() != 4) return 11;
    std::cout << "Host inspected " << *command_host.inspected_item << ". Synthetic core-only panel; placeholder text, no native window.\n";
}
