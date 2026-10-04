#include <tessera/layout/layout_box.hpp>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
tessera::UiNode node(tessera::NodeKind kind, std::string id, std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode result;
    result.kind = kind;
    result.id = std::move(id);
    result.children = std::move(children);
    return result;
}
tessera::UiNode label(std::string id, std::string text) {
    auto result = node(tessera::NodeKind::text, std::move(id));
    result.properties["text"] = std::move(text);
    return result;
}
}

int main() {
    // A centered menu panel: title above a row of two growing buttons.
    tessera::UiDocument document;
    document.root = node(tessera::NodeKind::box, "screen", {node(tessera::NodeKind::box, "panel", {
        label("title", "Tessera"),
        node(tessera::NodeKind::box, "buttons", {
            node(tessera::NodeKind::box, "start", {label("start-label", "Start")}),
            node(tessera::NodeKind::box, "quit", {label("quit-label", "Quit")}),
        }),
    })});
    auto created = tessera::UiTree::create(document);
    if (!created) return 1;
    const auto& tree = **created.value;

    // Already-resolved styles, indexed by node handle; no stylesheet resolution exists yet.
    std::vector<tessera::ResolvedStyle> styles(tree.size());
    auto style = [&](const char* id) -> tessera::ResolvedStyle& { return styles[tree.find(id)->index]; };
    style("screen").justify = tessera::Justify::center;
    style("screen").align = tessera::Align::center;
    auto& panel = style("panel");
    panel.width = tessera::Dimension::points(240);
    panel.padding = {16, 16, 16, 16};
    panel.border = {2, 2, 2, 2};
    panel.gap = 12;
    style("buttons").direction = tessera::FlexDirection::row;
    style("buttons").gap = 8;
    for (const char* id : {"start", "quit"}) {
        style(id).grow = 1;
        style(id).padding = {8, 8, 8, 8};
        style(id).align = tessera::Align::center;
    }

    tessera::PlaceholderTextShaper text;
    const auto result = tessera::compute_layout({&tree, styles, {640, 360}, &text});
    if (!result) return 2;
    for (const auto& box : result.value->boxes) {
        const auto& r = box.border_box;
        std::cout << *tree.get(box.node)->id << ": x=" << r.origin.x << " y=" << r.origin.y
                  << " w=" << r.size.width << " h=" << r.size.height << '\n';
    }
    std::cout << "Laid out " << result.value->boxes.size()
              << " boxes with placeholder text metrics. No rendering is performed.\n";
}
