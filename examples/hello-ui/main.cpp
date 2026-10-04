#include <tessera/ui/serialization.hpp>
#include <tessera/ui/tree.hpp>
#include <iostream>
#include <utility>

int main() {
    tessera::UiDocument document;
    document.root.id = "menu";
    tessera::UiNode title;
    title.kind = tessera::NodeKind::text;
    title.id = "title";
    title.properties["text"] = std::string("Tessera / テセラ");
    tessera::UiNode item;
    item.id = "start";
    item.properties["focusable"] = true;
    item.properties["labelled_by"] = tessera::NodeReference{"title"};
    item.events["activate"] = "start_game";
    document.root.children = {std::move(title), std::move(item)};
    const tessera::ValidationContext context{{"start_game"}};
    const auto saved = tessera::save_document(document, context);
    if (!saved) return 1;
    auto loaded = tessera::load_document(*saved.value, context);
    if (!loaded || *loaded.value != document) return 2;
    auto tree = tessera::UiTree::create(*loaded.value, context);
    if (!tree) return 3;
    const auto children = (*tree.value)->children((*tree.value)->root());
    if (children.size() != 2 || (*tree.value)->get(children[0])->id != "title" ||
        (*tree.value)->get(children[1])->id != "start") return 4;
    std::cout << *saved.value << "Validated and restored " << (*tree.value)->size()
              << " ordered nodes. No rendering or text shaping is performed.\n";
}
