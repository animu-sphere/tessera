#include <tessera/commands/commands.hpp>
#include <tessera/commands/transactions.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/reactive/runtime.hpp>
#include <tessera/ui/keyed.hpp>
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

int main() {
    CommandDescriptor inspect;
    inspect.id = "inventory.inspect";
    inspect.label = "Inspect selected item";
    inspect.category = "Inventory";
    CommandParameter item;
    item.name = "item"; item.type = CommandParameterType::object_id;
    inspect.parameters = {item};
    const auto registry = CommandRegistry::create({inspect}, {{"inspect-item", inspect.id}});
    if (!registry) return 1;

    UiDocument authored;
    authored.root.id = "tool-panel";
    UiNode button;
    button.id = "inspect"; button.properties["focusable"] = true; button.events["activate"] = "inspect-item";
    UiNode label;
    label.kind = NodeKind::text; label.properties["text"] = inspect.label;
    button.children.push_back(label); authored.root.children.push_back(button);
    const ValidationContext context{{"inspect-item"}};
    PlaceholderTextShaper text;
    std::uint64_t sequence = 0;
    std::optional<std::string> inspected_item; // Application state stays in the host.
    TransactionSession transactions; // Non-editing commands open no token.

    std::optional<std::string> host_selection;
    ReactiveRuntime runtime;
    auto selected_item = runtime.signal(runtime.root(), "selection", host_selection);
    auto panel_owner = runtime.owner(runtime.root(), "panel");
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
        const auto document = commands.value->apply_eligibility(authored, context);
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
            if (!invocation || !commands.value->recheck(*invocation.value, &ui).empty()) return 9;
            // Dispatch and validation have returned. Execute the non-editing host command here.
            inspected_item = std::get<CommandObjectId>(invocation.value->record().arguments.at("item")).value;
        }
        const auto info = commands.value->lookup(inspect.id);
        std::cout << "Generation " << generation << ": " << info.value->descriptor.label << " enabled=" << selected;
        if (!selected) std::cout << " (" << info.value->state.disabled_reason << ')';
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
        UiDocument list;
        list.root.id = "inventory";
        for (const auto& row : rows.instances()) {
            UiNode node;
            node.kind = NodeKind::text; node.id = row.key; node.properties["focusable"] = true;
            node.properties["text"] = row_state[row.lifetime].label.read();
            list.root.children.push_back(node);
        }
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

    // Application edits: the host owns quantities and history. A continuous scrub
    // spans several update batches with one transaction; a cancelled scrub rolls
    // back once. The earlier inspection opened no token.
    if (transactions.open()) return 21;
    std::map<std::string, int> quantity{{"item-3", 1}};
    std::vector<std::string> history;
    int before = 0;
    const auto apply = [&](const TransactionRecord& record) { // Host application adapter.
        if (record.phase == TransactionPhase::begin) before = quantity["item-3"];
        else if (record.phase == TransactionPhase::commit) history.push_back(record.metadata.label);
        else quantity["item-3"] = before;
    };
    auto stock = runtime.signal(panel_owner, "stock", quantity["item-3"]);
    TransactionMetadata scrub;
    scrub.label = "Adjust quantity"; scrub.source = CommandSource::pointer; scrub.objects = {"item-3"};
    for (const int steps : {3, 2}) {
        const auto begun = transactions.begin(scrub, ++sequence);
        if (!begun || begun.value->token.id != (steps == 3 ? 1u : 2u)) return 22;
        apply(*begun.value);
        for (int step = 0; step < steps; ++step) { // One update batch per pointer move.
            if (!transactions.edit(begun.value->token, ++sequence).empty()) return 23;
            runtime.batch([&] { stock.write(++quantity["item-3"]); });
            if (!runtime.flush().empty() || runtime.publish() != 0) return 24;
        }
        if (steps == 3) {
            const auto committed = transactions.commit(begun.value->token, ++sequence);
            if (!committed || committed.value->edits != 3) return 25;
            apply(*committed.value);
        } else { // Escape, then capture loss: one rollback.
            const auto escape = transactions.rollback(begun.value->token, ++sequence, RollbackReason::cancelled);
            const auto lost = transactions.rollback(begun.value->token, ++sequence, RollbackReason::capture_lost);
            if (!escape || !*escape.value || !lost || *lost.value) return 26;
            apply(**escape.value);
        }
        runtime.batch([&] { stock.write(quantity["item-3"]); }); // Settles before the next generation.
        if (!runtime.flush().empty() || runtime.publish() != 0) return 27;
    }
    if (quantity["item-3"] != 4 || stock.read() != 4 || history != std::vector<std::string>{"Adjust quantity"}) return 28;
    std::cout << "Transactions: committed \"" << history.front() << "\" (3 edits), rolled back 1; quantity=" << quantity["item-3"] << '\n';

    if (inspected_item != "item-7" || evaluations != 3 || status_detached != 2) return 10;
    panel_owner.dispose();
    runtime.flush();
    if (!selection_observer_connected || !runtime.deliver_cleanups().empty() || selection_observer_connected ||
        status_detached != 3 || unsubscribed.size() != 4) return 11;
    std::cout << "Host inspected " << *inspected_item << ". Synthetic core-only panel; placeholder text, no native window.\n";
}
