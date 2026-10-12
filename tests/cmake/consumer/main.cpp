#include <tessera/ui/serialization.hpp>
#include <tessera/ui/tree.hpp>
#include <tessera/ui/async.hpp>
#include <tessera/ui/component.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/style/style_sheet.hpp>
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
    tessera::ReactiveRuntime runtime;
    tessera::AsyncValue asset(runtime, runtime.root(), "asset", tessera::PropertyType::string);
    const auto attempt = asset.begin("host-request");
    if (!attempt || !asset.complete(attempt.value->attempt, std::string("Ready"))) return 6;
    runtime.flush();
    const auto observed = asset.snapshot(runtime);
    if (observed.status != tessera::AsyncStatus::ready || std::get<std::string>(observed.value->value) != "Ready") return 7;
    tessera::Theme theme;
    const tessera::Color ink{0.2f, 0.4f, 0.8f, 1};
    theme.variables = {{"ink", tessera::ThemeValue{ink}}};
    theme.defaults.rules = {{tessera::StyleSelector::of_id("panel"), {}, {{"color", {"ink", {}}}}}};
    const tessera::StyleSheet sheet;
    const auto styles = tessera::resolve_styles({tree.value->get(), &sheet, {}, {}, &theme});
    if (!tessera::validate(theme).empty() || !styles || styles.value->at(target->index).color != ink) return 8;
    tessera::ComponentDescription component;
    component.kind = "panel"; component.document = document;
    component.props = {{"caption", tessera::PropertyType::string, {}}};
    component.properties = {{"caption", "title", "text"}};
    tessera::ComponentArguments arguments;
    arguments.props["caption"] = title.properties.at("text");
    const tessera::SourceLocation origin{"fixture:consumer", {0, 8}};
    arguments.sources = tessera::ComponentArgumentSources{origin, {{"caption", origin}}, {}, {}};
    const auto bound = tessera::lower_component(runtime, component, arguments);
    if (!tessera::validate_source_location(origin, "/origin").empty() || !bound || bound.value->document != document ||
        !bound.value->sources || !tessera::validate_sources(bound.value->document, *bound.value->sources).empty() ||
        bound.value->sources->nodes[1].property_arguments.at("text") != origin) return 9;
    std::cout << "Source consumer: JSON round trip, owned tree, scalar CPU capture, async observation, theme and component origins agree.\n";
}
