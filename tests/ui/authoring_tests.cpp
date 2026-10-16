#include <tessera/ui/authoring.hpp>
#include "../check.hpp"
#include <iostream>

using namespace tessera;
using namespace tessera::authoring;
using tessera::test::check;
using tessera::test::has;

static_assert(!std::is_constructible_v<Value<std::string>, Signal<bool>>);
static_assert(!std::is_copy_constructible_v<Session>);

namespace {
template<class T> T take(Result<T> result) {
    if (!result) throw std::runtime_error(result.diagnostics.front().message);
    return std::move(*result.value);
}
const SourceLocation origin{"fixture://settings.cpp", {20, 45}};
const ValidationContext actions{{"toggle"}};

void composition_and_updates() {
    Session ui;
    const auto enabled = ui.state("enabled", false);
    int calculations = 0;
    const auto summary = ui.computed("summary", [enabled, &calculations] {
        ++calculations;
        return enabled.read() ? std::string("Enabled") : std::string("Disabled");
    });
    const auto disabled = ui.state("disabled", false);
    const auto form = Column({Text(summary, {.id = "summary", .source = origin}),
        Row({Button("Toggle", "toggle", {.node = {.id = "toggle"}, .disabled = disabled})},
            {.style = {.gap = 8.f}})}, {.id = "settings"});
    auto first = take(ui.lower(form, actions));
    check(calculations == 1 && first.document.root.children[0].properties.at("text") == Property{std::string("Disabled")},
        "Initial derived value differs");
    auto tree = take(UiTree::create(first.document, actions));
    check(tree->size() == 5 && first.styles.size() == 5 && first.styles[2].direction == FlexDirection::row,
        "Composition or preorder alignment differs");
    check(first.styles[0].direction == FlexDirection::column && first.styles[2].gap == 8,
        "Row/Column declarations differ");
    check(validate_sources(first.document, first.sources).empty() && first.sources.nodes[1].invocation == origin &&
        !first.sources.nodes[1].mapped, "C++ expression provenance must not invent template byte spans");
    const StyleSheet sheet;
    const auto styles = take(resolve_styles({tree.get(), &sheet, {}, first.styles}));
    check(styles[2].direction == FlexDirection::row && styles[2].gap == 8, "Ordinary style resolution differs");
    ui.publish();
    UiDocument ordinary;
    ordinary.root.id = "settings";
    UiNode summary_node; summary_node.kind = NodeKind::text; summary_node.id = "summary";
    summary_node.properties["text"] = std::string("Disabled");
    UiNode button_node; button_node.id = "toggle";
    button_node.properties = {{"focusable", true}, {"disabled", false}};
    button_node.events["activate"] = "toggle";
    UiNode label_node; label_node.kind = NodeKind::text; label_node.properties["text"] = std::string("Toggle");
    button_node.children.push_back(label_node);
    UiNode row_node; row_node.children.push_back(button_node);
    ordinary.root.children = {summary_node, row_node};
    check(first.document == ordinary, "High-level composition must lower to the ordinary document representation");
    ui.update([&] { enabled.write(true); disabled.write(true); });
    auto second = take(ui.lower(form, actions));
    check(calculations == 2 && second.document.root.children[0].properties.at("text") == Property{std::string("Enabled")} &&
        second.document.root.children[1].children[0].properties.at("disabled") == Property{true},
        "State and computed/disabled bindings must settle together");
    check(first.document.root.children[0].properties.at("text") == Property{std::string("Disabled")},
        "Lowering mutated an accepted candidate");
    (void)take(ui.lower(form, actions));
    check(calculations == 2, "Unchanged descriptions must reuse graph caches");
    ui.publish();
}

void rejection_and_preflight() {
    Session ui;
    int calculations = 0;
    auto text = ui.computed("label", [&] { ++calculations; return std::string("Accepted"); });
    auto accepted = take(ui.lower(Text(text, {.id = "value"})));
    auto invalid_style = ui.lower(Text(text, {.style = {.opacity = 2.f}, .source = origin}));
    check(!invalid_style && has(invalid_style.diagnostics, "out_of_range", "/root/style/opacity") &&
        invalid_style.diagnostics[0].source && invalid_style.diagnostics[0].source->location == origin,
        "Style rejection must locate the supplied expression");
    auto invalid_action = ui.lower(Button("Toggle", "unknown", {.node = {.source = origin}}), actions);
    check(!invalid_action && invalid_action.diagnostics[0].source, "Unknown action needs a located diagnostic");
    check(!ui.lower(Column({Text("A", {.id = "duplicate"}), Text("B", {.id = "duplicate"})})),
        "Duplicate identity accepted");
    check(!ui.lower(Text("bad", {.source = SourceLocation{"fixture", {9, 1}}})), "Invalid source span accepted");
    auto invalid_text = ui.lower(Text(std::string("\xff"), {.source = origin}));
    check(!invalid_text && has(invalid_text.diagnostics, "invalid_utf8", "/root/properties/text") &&
        invalid_text.diagnostics[0].source->location == origin, "Invalid text needs a located property diagnostic");
    check(accepted.document.root.properties.at("text") == Property{std::string("Accepted")},
        "Rejected candidates changed accepted content");

    Session pending;
    int reads = 0;
    auto value = pending.computed("pending", [&] { ++reads; return std::string("Pending"); });
    check(!pending.lower(Text(value, {.style = {.gap = -1.f}})) && reads == 0,
        "Invalid declarations must fail before graph evaluation");
    check(!pending.lower(Column({Text(value, {.id = "same"}), Text("other", {.id = "same"})})) && reads == 0,
        "Document preflight must precede graph evaluation");
    check(!pending.lower(Column({Text(value), Text(std::string("\xff"))})) && reads == 0,
        "Invalid literal props must fail before graph evaluation");
    (void)take(pending.lower(Text(value)));
    check(reads == 1, "Preflight rejection must permit repair");
}

void controlled_checkbox() {
    Session ui;
    const auto checked = ui.state("checked", false);
    const auto disabled = ui.state("disabled", false);
    const auto label = ui.state("checkbox-label", std::string("Enable feature"));
    const auto form = Checkbox(label, checked, "toggle", {.node = {.id = "feature", .source = origin},
        .disabled = disabled});
    const auto first = take(ui.lower(form, actions));
    check(first.document.root.children[0].properties.at("text") == Property{std::string("[ ]")},
        "Unchecked control must expose its value through ordinary Text");
    ui.update([&] { checked.write(true); disabled.write(true); label.write("Feature"); });
    const auto second = take(ui.lower(form, actions));
    check(second.document.root.children[0].properties.at("text") == Property{std::string("[x]")} &&
        second.document.root.children[1].properties.at("text") == Property{std::string("Feature")} &&
        second.document.root.properties.at("disabled") == Property{true},
        "Checked, label and disabled bindings must settle in one candidate");
    check(first.document.root.children[0].properties.at("text") == Property{std::string("[ ]")},
        "Controlled value update mutated the accepted snapshot");
    check(validate_sources(second.document, second.sources).empty() &&
        second.sources.nodes[1].property_arguments.at("text") == origin,
        "Generated checked marker needs its caller's source origin");
    Session foreign;
    const auto wrong = foreign.state("checked", true);
    auto rejected = ui.lower(Checkbox("Foreign", wrong, "toggle", {.node = {.source = origin}}), actions);
    check(!rejected && rejected.diagnostics[0].source &&
        rejected.diagnostics[0].path == "/root/children/0/properties/text" &&
        rejected.diagnostics[0].source->location == origin,
        "Foreign checked observation needs a located rejection");
    int reads = 0;
    const auto pending = ui.computed("pending-check", [&] { ++reads; return true; });
    check(!ui.lower(Checkbox(std::string("\xff"), pending, "toggle"), actions) && reads == 0,
        "Invalid Checkbox label must reject before checked evaluation");
    check(!ui.lower(Checkbox("Unknown", true, "unknown", {.node = {.source = origin}}), actions),
        "Unregistered Checkbox command accepted");
    auto deepest = Checkbox("Bounded", true, "toggle");
    for (std::size_t i = 2; i < max_document_depth; ++i) deepest = Column({deepest});
    check(bool(ui.lower(deepest, actions)) && !ui.lower(Column({deepest}), actions),
        "Generated Checkbox children must count against depth bounds");
}

void lifetimes_and_recovery() {
    Signal<std::string> expired;
    std::optional<Element> description;
    {
        Session ui;
        expired = ui.state("label", std::string("alive"));
        description = Text(expired, {.source = origin});
        (void)take(ui.lower(*description));
    }
    bool stale = false;
    try { expired.write("late"); } catch (const ReactiveError&) { stale = true; }
    check(stale, "A description must not retain its session lifetime");
    Session other;
    auto rejected = other.lower(*description);
    check(!rejected && rejected.diagnostics[0].path == "/root/properties/text" &&
        rejected.diagnostics[0].source->location == origin, "Expired binding needs its authoring location");
    Session live;
    auto foreign = live.state("label", std::string("foreign"));
    check(!other.lower(Text(foreign)), "Foreign-runtime binding accepted");
    auto own = other.state("label", std::string("own"));
    other.update([&] {
        check(!other.lower(Text(own)), "Lowering inside a batch accepted");
        own.write("updated");
    });
    check(take(other.lower(Text(own))).document.root.properties.at("text") == Property{std::string("updated")},
        "Rejected batch entry did not recover");
    bool fail = true;
    auto derived = other.computed("retry", [&] {
        if (fail) throw std::runtime_error("fixture calculation failed");
        return own.read();
    });
    check(!other.lower(Text(derived)), "Calculation failure accepted");
    fail = false;
    (void)take(other.lower(Text(derived)));
    other.publish();
}

void bounded_descriptions() {
    Session ui;
    Element nested = Text("deep");
    for (std::size_t i = 1; i < max_document_depth; ++i) nested = Column({nested});
    check(bool(ui.lower(nested)), "Exact depth bound rejected");
    check(!ui.lower(Column({nested})), "Oversized depth accepted");
    const auto leaf = Text("shared");
    std::vector<Element> children(max_document_nodes - 1, leaf);
    check(bool(ui.lower(Column(children))), "Exact node bound rejected");
    children.push_back(leaf);
    check(!ui.lower(Column(std::move(children))), "Shared descriptions bypassed expanded node bound");
    auto moved = Text("moved");
    const auto retained = std::move(moved);
    check(!ui.lower(moved) && bool(ui.lower(retained)), "Moved-from description handling differs");
}
} // namespace

int main() {
    try {
        composition_and_updates(); rejection_and_preflight(); controlled_checkbox(); lifetimes_and_recovery(); bounded_descriptions();
        std::cout << "Authoring composition, state, diagnostics and lifetime checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
