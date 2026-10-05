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
    check(output == play(recording), "Repeated playback must be identical");
    check(tessera::compare_replay(output, play(recording)).empty(), "Identical playback must compare clean");
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
          "Non-pointer input must be rejected until focus dispatch exists");

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
        comparison_locates_differences();
        invalid_recordings_are_located_without_output();
        std::cout << "Replay playback, comparison, and rejection checks passed.\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
