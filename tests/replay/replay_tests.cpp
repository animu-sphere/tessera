#include <tessera/replay/replay.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>
#include <string>
#include <utility>

using tessera::test::check;
using tessera::test::has;
using namespace std::chrono_literals;

namespace {

tessera::UiNode button(std::string id, std::string caption, std::string action) {
    tessera::UiNode label;
    label.kind = tessera::NodeKind::text;
    label.id = id + "-label";
    label.properties["text"] = std::move(caption);
    tessera::UiNode result;
    result.id = std::move(id);
    result.events["activate"] = std::move(action);
    result.children.push_back(std::move(label));
    return result;
}

tessera::UiNode focusable(tessera::UiNode node, bool disabled = false) {
    node.properties["focusable"] = true;
    if (disabled) node.properties["disabled"] = true;
    return node;
}

tessera::UiDocument menu(std::vector<tessera::UiNode> buttons) {
    tessera::UiDocument document;
    document.root.id = "menu";
    document.root.children = std::move(buttons);
    return document;
}

// Preorder styles for a root followed by button/label pairs.
std::vector<tessera::ResolvedStyle> menu_styles(std::size_t buttons) {
    std::vector<tessera::ResolvedStyle> styles(1 + 2 * buttons);
    styles[0].padding = {12, 12, 12, 12};
    styles[0].gap = 8;
    styles[0].background = {0.05f, 0.05f, 0.05f, 1};
    for (std::size_t i = 0; i < buttons; ++i) {
        auto& style = styles[1 + 2 * i];
        style.padding = {8, 8, 8, 8};
        style.background = {0.2f, 0.3f, 0.5f, 1};
        style.align = tessera::Align::center;
    }
    return styles;
}

tessera::ReplayRecording recording() {
    tessera::ReplayRecording result;
    result.context = {{"start-game", "quit-game"}};
    result.viewport = {200, 120};
    result.document = menu({button("start", "Start", "start-game"), button("quit", "Quit", "quit-game")});
    result.styles = menu_styles(2);
    return result;
}

tessera::Point center(const tessera::ReplayBox& box) {
    const auto& rect = box.border_box;
    return {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
}

tessera::ReplayOutput play(const tessera::ReplayRecording& recording) {
    tessera::PlaceholderTextShaper text;
    auto result = tessera::play_replay(recording, text);
    check(result && result.diagnostics.empty(), "Valid replay rejected");
    return std::move(*result.value);
}

tessera::Result<tessera::ReplayOutput> play_invalid(const tessera::ReplayRecording& recording) {
    tessera::PlaceholderTextShaper text;
    auto result = tessera::play_replay(recording, text);
    check(!result && !result.diagnostics.empty(), "Invalid replay produced output");
    return result;
}

// Clicks, drag-out, resize, and reload through the ordinary tree/layout/paint/pointer path.
tessera::ReplayRecording interaction() {
    auto result = recording();
    const auto initial = play(result);
    check(initial.generations.size() == 1 && !initial.generations[0].step && initial.actions.empty(),
          "Initial replay must settle one generation without actions");
    // Preorder: menu 0, start 1, start-label 2, quit 3, quit-label 4.
    const auto start = center(initial.generations[0].boxes.at(1));
    const auto quit = center(initial.generations[0].boxes.at(3));
    std::chrono::microseconds time{0};
    const auto at = [&](auto data) { return tessera::InputEvent{time += 1us, std::move(data)}; };
    auto& steps = result.steps;
    steps.push_back(at(tessera::PointerDown{{1}, start}));                  // 0
    steps.push_back(at(tessera::PointerUp{{1}, start}));                    // 1: start-game
    steps.push_back(at(tessera::PointerDown{{1}, quit}));                   // 2
    steps.push_back(at(tessera::PointerMove{{1}, {1, 1}}));                 // 3: drag out
    steps.push_back(at(tessera::PointerUp{{1}, {1, 1}}));                   // 4: no action
    steps.push_back(tessera::ReplayResize{{300, 160}});                     // 5: generation 1
    steps.push_back(at(tessera::PointerDown{{1}, quit}));                   // 6
    steps.push_back(at(tessera::PointerUp{{1}, quit}));                     // 7: quit-game
    steps.push_back(at(tessera::PointerDown{{1}, start}));                  // 8: held across reload
    steps.push_back(tessera::ReplayReload{menu({button("resume", "Resume", "start-game")}), menu_styles(1)}); // 9
    steps.push_back(at(tessera::PointerUp{{1}, start}));                    // 10: press was cleared
    steps.push_back(at(tessera::PointerDown{{1}, start}));                  // 11
    steps.push_back(at(tessera::PointerUp{{1}, start}));                    // 12: start-game from resume
    return result;
}

void playback_observes_geometry_paint_and_actions() {
    const auto recording = interaction();
    const auto output = play(recording);
    check(output.generations.size() == 3, "Initial, resize, and reload generations expected");
    check(!output.generations[0].step && output.generations[1].step == 5u && output.generations[2].step == 9u,
          "Generations must name their producing steps");
    const auto& before = output.generations[0];
    const auto& resized = output.generations[1];
    check(before.boxes.size() == 5 && resized.boxes.size() == 5 && output.generations[2].boxes.size() == 3,
          "Every displayed node must be observed");
    check(before.boxes[0].border_box.size.width == 200 && resized.boxes[0].border_box.size.width == 300,
          "Resize must relayout the root at the new viewport");
    check(before.boxes[1].border_box.size.width == 176 && resized.boxes[1].border_box.size.width == 276,
          "Stretched buttons must follow the new viewport");
    check(before.boxes[2].parent == 1 && before.boxes[2].padding == tessera::Edges{},
          "Box observations must keep parent indices and edges");
    check(before.paint.commands.size() == 5 && before.paint != resized.paint,
          "Each generation must own its full paint list");

    const std::vector<tessera::ReplayAction> expected{
        {1, 0, "activate", "start-game", 1, "start"},
        {7, 1, "activate", "quit-game", 3, "quit"},
        {12, 2, "activate", "start-game", 1, "resume"},
    };
    check(output.actions == expected, "Replay must preserve action order, steps, generations, and identities");
    check(output.semantics.size() == 3 && !output.semantics[0].step && output.semantics[1].step == 5u &&
              output.semantics[2].generation == 2 && output.semantics[2].nodes.size() == 2 &&
              output.semantics[2].nodes[1].name == "Resume",
          "Each generation must be projected once when no command runs");
    check(output == play(recording), "Repeated playback must be identical");
    check(tessera::compare_replay(output, play(recording)).empty(), "Identical playback must compare clean");
}

// A scroll step routes against the current snapshot and settles a generation at the new offset; a click
// then targets the moved button.
void scroll_steps_relayout_before_clicks() {
    auto scrolled = recording();
    scrolled.document = menu({button("start", "Start", "start-game"), button("options", "Options", "start-game"),
                              button("quit", "Quit", "quit-game")});
    scrolled.styles = menu_styles(3);
    scrolled.styles[0].overflow = tessera::Overflow::scroll;
    scrolled.viewport = {200, 60};
    scrolled.steps = {tessera::InputEvent{1us, tessera::Scroll{{100, 30}, {0, 1000}}}};
    const auto first = play(scrolled);
    check(first.generations.size() == 2 && first.generations[1].step == 0u, "A scroll step must settle a generation");
    const auto& before = first.generations[0].boxes;
    const auto& after = first.generations[1].boxes;
    check(before[0].scroll && before[0].scroll->offset == tessera::Point{} && after[0].scroll &&
              after[0].scroll->offset.y > 0 && after[0].scroll->offset.y == after[0].scroll->extent.height - 60,
          "The routed offset must clamp at the root's limit");
    check(after[1].border_box.origin.y == before[1].border_box.origin.y - after[0].scroll->offset.y,
          "Children must move by the routed offset");

    // Preorder: menu 0, start 1, options 3, quit 5.
    const auto quit = center(after.at(5));
    scrolled.steps.push_back(tessera::InputEvent{2us, tessera::PointerDown{{1}, quit}});
    scrolled.steps.push_back(tessera::InputEvent{3us, tessera::PointerUp{{1}, quit}});
    const auto output = play(scrolled);
    check(output.actions == std::vector<tessera::ReplayAction>{{2, 1, "activate", "quit-game", 5, "quit"}},
          "A click after scrolling must target the moved button");
    check(output == play(scrolled), "Repeated scroll playback must be identical");

    auto expected = output;
    expected.generations[1].boxes[0].scroll->offset.y -= 1;
    const auto diagnostics = tessera::compare_replay(expected, output, 0.5f);
    check(diagnostics.size() == 1 && has(diagnostics, "replay_mismatch", "/generations/1/boxes/0/scroll"),
          "Scroll geometry must be compared within the geometry tolerance");
    check(tessera::compare_replay(expected, output, 1).empty(), "Scroll tolerance must accept the difference");

    auto bad = scrolled;
    bad.steps = {tessera::InputEvent{2us, tessera::Scroll{{100, 30}, {0, 5}}},
                 tessera::InputEvent{1us, tessera::PointerMove{{1}, {1, 1}}}};
    check(has(play_invalid(bad).diagnostics, "event_order", "/steps/1/timestamp"),
          "Pointer steps must not run backwards after a scroll step");
    bad.steps = {tessera::InputEvent{2us, tessera::PointerMove{{1}, {1, 1}}},
                 tessera::InputEvent{1us, tessera::Scroll{{100, 30}, {0, 5}}}};
    check(has(play_invalid(bad).diagnostics, "event_order", "/steps/1/timestamp"),
          "Scroll steps must share the pointer timestamp stream");
    bad.steps = {tessera::InputEvent{1us, tessera::Scroll{{100, 30}, {0, std::numeric_limits<float>::infinity()}}}};
    check(has(play_invalid(bad).diagnostics, "invalid_number", "/steps/0/delta/y"), "Scroll deltas must be validated");
}

// Commands move focus and activate through focus dispatch, request what clicks request, and are observed
// in semantic projections; settled generations recover focus.
void logical_commands_match_clicks_and_semantics() {
    auto keyboard = recording();
    keyboard.document = menu({focusable(button("start", "Start", "start-game")),
                              focusable(button("quit", "Quit", "quit-game"))});
    const auto initial = play(keyboard);
    // Preorder: menu 0, start 1, start-label 2, quit 3, quit-label 4.
    const auto start = center(initial.generations[0].boxes.at(1));
    const auto quit = center(initial.generations[0].boxes.at(3));
    std::chrono::microseconds time{0};
    const auto at = [&](auto data) { return tessera::InputEvent{time += 1us, std::move(data)}; };
    auto clicks = keyboard;
    clicks.steps = {at(tessera::PointerDown{{1}, quit}), at(tessera::PointerUp{{1}, quit}),
                    at(tessera::PointerDown{{1}, start}), at(tessera::PointerUp{{1}, start})};
    auto& steps = keyboard.steps;
    steps.push_back(at(tessera::FocusNext{}));                                  // 0: start
    steps.push_back(at(tessera::Navigate{tessera::Direction::down}));           // 1: quit
    steps.push_back(at(tessera::Activate{}));                                   // 2: quit-game
    steps.push_back(at(tessera::FocusNext{}));                                  // 3: wraps to start
    steps.push_back(at(tessera::Activate{}));                                   // 4: start-game
    steps.push_back(at(tessera::Cancel{}));                                     // 5: no binding
    steps.push_back(at(tessera::PointerDown{{1}, quit}));                       // 6
    steps.push_back(at(tessera::PointerUp{{1}, quit}));                         // 7: quit-game, focus stays
    steps.push_back(at(tessera::Navigate{tessera::Direction::down}));           // 8: quit
    steps.push_back(tessera::ReplayReload{                                      // 9: restored by ID
        menu({focusable(button("resume", "Resume", "start-game")), focusable(button("quit", "Quit", "quit-game"))}),
        menu_styles(2)});
    steps.push_back(tessera::ReplayReload{                                      // 10: quit disabled
        menu({focusable(button("resume", "Resume", "start-game")),
              focusable(button("quit", "Quit", "quit-game"), true)}),
        menu_styles(2)});
    steps.push_back(at(tessera::Activate{}));                                   // 11: start-game from resume
    const auto output = play(keyboard);

    const auto pointer = play(clicks).actions;
    check(output.actions.size() == 4 && pointer.size() == 2, "Commands and clicks must request one action each");
    for (std::size_t i = 0; i < pointer.size(); ++i) {
        const auto& command = output.actions[i];
        check(command.binding == pointer[i].binding && command.action == pointer[i].action &&
                  command.node == pointer[i].node && command.id == pointer[i].id,
              "Commands must request exactly what clicks on the same buttons request");
    }
    const std::vector<tessera::ReplayAction> expected{
        {2, 0, "activate", "quit-game", 3, "quit"},
        {4, 0, "activate", "start-game", 1, "start"},
        {7, 0, "activate", "quit-game", 3, "quit"},
        {11, 2, "activate", "start-game", 1, "resume"},
    };
    check(output.actions == expected, "Command actions must keep their steps, generations, and identities");

    // Initial, six commands, the Navigate at step 8, two reload generations, and the final Activate.
    const auto& semantics = output.semantics;
    check(semantics.size() == 11, "Semantics must be observed per generation and per command step");
    const std::vector<std::optional<std::size_t>> observed{std::nullopt, 0u, 1u, 2u, 3u, 4u, 5u, 8u, 9u, 10u, 11u};
    const std::vector<std::optional<std::uint32_t>> focus{std::nullopt, 1u, 3u, 3u, 1u, 1u, 1u, 3u, 3u, 1u, 1u};
    for (std::size_t i = 0; i < semantics.size(); ++i) {
        check(semantics[i].step == observed[i] && semantics[i].focused == focus[i],
              "Each observation must name its step and the focus after it");
        std::size_t focused = 0;
        for (const auto& entry : semantics[i].nodes) focused += entry.focused;
        check(focused == (focus[i] ? 1u : 0u), "Exactly the focused eligible entry must report focus");
    }
    const auto& first = semantics[0].nodes;
    check(first.size() == 3 && first[0].role == tessera::SemanticRole::root && first[0].id == "menu" &&
              first[1].node == 1 && first[1].role == tessera::SemanticRole::button && first[1].name == "Start" &&
              first[1].name_source == tessera::NameSource::content && first[1].parent == 0 && first[1].focusable &&
              first[1].actions == std::vector<tessera::SemanticAction>{{"activate", "start-game"}} &&
              first[2].node == 3 && first[2].id == "quit",
          "Entries must own preorder indices, roles, names, state, and actions");
    check(semantics[1].nodes[1].focused && !semantics[1].nodes[2].focused, "FocusNext must focus Start");
    check(semantics[8].generation == 1 && semantics[8].nodes[2].focused && semantics[8].nodes[1].name == "Resume",
          "A reload must restore focus by author ID");
    const auto& disabled = semantics[9].nodes[2];
    check(semantics[9].generation == 2 && !disabled.enabled && !disabled.focusable && !disabled.focused &&
              disabled.actions.empty() && semantics[9].nodes[1].focused,
          "Focus on a disabled button must recover to an eligible one, which is projected");
    check(output == play(keyboard), "Repeated command playback must be identical");

    auto bad = keyboard;
    bad.steps = {tessera::InputEvent{2us, tessera::PointerMove{{1}, {1, 1}}}, tessera::InputEvent{1us, tessera::FocusNext{}}};
    check(has(play_invalid(bad).diagnostics, "event_order", "/steps/1/timestamp"),
          "Command steps must share the pointer timestamp stream");
    bad.steps = {tessera::InputEvent{2us, tessera::FocusNext{}}, tessera::InputEvent{1us, tessera::Scroll{{1, 1}, {0, 1}}}};
    check(has(play_invalid(bad).diagnostics, "event_order", "/steps/1/timestamp"),
          "Scroll steps must not run backwards after a command step");

    // Semantic warnings are located per observation and returned with the output.
    auto unnamed = keyboard;
    unnamed.document.root.children[0].children.clear();
    unnamed.styles = menu_styles(2);
    unnamed.styles.erase(unnamed.styles.begin() + 2);
    unnamed.steps = {tessera::InputEvent{1us, tessera::FocusNext{}}};
    tessera::PlaceholderTextShaper text;
    const auto warned = tessera::play_replay(unnamed, text);
    check(warned && warned.diagnostics.size() == 2, "Semantic warnings must accompany the output");
    for (const auto& [diagnostic, path] : {std::pair{warned.diagnostics[0], "/nodes/1"},
                                           std::pair{warned.diagnostics[1], "/steps/0/nodes/1"}}) {
        check(diagnostic.code == "missing_name" && diagnostic.severity == tessera::Severity::warning &&
                  diagnostic.path == path,
              "Semantic warnings must be located under their observation");
    }
}

void comparison_locates_differences() {
    const auto actual = play(interaction());
    auto expected = actual;
    expected.generations[1].boxes[1].border_box.size.width += 0.25f;
    auto diagnostics = tessera::compare_replay(expected, actual);
    check(diagnostics.size() == 1 && has(diagnostics, "replay_mismatch", "/generations/1/boxes/1/border_box"),
          "Exact comparison must locate a geometry difference");
    check(tessera::compare_replay(expected, actual, 0.25f).empty(), "Declared tolerance must accept the difference");
    check(!tessera::compare_replay(expected, actual, 0.125f).empty(), "Smaller tolerance must reject the difference");
    expected = actual;
    expected.generations[0].boxes[1].clip = tessera::Rect{};
    diagnostics = tessera::compare_replay(expected, actual, 1);
    check(diagnostics.size() == 1 && has(diagnostics, "replay_mismatch", "/generations/0/boxes/1/clip"),
          "Clip presence must be compared");

    expected = actual;
    expected.generations[0].boxes[4].visible = false;
    expected.generations[2].step = 8;
    std::get<tessera::DrawRect>(expected.generations[0].paint.commands[0]).color.a = 0.5f;
    expected.actions[1].action = "start-game";
    expected.actions.pop_back();
    diagnostics = tessera::compare_replay(expected, actual, 1);
    check(diagnostics.size() == 5 && has(diagnostics, "replay_mismatch", "/generations/0/boxes/4/visible") &&
              has(diagnostics, "replay_mismatch", "/generations/2/step") &&
              has(diagnostics, "replay_mismatch", "/generations/0/paint/commands/0") &&
              has(diagnostics, "replay_mismatch", "/actions") && has(diagnostics, "replay_mismatch", "/actions/1"),
          "Tolerance must not hide non-geometry differences");

    expected = actual;
    expected.generations.pop_back();
    expected.generations[0].paint.commands.pop_back();
    diagnostics = tessera::compare_replay(expected, actual);
    check(diagnostics.size() == 2 && has(diagnostics, "replay_mismatch", "/generations") &&
              has(diagnostics, "replay_mismatch", "/generations/0/paint/commands"),
          "Count differences must be located");

    expected = actual;
    expected.semantics[1].focused = 1;
    expected.semantics[2].nodes[1].name = "Continue";
    expected.semantics[0].nodes.pop_back();
    diagnostics = tessera::compare_replay(expected, actual, 1);
    check(diagnostics.size() == 3 && has(diagnostics, "replay_mismatch", "/semantics/1/focused") &&
              has(diagnostics, "replay_mismatch", "/semantics/2/nodes/1") &&
              has(diagnostics, "replay_mismatch", "/semantics/0/nodes"),
          "Semantic differences must be located without geometry tolerance");
    expected = actual;
    expected.semantics.pop_back();
    diagnostics = tessera::compare_replay(expected, actual);
    check(diagnostics.size() == 1 && has(diagnostics, "replay_mismatch", "/semantics"),
          "Semantic observation counts must be compared");

    diagnostics = tessera::compare_replay(actual, actual, -1);
    check(diagnostics.size() == 1 && has(diagnostics, "out_of_range", "/geometry_tolerance"), "Negative tolerance accepted");
    diagnostics = tessera::compare_replay(actual, actual, std::numeric_limits<float>::quiet_NaN());
    check(diagnostics.size() == 1 && has(diagnostics, "invalid_number", "/geometry_tolerance"), "NaN tolerance accepted");
}

void invalid_recordings_are_located_without_output() {
    auto bad = recording();
    bad.version = 1;
    check(has(play_invalid(bad).diagnostics, "unsupported_version", "/version"), "Replay version must be checked");

    bad = recording();
    bad.steps.assign(tessera::max_replay_steps + 1, tessera::ReplayResize{{200, 120}});
    check(has(play_invalid(bad).diagnostics, "out_of_range", "/steps"), "Step bound must be enforced");

    bad = recording();
    bad.document.version = 2;
    check(has(play_invalid(bad).diagnostics, "unsupported_version", "/document/version"),
          "Initial document diagnostics must be relocated");

    bad = recording();
    bad.styles.pop_back();
    check(has(play_invalid(bad).diagnostics, "style_count", "/styles"), "Initial style count must be checked");

    bad = recording();
    bad.viewport.width = -1;
    check(has(play_invalid(bad).diagnostics, "out_of_range", "/viewport/width"), "Initial viewport must be checked");

    bad = recording();
    bad.steps = {tessera::InputEvent{1us, tessera::KeyDown{}}};
    check(has(play_invalid(bad).diagnostics, "unsupported_event", "/steps/0/event"),
          "Keys must be rejected; hosts translate them into commands");

    bad = recording();
    bad.steps = {tessera::InputEvent{2us, tessera::PointerMove{{1}, {1, 1}}},
                 tessera::ReplayResize{{100, 100}},
                 tessera::InputEvent{1us, tessera::PointerMove{{1}, {1, 1}}}};
    check(has(play_invalid(bad).diagnostics, "event_order", "/steps/2/timestamp"),
          "The timestamp stream must survive resize steps");

    bad = recording();
    bad.steps = {tessera::ReplayResize{{-1, 100}}};
    check(has(play_invalid(bad).diagnostics, "out_of_range", "/steps/0/viewport/width"), "Resize must be checked");

    bad = recording();
    bad.steps = {tessera::ReplayReload{recording().document, menu_styles(1)}};
    check(has(play_invalid(bad).diagnostics, "style_count", "/steps/0/styles"), "Reload style count must be checked");

    bad = recording();
    auto document = recording().document;
    document.version = 2;
    bad.steps = {tessera::ReplayReload{document, menu_styles(2)}};
    check(has(play_invalid(bad).diagnostics, "unsupported_version", "/steps/0/document/version"),
          "Reload document diagnostics must be relocated");

    bad = recording();
    bad.context.actions.erase("quit-game");
    check(!play_invalid(bad).value, "Undeclared actions must reject the recording");
}

} // namespace

int main() {
    try {
        playback_observes_geometry_paint_and_actions();
        scroll_steps_relayout_before_clicks();
        logical_commands_match_clicks_and_semantics();
        comparison_locates_differences();
        invalid_recordings_are_located_without_output();
        std::cout << "Replay playback, command, semantic, comparison, and rejection checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
