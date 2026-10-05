#include <tessera/input/focus.hpp>
#include <tessera/semantics/semantics.hpp>
#include <tessera/style/style_sheet.hpp>
#include "../check.hpp"
#include <iostream>
#include <memory>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {

tessera::UiNode box(std::vector<tessera::UiNode> children = {}) {
    tessera::UiNode result;
    result.children = std::move(children);
    return result;
}
tessera::UiNode text(std::string value) {
    tessera::UiNode result;
    result.kind = tessera::NodeKind::text;
    result.properties["text"] = std::move(value);
    return result;
}
tessera::UiNode button(std::string id, std::string caption, std::string action) {
    auto result = box({text(std::move(caption))});
    result.id = std::move(id);
    result.events["activate"] = std::move(action);
    return result;
}

// Preorder: 0 menu, 1 title, 2 start, 3 "Start", 4 options, 5 "Options", 6 quit, 7 "Quit",
// 8 hint box, 9 quit-hint (display none), 10 footer (hidden), 11 "Version 1".
tessera::UiDocument menu() {
    tessera::UiDocument document;
    auto& root = document.root;
    root.id = "menu";
    root.properties["labelled_by"] = tessera::NodeReference{"title"};
    auto title = text("Main Menu");
    title.id = "title";
    auto start = button("start", "Start", "start-game");
    start.properties["focusable"] = true;
    auto options = button("options", "Options", "open-options");
    options.properties["disabled"] = true;
    auto quit = button("quit", "Quit", "quit-game");
    quit.properties["labelled_by"] = tessera::NodeReference{"quit-hint"};
    auto hint = text("Leave game");
    hint.id = "quit-hint";
    root.children = {std::move(title), std::move(start), std::move(options), std::move(quit),
                     box({std::move(hint)}), box({text("Version 1")})};
    return document;
}

struct Fixture {
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper shaper;
    tessera::LayoutResult layout;

    explicit Fixture(const tessera::UiDocument& document = menu()) {
        auto created = tessera::UiTree::create(document, {{"start-game", "open-options", "quit-game"}});
        check(static_cast<bool>(created), "Fixture tree rejected");
        tree = std::move(*created.value);
        styles.resize(tree->size());
        if (tree->size() == 12) {
            styles[9].display = tessera::Display::none;
            styles[10].visibility = tessera::Visibility::hidden;
        }
        auto result = tessera::compute_layout({tree.get(), styles, {200, 200}, &shaper});
        check(static_cast<bool>(result), "Fixture layout rejected");
        layout = std::move(*result.value);
    }
    tessera::NodeHandle node(std::uint32_t index) const { return {tree->root().tree, index}; }
    tessera::SemanticInput input(std::optional<tessera::NodeHandle> focused = {}) const {
        return {tree.get(), styles, &layout, focused};
    }
    tessera::SemanticTree project(std::optional<tessera::NodeHandle> focused = {}) const {
        auto result = tessera::build_semantic_tree(input(focused));
        check(result && result.diagnostics.empty(), "Valid semantic projection rejected or warned");
        return std::move(*result.value);
    }
    tessera::Result<tessera::ActionRequest> invoke(tessera::NodeHandle target, std::string_view binding = "activate") const {
        return tessera::request_semantic_action(input(), target, binding);
    }
};

