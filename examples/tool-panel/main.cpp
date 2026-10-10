#include <tessera/commands/commands.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/reactive/runtime.hpp>
#include <iostream>

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

    std::uint64_t generation = 0;
    for (const bool selected : {false, true, false}) {
        runtime.batch([&] {
            host_selection = selected ? std::optional<std::string>{"item-7"} : std::nullopt;
            // Publish an owned host-state observation at the update point.
            selected_item.write(host_selection);
        });
        runtime.flush();
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
        std::cout << '\n';
    }
    if (inspected_item != "item-7" || evaluations != 3) return 10;
    panel_owner.dispose();
    runtime.flush();
    if (!selection_observer_connected || !runtime.deliver_cleanups().empty() || selection_observer_connected) return 11;
    std::cout << "Host inspected " << *inspected_item << ". Synthetic core-only panel; placeholder text, no native window.\n";
}
