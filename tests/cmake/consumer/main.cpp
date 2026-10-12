#include <tessera/ui/serialization.hpp>
#include <tessera/ui/tree.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <concepts>
#include <iostream>

// This requires the target's transitive C++20 feature on the host executable.
static_assert(std::equality_comparable<tessera::UiDocument>);

int main() {
    tessera::UiDocument document;
    document.root.id = "panel";
    tessera::UiNode title;
    title.kind = tessera::NodeKind::text;
    title.id = "title";
    title.properties["text"] = std::string("Tessera / テセラ");
    document.root.children.push_back(title);

    const auto saved = tessera::save_document(document);
    if (!saved) return 1;
    const auto loaded = tessera::load_document(*saved.value);
    if (!loaded || *loaded.value != document) return 2;
    const auto tree = tessera::UiTree::create(*loaded.value);
    if (!tree || (*tree.value)->size() != 2) return 3;
    const auto target = (*tree.value)->find("title");
    if (!target || (*tree.value)->get(*target)->properties.at("text") != title.properties.at("text")) return 4;
    tessera::CpuFrameCapture frames;
    const tessera::UiDrawList paint{{tessera::DrawRect{{{0,0},{1,1}},{1,0,0,1}}}};
    const auto capture = frames.capture({0,{1,1},1,{1,1},&paint});
    if (!capture || capture.value->pixels != std::vector<std::uint8_t>{255,0,0,255}) return 5;
    std::cout << "Source consumer: JSON round trip, owned tree and scalar CPU capture agree.\n";
}