void projection_derives_roles_names_states_and_actions() {
    const Fixture fixture;
    const auto semantics = fixture.project();
    const auto& nodes = semantics.nodes;
    check(nodes.size() == 6, "Labels, flattened boxes, hidden boxes, and display-none nodes must have no entries");
    using Role = tessera::SemanticRole;
    using Source = tessera::NameSource;

    check(nodes[0].node == fixture.node(0) && nodes[0].id == "menu" && nodes[0].role == Role::root &&
              nodes[0].parent == tessera::no_semantic_parent && nodes[0].layout_box == 0,
          "Root entry identity");
    check(nodes[0].name == "Main Menu" && nodes[0].name_source == Source::relationship &&
              nodes[0].labelled_by == fixture.node(1),
          "labelled_by names the root through the referenced node");
    check(nodes[1].node == fixture.node(1) && nodes[1].role == Role::text && nodes[1].name == "Main Menu" &&
              nodes[1].name_source == Source::content && nodes[1].parent == 0 && nodes[1].actions.empty(),
          "Text outside a button is a text entry");

    const std::vector<tessera::SemanticAction> start_actions{{"activate", "start-game"}};
    check(nodes[2].node == fixture.node(2) && nodes[2].role == Role::button && nodes[2].name == "Start" &&
              nodes[2].name_source == Source::content && nodes[2].enabled && nodes[2].focusable &&
              nodes[2].actions == start_actions && nodes[2].layout_box == 2 && !nodes[2].labelled_by,
          "Button named by its Text content with an eligible activate action");
    check(nodes[3].node == fixture.node(4) && nodes[3].name == "Options" && !nodes[3].enabled &&
              !nodes[3].focusable && nodes[3].actions.empty(),
          "Disabled button stays exposed without actions");
    check(nodes[4].node == fixture.node(6) && nodes[4].name == "Leave game" &&
              nodes[4].name_source == Source::relationship && nodes[4].labelled_by == fixture.node(9),
          "A display-none referenced label still names the button");

    check(nodes[5].node == fixture.node(11) && nodes[5].role == Role::text && nodes[5].name == "Version 1" &&
              nodes[5].parent == 0 && nodes[5].layout_box == 10,
          "Visible Text under a hidden box attaches to the nearest included ancestor");
    for (std::size_t i = 1; i < nodes.size(); ++i) check(nodes[i].parent == 0, "Flattened entries are root children");
    for (const auto& entry : nodes) check(!entry.focused, "Without supplied focus no entry is focused");

    check(fixture.project() == semantics, "Projection is deterministic for one snapshot");
}

void nesting_fallback_and_missing_names() {
    // 0 root, 1 outer, 2 "Outer", 3 hidden "Secret", 4 inner, 5 "Inner", 6 text button, 7 unnamed, 8 empty label.
    tessera::UiDocument document;
    auto outer = button("outer", "Outer", "go");
    outer.children.push_back(text("Secret"));
    outer.children.push_back(button("inner", "Inner", "go"));
    auto text_button = text("Go");
    text_button.events["activate"] = "go";
    auto unnamed = button("unnamed", "", "go");
    unnamed.children.clear();
    auto empty_label = text("");
    empty_label.id = "empty";
    unnamed.properties["labelled_by"] = tessera::NodeReference{"empty"};
    document.root.children = {std::move(outer), std::move(text_button), std::move(unnamed), std::move(empty_label)};

    auto created = tessera::UiTree::create(document, {{"go"}});
    check(static_cast<bool>(created), "Nesting tree rejected");
    const auto tree = std::move(*created.value);
    std::vector<tessera::ResolvedStyle> styles(tree->size());
    styles[3].visibility = tessera::Visibility::hidden;
    tessera::PlaceholderTextShaper shaper;
    const auto layout = tessera::compute_layout({tree.get(), styles, {200, 200}, &shaper});
    check(static_cast<bool>(layout), "Nesting layout rejected");

    const auto result = tessera::build_semantic_tree({tree.get(), styles, &*layout.value});
    check(static_cast<bool>(result), "Projection with warnings must still produce output");
    const auto& nodes = result.value->nodes;
    check(nodes.size() == 6, "Root, outer, inner, text button, unnamed button, and empty text");
    check(nodes[1].name == "Outer" && nodes[2].name == "Inner" && nodes[2].parent == 1,
          "Nested buttons own their own labels; hidden text does not contribute");
    check(nodes[3].role == tessera::SemanticRole::button && nodes[3].name == "Go",
          "A Text node with an activate binding is a button named by its text");
    check(nodes[4].name.empty() && nodes[4].name_source == tessera::NameSource::none &&
              nodes[4].labelled_by == tessera::NodeHandle{tree->root().tree, 8},
          "An empty relationship leaves the button unnamed");
    check(nodes[5].role == tessera::SemanticRole::text && nodes[5].name.empty(), "Empty Text remains a text entry");
    check(result.diagnostics.size() == 1 && result.diagnostics[0].code == "missing_name" &&
              result.diagnostics[0].severity == tessera::Severity::warning && result.diagnostics[0].path == "/nodes/7",
          "Unnamed buttons produce a located warning");
}

