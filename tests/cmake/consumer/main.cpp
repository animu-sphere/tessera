#include <tessera/ui/serialization.hpp>
#include <tessera/ui/tree.hpp>
#include <tessera/ui/async.hpp>
#include <tessera/ui/component.hpp>
#include <tessera/ui/component_mount.hpp>
#include <tessera/ui/component_tree_mount.hpp>
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
    arguments.props["caption"] = tessera::Property{true};
    const auto rejected = tessera::lower_component(runtime, component, arguments);
    if (rejected || rejected.diagnostics.empty() || !rejected.diagnostics[0].source ||
        rejected.diagnostics[0].source->location != origin ||
        rejected.diagnostics[0].source->origin != tessera::DiagnosticOrigin::argument) return 10;
    std::vector<tessera::Diagnostic> diagnostics{{"fixture", tessera::Severity::warning, "/root/children/0/properties/text", "Caption", {}}};
    if (!tessera::locate_diagnostics(bound.value->document, *bound.value->sources, diagnostics).empty() ||
        !diagnostics[0].source || diagnostics[0].source->location != origin) return 11;
    component.local_state = {{"pinned", tessera::PropertyType::boolean, tessera::Property{false}}};
    component.cleanups = {"detach"};
    tessera::ComponentCollection components(runtime.owner(runtime.root(), "components"));
    int setups = 0;
    const std::vector<tessera::ComponentMount> mounts{{"panel", &component,
        [&](tessera::ReactiveRuntime& setup_runtime, const tessera::ComponentSetup& setup) {
            ++setups;
            const auto pinned = setup.state<bool>("pinned");
            if (!pinned || setup.instance().key != "panel") throw std::runtime_error("Invalid setup state");
            pinned.value->write(true);
            (void)setup_runtime;
        }, {{"detach", [] {}}}}};
    if (!components.reconcile(runtime, mounts)) return 12;
    const auto local = components.state<bool>("panel", "pinned");
    if (!local || !local.value->read() || !components.reconcile(runtime, mounts) || setups != 1 ||
        !components.state<bool>("panel", "pinned").value->read()) return 13;
    tessera::ComponentTreeCollection invocation_tree(runtime.owner(runtime.root(), "invocations"));
    tessera::ComponentInvocation invocation{component, {}, {}};
    invocation.component.async = {{"work", tessera::PropertyType::boolean}};
    invocation.component.effects = {{"observe-pin", tessera::PropertyType::boolean, tessera::EffectPhase::notify}};
    invocation.arguments.props["caption"] = title.properties.at("text");
    int detached = 0;
    int declared_cleanups = 0;
    int effect_factories = 0, notifications = 0, effect_closed = 0;
    const std::vector<tessera::ComponentInvocationMount> invocation_mounts{{"panel",
        [&](tessera::ReactiveRuntime& setup_runtime, const tessera::ComponentSetup& setup) {
            setup.state<bool>("pinned").value->write(true);
            const auto work = *setup.async("work").value;
            (void)setup_runtime;
            if (!work.begin("host-request")) throw std::runtime_error("Invalid host request");
        }, {{"detach", [&] { ++declared_cleanups; }}},
        {{"observe-pin", [&](const tessera::ComponentSetup& setup) {
            ++effect_factories;
            const auto pinned = *setup.state<bool>("pinned").value;
            return tessera::component_effect<bool>([pinned] { return pinned.read(); }, [&](bool value) {
                if (!value) throw std::runtime_error("Wrong published pin value");
                ++notifications;
                return [&] { ++effect_closed; };
            });
        }}}, {{"work", [&](const tessera::AsyncAttempt& address) {
            if (address.request() != "host-request" || address.sequence() != 1) throw std::runtime_error("Wrong work address");
            ++detached;
        }}}}};
    const auto mounted = invocation_tree.reconcile(runtime, {&invocation, 1}, invocation_mounts);
    if (!mounted || !mounted.value->instances[0].state<bool>("pinned").value->read() ||
        !invocation_tree.reconcile(runtime, {&invocation, 1}, invocation_mounts).value->created.empty()) return 14;
    runtime.flush();
    if (!tessera::lower_component_tree(runtime, {&invocation, 1})) return 15;
    if (effect_factories != 1 || notifications != 0 || runtime.publish() != 1 ||
        !runtime.deliver_effects(tessera::EffectPhase::notify).empty() || notifications != 1) return 17;
    if (!invocation_tree.reconcile(runtime, {}, {}) || detached != 0 || declared_cleanups != 0 ||
        !runtime.deliver_cleanups().empty() || detached != 1 || declared_cleanups != 1 ||
        !runtime.deliver_cleanups().empty() || detached != 1 || declared_cleanups != 1 || effect_closed != 1) return 16;
    std::cout << "Source consumer: JSON, tree, CPU capture, async detachment, declared cleanup/effects/async, theme, component origins, diagnostics and invocation setup agree.\n";
}
