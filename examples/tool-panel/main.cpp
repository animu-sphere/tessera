#include <tessera/commands/commands.hpp>
#include <tessera/input/focus.hpp>
#include <iostream>

using namespace tessera;
using namespace std::chrono_literals;

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

    for (const bool selected : {false, true}) {
        const std::uint64_t generation = selected ? 2 : 1;
        const auto commands = CommandSnapshot::publish(*registry.value, generation, CommandMode::production,
            {{inspect.id, {selected, false, selected ? "" : "Select an item."}}}, selected ? CommandObjectIds{"item-7"} : CommandObjectIds{});
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
                {{"item", CommandObjectId{"item-7"}}}, CommandSource::keyboard, ++sequence);
            if (!invocation || !commands.value->recheck(*invocation.value, &ui).empty()) return 9;
            // Dispatch and validation have returned. Execute the non-editing host command here.
            inspected_item = std::get<CommandObjectId>(invocation.value->record().arguments.at("item")).value;
        }
        const auto info = commands.value->lookup(inspect.id);
        std::cout << "Generation " << generation << ": " << info.value->descriptor.label << " enabled=" << selected;
        if (!selected) std::cout << " (" << info.value->state.disabled_reason << ')';
        std::cout << '\n';
    }
    if (inspected_item != "item-7") return 10;
    std::cout << "Host inspected " << *inspected_item << ". Synthetic core-only panel; placeholder text, no native window.\n";
}