void invocation_matches_pointer_activation() {
    const Fixture fixture;
    const auto requested = fixture.invoke(fixture.node(2));
    check(requested && requested.diagnostics.empty(), "Eligible semantic activation rejected");

    tessera::PointerDispatcher pointer;
    const tessera::HitTestInput hit{fixture.tree.get(), fixture.styles, &fixture.layout};
    const auto& rect = fixture.layout.boxes[2].border_box;
    const tessera::Point center{rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
    check(static_cast<bool>(pointer.dispatch(hit, {1ms, tessera::PointerDown{{1}, center}})), "Pointer down rejected");
    const auto clicked = pointer.dispatch(hit, {2ms, tessera::PointerUp{{1}, center}});
    check(clicked && clicked.value->actions.size() == 1 && clicked.value->actions[0] == *requested.value,
          "Semantic and pointer activation must produce the same ActionRequest");

    const auto& options = fixture.layout.boxes[4].border_box;
    const tessera::Point disabled_center{options.origin.x + 1, options.origin.y + 1};
    check(static_cast<bool>(pointer.dispatch(hit, {3ms, tessera::PointerDown{{1}, disabled_center}})), "Down rejected");
    const auto disabled_click = pointer.dispatch(hit, {4ms, tessera::PointerUp{{1}, disabled_center}});
    check(disabled_click && disabled_click.value->actions.empty(), "Pointer cannot activate a disabled button");
    check(has(fixture.invoke(fixture.node(4)).diagnostics, "disabled_target", "/target"),
          "Semantic invocation agrees with pointer disabled eligibility");
}

void invocation_rejects_ineligible_and_stale_targets() {
    const Fixture fixture;
    const auto rejected = [](const tessera::Result<tessera::ActionRequest>& result, std::string_view code,
                             std::string_view path) { return !result && has(result.diagnostics, code, path); };
    check(rejected(fixture.invoke(fixture.node(3)), "unsupported_action", "/binding"),
          "A button label is not a semantic action target");
    check(rejected(fixture.invoke(fixture.node(8)), "unsupported_action", "/binding"),
          "A flattened container has no actions");
    check(rejected(fixture.invoke(fixture.node(2), "cancel"), "unsupported_action", "/binding"),
          "Unexposed bindings are rejected");
    check(rejected(fixture.invoke(fixture.node(9)), "hidden_target", "/target"), "Display-none target");
    check(rejected(fixture.invoke(fixture.node(10)), "hidden_target", "/target"), "Hidden target");

    // A replacement with the same author IDs has a new identity; old handles do not resolve.
    const Fixture replacement;
    check(rejected(replacement.invoke(fixture.node(2)), "stale_target", "/target"),
          "Handles from a replaced tree must be rejected even when author IDs match");
    check(rejected(replacement.invoke({}), "stale_target", "/target"), "Invalid handle");
    const auto fresh = replacement.invoke(*replacement.tree->find("start"));
    check(fresh && fresh.value->target == replacement.node(2), "Resolving the author ID again succeeds");

    auto styles = fixture.styles;
    styles.pop_back();
    const tessera::SemanticInput mismatched{fixture.tree.get(), styles, &fixture.layout};
    const auto projected = tessera::build_semantic_tree(mismatched);
    check(!projected && has(projected.diagnostics, "style_count", "/styles"), "Incoherent snapshot rejected");
    check(rejected(tessera::request_semantic_action(mismatched, fixture.node(2), "activate"), "style_count", "/styles"),
          "Invocation validates its snapshot");
    check(has(tessera::validate(tessera::SemanticInput{}), "missing_input", "/tree"), "Missing inputs rejected");
}


// Focus state must agree with focus dispatch eligibility and with `:focus` style resolution.
void focus_state_matches_dispatch_and_style() {
    auto document = menu();
    document.root.children[2].properties["focusable"] = true; // Options, disabled.
    document.root.children[3].properties["focusable"] = true; // Quit.
    Fixture fixture(document);
    constexpr tessera::Color ring{1, 0, 0, 1};
    tessera::StyleDeclarations focus_rule;
    focus_rule.border_color = ring;
    const tessera::StyleSheet sheet{{{tessera::StyleSelector::of_type(tessera::NodeKind::box, {.focus = true}), focus_rule}}};
    std::vector<tessera::StyleDeclarations> overrides(fixture.tree->size());
    overrides[9].display = tessera::Display::none;
    overrides[10].visibility = tessera::Visibility::hidden;
    // Resolves styles for the focus and recomputes layout, as a host does at an update point.
    const auto settle = [&](std::optional<tessera::NodeHandle> focused) {
        auto styles = tessera::resolve_styles({fixture.tree.get(), &sheet, {{}, {}, focused}, overrides});
        check(static_cast<bool>(styles), "Focus styles rejected");
        fixture.styles = std::move(*styles.value);
        auto layout = tessera::compute_layout({fixture.tree.get(), fixture.styles, {200, 200}, &fixture.shaper});
        check(static_cast<bool>(layout), "Focus layout rejected");
        fixture.layout = std::move(*layout.value);
    };
    // Exactly the styled node is focused, and only eligible buttons report focusable.
    const auto agrees = [&](std::optional<tessera::NodeHandle> focused, std::optional<tessera::NodeHandle> expected) {
        const auto semantics = fixture.project(focused);
        for (const auto& entry : semantics.nodes) {
            const bool styled = fixture.styles[entry.node.index].border_color == ring;
            if (entry.focused != (entry.node == expected) || entry.focused != styled) return false;
            const bool eligible = entry.node == fixture.node(2) || (entry.node == fixture.node(6) && fixture.layout.boxes[6].visible);
            if (entry.focusable != eligible) return false;
        }
        return true;
    };

    settle({});
    tessera::FocusDispatcher focus;
    const std::vector<std::uint32_t> order{2, 6, 2}; // Disabled Options is skipped; traversal wraps.
    auto time = 1ms;
    for (const auto expected : order) {
        const auto moved = focus.dispatch({fixture.tree.get(), fixture.styles, &fixture.layout}, {time++, tessera::FocusNext{}});
        check(moved && moved.value->focused == fixture.node(expected), "Focus traversal fixture");
        settle(moved.value->focused);
        check(agrees(moved.value->focused, fixture.node(expected)), "Semantic focus follows dispatch and :focus style");
    }

    // A disabled node supplied as focus is neither styled nor reported, without a warning.
    settle(fixture.node(4));
    check(agrees(fixture.node(4), {}), "Disabled focus is suppressed in both style and semantics");
    check(tessera::build_semantic_tree(fixture.input(fixture.node(4))).diagnostics.empty(), "Ineligible focus is not a warning");

    // Hiding the focused node reports no focus until the host refreshes dispatch, which recovers to Start.
    overrides[6].visibility = tessera::Visibility::hidden;
    settle(fixture.node(6));
    check(agrees(fixture.node(6), {}), "Hidden focus has no entry and no style match on an entry");
    const auto recovered = focus.refresh({fixture.tree.get(), fixture.styles, &fixture.layout});
    check(recovered && recovered.value->focused == fixture.node(2), "Recovery fixture");
    settle(recovered.value->focused);
    check(agrees(recovered.value->focused, fixture.node(2)), "Recovered focus is projected");
}

void focus_without_entry_and_stale_focus() {
    auto document = menu();
    document.root.children[4].properties["focusable"] = true;             // Flattened hint Box.
    document.root.children[1].children[0].properties["focusable"] = true; // Start's presentational label.
    const Fixture fixture(document);
    for (const std::uint32_t index : {3u, 8u}) {
        const auto result = tessera::build_semantic_tree(fixture.input(fixture.node(index)));
        check(result && result.diagnostics.size() == 1 && result.diagnostics[0].code == "focus_not_exposed" &&
                  result.diagnostics[0].severity == tessera::Severity::warning &&
                  result.diagnostics[0].path == "/nodes/" + std::to_string(index),
              "Eligible focus without an entry produces a located warning");
        for (const auto& entry : result.value->nodes) check(!entry.focused, "Focus is not moved to another entry");
    }

    const Fixture replacement(document);
    const auto stale = tessera::build_semantic_tree(replacement.input(fixture.node(2)));
    check(!stale && has(stale.diagnostics, "stale_target", "/focused"), "Focus from a replaced tree is rejected");
    check(has(tessera::request_semantic_action(replacement.input(fixture.node(2)), replacement.node(2), "activate").diagnostics,
              "stale_target", "/focused"),
          "Invocation validates the supplied focus");
}

} // namespace

int main() {
    try {
        projection_derives_roles_names_states_and_actions();
        nesting_fallback_and_missing_names();
        invocation_matches_pointer_activation();
        invocation_rejects_ineligible_and_stale_targets();
        focus_state_matches_dispatch_and_style();
        focus_without_entry_and_stale_focus();
        std::cout << "Semantic projection and invocation checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
