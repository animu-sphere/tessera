#include <tessera/replay/replay_serialization.hpp>
#include "../../src/detail/sha256.hpp"
#include "../check.hpp"
#include <cmath>
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
    result.properties["focusable"] = true;
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

// Every step kind, target form, enumeration, and non-numeric style keyword.
tessera::ReplayRecording every_step() {
    auto result = recording();
    result.policy = {true, true};
    result.environment = {
        2.5f, "ja-JP", {tessera::ReplayText::Kind::font_profile, "menu fonts/日本語", std::string(64, 'a')}};
    result.document.extensions["path-finder:editor"] = tessera::JsonValue{std::string("kept")};
    auto& style = result.styles[2];
    style.display = tessera::Display::none;
    style.direction = tessera::FlexDirection::row;
    style.justify = tessera::Justify::space_between;
    style.overflow = tessera::Overflow::scroll;
    style.width = tessera::Dimension::points(0.1f);
    style.max_height = 48;
    style.margin = {1, 2, 3, 4};
    style.border = {0.5f, 0, 0.25f, 0};
    style.grow = 1;
    style.shrink = 1.0f / 3;
    style.visibility = tessera::Visibility::hidden;
    style.opacity = 0.7f;
    style.border_color = {1, 0, 0, 0.5f};
    style.corner_radius = 1e-30f;
    style.text = {{7}, 3.4e38f, 20.5f, 700};
    style.color = {0.1f, 0.2f, 0.3f, 1};
    const tessera::Modifiers keys{true, false, true, false};
    auto& steps = result.steps;
    steps.push_back(tessera::InputEvent{1us, tessera::PointerMove{{4}, {10.5f, -2}, keys}});
    steps.push_back(
        tessera::InputEvent{2us, tessera::PointerDown{{4}, {10, 20}, tessera::PointerButton::secondary, keys}});
    steps.push_back(tessera::InputEvent{3us, tessera::PointerUp{{4}, {10, 20}, tessera::PointerButton::middle, {}}});
    steps.push_back(tessera::InputEvent{4us, tessera::PointerCancel{{4294967295u}}});
    steps.push_back(tessera::InputEvent{5us, tessera::Scroll{{1, 2}, {0, -30}, keys}});
    steps.push_back(tessera::InputEvent{6us, tessera::Navigate{tessera::Direction::left, true}});
    steps.push_back(tessera::InputEvent{7us, tessera::FocusNext{}});
    steps.push_back(tessera::InputEvent{8us, tessera::FocusPrevious{}});
    steps.push_back(tessera::InputEvent{9us, tessera::Activate{}});
    steps.push_back(tessera::InputEvent{9007199254740991us, tessera::Cancel{}});
    steps.push_back(tessera::ReplayResize{{300, 160.25f}});
    steps.push_back(tessera::ReplayReload{menu({button("resume", "Resume", "start-game")}), menu_styles(1)});
    steps.push_back(tessera::ReplayFocus{tessera::AuthorIdTarget{"resume"}});
    steps.push_back(tessera::ReplayFocus{tessera::SemanticTarget{tessera::SemanticRole::text, "Resume"}});
    steps.push_back(tessera::ReplayFocus{tessera::PathTarget{{0, 0}}});
    steps.push_back(tessera::ReplayFocus{tessera::PathTarget{}});
    steps.push_back(tessera::ReplaySemanticAction{tessera::PointTarget{{5, 6}}, "cancel"});
    steps.push_back(tessera::ReplaySemanticAction{tessera::SemanticTarget{tessera::SemanticRole::button, "Resume"}});
    steps.push_back(tessera::ReplayScale{1.5f});
    steps.push_back(tessera::ReplayTick{9007199254740991us});
    return result;
}

std::string save(const tessera::ReplayRecording& recording) {
    auto saved = tessera::save_replay(recording);
    check(saved && saved.diagnostics.empty(), "Valid replay must save");
    return std::move(*saved.value);
}

tessera::ReplayRecording load(std::string_view json) {
    auto loaded = tessera::load_replay(json);
    check(loaded && loaded.diagnostics.empty(), "Valid replay JSON must load");
    return std::move(*loaded.value);
}

tessera::ReplayOutput play(const tessera::ReplayRecording& recording) {
    tessera::PlaceholderTextShaper text;
    auto result = tessera::play_replay(recording, text);
    check(result && result.diagnostics.empty(), "Valid replay rejected");
    return std::move(*result.value);
}

std::string changed(std::string json, std::string_view from, std::string_view to) {
    const auto at = json.find(from);
    check(at != std::string::npos, "Fixture text must contain the replaced fragment");
    return json.replace(at, from.size(), to);
}

void round_trip_preserves_every_field() {
    const auto original = every_step();
    const auto json = save(original);
    check(!json.empty() && json.back() == '\n' && json.find('\n') == json.size() - 1, "Canonical replay is one line");
    for (const auto* fragment :
         {"\"version\":1", "\"locale\":\"ja-JP\"", "\"scale\":2.5", "\"max_width\":\"none\"",
          "\"line_height\":\"normal\"", "\"width\":\"auto\"", "\"width\":0.1", "\"opacity\":0.7",
          "\"line_height\":20.5", "\"kind\":\"font_profile\"", "\"type\":\"pointer_cancel\"",
          "\"pointer\":4.294967295e+09", "\"timestamp\":9.007199254740991e+15", "\"children\":[]"})
        check(json.find(fragment) != std::string::npos, std::string("Canonical replay must contain ") + fragment);
    const auto loaded = load(json);
    check(loaded == original, "Load must restore every saved field exactly");
    check(save(loaded) == json, "Re-saving a loaded replay must be byte-stable");

    // Spelling is normalized; values are not.
    auto spelled = changed(json, "\"scale\":2.5", "\"scale\" : 25e-1");
    spelled = changed(spelled, "\"locale\":\"ja-JP\"", "\"locale\":\"ja\\u002dJP\"");
    check(load(spelled) == original && save(load(spelled)) == json,
          "Whitespace, escapes, and number notation normalize");

    auto placeholder = original;
    placeholder.environment.text = {};
    const auto plain = save(placeholder);
    check(plain.find("\"text\":{\"kind\":\"placeholder\"}") != std::string::npos, "Placeholder text has no identity");
    check(load(plain) == placeholder, "Placeholder text must round trip");
}

void loaded_recordings_replay_identically() {
    auto source = recording();
    source.policy.reveal_focus = true;
    source.environment.locale = "und";
    auto& steps = source.steps;
    steps.push_back(tessera::InputEvent{1us, tessera::FocusNext{}});
    steps.push_back(tessera::InputEvent{2us, tessera::Navigate{tessera::Direction::down}});
    steps.push_back(tessera::InputEvent{3us, tessera::Activate{}});
    steps.push_back(tessera::ReplayResize{{240, 100}});
    steps.push_back(tessera::InputEvent{4us, tessera::PointerDown{{1}, {100, 30}}});
    steps.push_back(tessera::InputEvent{5us, tessera::PointerUp{{1}, {100, 30}}});
    steps.push_back(tessera::ReplayFocus{tessera::AuthorIdTarget{"start"}});
    steps.push_back(tessera::ReplaySemanticAction{tessera::SemanticTarget{tessera::SemanticRole::button, "Quit"}});
    const auto expected = play(source);
    check(expected.actions.size() == 3, "Fixture must request command, pointer, and semantic actions");
    const auto restored = load(save(source));
    check(restored == source && play(restored) == expected, "A restored recording must reproduce playback");

    // A session's accepted steps serialize and replay to its observations.
    tessera::PlaceholderTextShaper text;
    auto session = tessera::ReplaySession::open(recording(), text);
    check(session && session.value->apply(tessera::InputEvent{1us, tessera::FocusNext{}}), "Session must accept steps");
    auto captured = session.value->capture();
    check(captured.value.has_value(), "Session must capture");
    auto quit = tessera::resolve_target(*captured.value, tessera::AuthorIdTarget{"quit"});
    check(quit && session.value->invoke(*quit.value), "Session must invoke by handle");
    const auto session_json = save(session.value->recording());
    check(session_json.find("\"kind\":\"id\",\"id\":\"quit\"") == std::string::npos &&
              session_json.find("{\"id\":\"quit\",\"kind\":\"id\"}") != std::string::npos,
          "Handle steps must be saved as author-ID targets with sorted keys");
    check(play(load(session_json)) == session.value->output(), "A saved session must replay its output");
}

void stack_styles_round_trip() {
    auto source = recording();
    source.styles[0].display = tessera::Display::stack;
    source.styles[1].stack_horizontal = tessera::Align::end;
    source.styles[1].stack_vertical = tessera::Align::start;
    source.styles[3].stack_horizontal = tessera::Align::center;
    source.styles[3].stack_vertical = tessera::Align::end;
    source.steps.push_back(tessera::ReplayReload{source.document, source.styles});
    source.steps.push_back(tessera::InputEvent{1us, tessera::FocusNext{}});
    source.steps.push_back(tessera::InputEvent{2us, tessera::Activate{}});
    const auto json = save(source);
    check(load(json) == source && save(load(json)) == json,
          "Stack display and each child's placement must survive initial/reload round trips");
    const auto original = play(source);
    check(tessera::compare_replay(original, play(load(json))).empty(),
          "Restored Stack playback must reproduce layout, paint, semantics and actions");
    auto invalid = tessera::load_replay(changed(json, "\"stack_horizontal\":\"end\"", "\"stack_horizontal\":\"bad\""));
    check(!invalid && has(invalid.diagnostics, "unknown_value", "/steps/0/styles/1/stack_horizontal"),
          "Unknown encoded Stack placement must report its field");
    const auto legacy = save(recording());
    check(legacy.find("stack_horizontal") == std::string::npos && legacy.find("stack_vertical") == std::string::npos &&
              load(legacy) == recording(),
          "Legacy recordings must retain their default placement and canonical encoding");
    auto host = source;
    host.version = tessera::replay_host_version;
    check(load(save(host)) == host, "Stack placement must also survive the host-state recording version");
}

void grid_styles_round_trip() {
    auto source = recording();
    auto& grid = source.styles[0];
    grid.display = tessera::Display::grid;
    grid.grid_columns = {tessera::GridTrack::points(50), tessera::GridTrack::fraction(2, 20, 140)};
    grid.grid_rows = {tessera::GridTrack{}, tessera::GridTrack::fraction()};
    grid.column_gap = 6;
    grid.row_gap = 4;
    source.styles[1].grid_horizontal = tessera::Align::end;
    source.styles[1].grid_vertical = tessera::Align::center;
    source.steps.push_back(tessera::ReplayReload{source.document, source.styles});
    source.steps.push_back(tessera::ReplayResize{{160, 100}});
    source.steps.push_back(tessera::InputEvent{1us, tessera::FocusNext{}});
    source.steps.push_back(tessera::InputEvent{2us, tessera::Activate{}});
    const auto json = save(source);
    check(load(json) == source && save(load(json)) == json,
          "Grid tracks, limits, gaps and alignment must round-trip in initial/reloaded views");
    check(tessera::compare_replay(play(source), play(load(json))).empty(),
          "Restored Grid must preserve geometry, paint, semantics and actions after resizing");
    auto invalid = tessera::load_replay(changed(json, "\"kind\":\"fraction\"", "\"kind\":\"bad\""));
    check(!invalid && has(invalid.diagnostics, "unknown_value", "/steps/0/styles/0/grid_columns/1/kind"),
          "Unknown Grid track kinds must locate their encoded field");
    invalid = tessera::load_replay(changed(json, "\"column_gap\":6", "\"column_gap\":-1"));
    check(!invalid && has(invalid.diagnostics, "out_of_range", "/steps/0/styles/0/column_gap"),
          "Malformed Grid gaps must fail before playback");
    source.version = tessera::replay_host_version;
    check(load(save(source)) == source, "Grid must round-trip under host-state replay v2");
    const auto legacy = save(recording());
    check(legacy.find("grid_") == std::string::npos && legacy.find("column_gap") == std::string::npos &&
              load(legacy) == recording(),
          "Default Grid fields must not change legacy canonical recordings");
}

void gradient_styles_round_trip() {
    auto source = recording();
    source.styles[0].background_gradient = tessera::LinearGradient{{.25f, 0}, {.75f, 1}, {1, 0, 0, 0}, {0, 0, 1, .75f}};
    source.steps.push_back(tessera::ReplayReload{source.document, source.styles});
    source.steps.push_back(tessera::ReplayResize{{160, 100}});
    const auto json = save(source);
    check(load(json) == source && save(load(json)) == json,
          "Gradient endpoints and colors must survive initial and reloaded style encoding");
    check(tessera::compare_replay(play(source), play(load(json))).empty(),
          "Gradient replay must reproduce ordinary geometry, paint and semantics after resize");
    const auto invalid = tessera::load_replay(changed(json, "\"x\":0.75", "\"x\":2"));
    check(!invalid && has(invalid.diagnostics, "out_of_range", "/steps/0/styles/0/background_gradient/end/x"),
          "Invalid encoded gradient must fail with its located field before playback");
    source.version = tessera::replay_host_version;
    check(load(save(source)) == source, "Gradient styles must also round-trip in host-state replay v2");
    check(json.find("\"stops\"") == std::string::npos, "Shorthand gradients must retain their canonical encoding");
    std::get<tessera::LinearGradient>(*source.styles[0].background_gradient).stops = {
        {.1f, {1, 0, 0, 0}}, {.5f, {0, 1, 0, .5f}}, {.5f, {0, 0, 1, .75f}}, {.9f, {1, 1, 1, 1}}};
    source.steps[0] = tessera::ReplayReload{source.document, source.styles};
    for (const auto version : {tessera::replay_version, tessera::replay_host_version}) {
        source.version = version;
        const auto multi = save(source);
        check(load(multi) == source && save(load(multi)) == multi &&
                  tessera::compare_replay(play(source), play(load(multi))).empty(),
              "Explicit stop order, offsets and alpha must round-trip in initial/reloaded styles in v1 and v2");
        auto rejected = tessera::load_replay(changed(multi, "\"offset\":0.5", "\"offset\":-1"));
        check(!rejected &&
                  has(rejected.diagnostics, "out_of_range", "/steps/0/styles/0/background_gradient/stops/1/offset"),
              "Malformed stop offsets must reject before playback with a located path");
        rejected = tessera::load_replay(changed(multi, "\"offset\":0.5", "\"offset\":0"));
        check(!rejected && has(rejected.diagnostics, "invalid_gradient_stops",
                               "/steps/0/styles/0/background_gradient/stops/1/offset"),
              "Replay must reject unsorted stops, not normalize their order");
        rejected = tessera::load_replay(changed(multi, "\"offset\":0.5", "\"unknown\":0.5"));
        check(!rejected &&
                  has(rejected.diagnostics, "unknown_field", "/steps/0/styles/0/background_gradient/stops/1/unknown"),
              "Stop schema must reject unknown fields");
    }
    const auto legacy = save(recording());
    check(legacy.find("background_gradient") == std::string::npos && load(legacy) == recording(),
          "Absent gradients must not change legacy canonical recording bytes");
    source.styles[0].background_gradient = tessera::RadialGradient{
        .center = {.25f, .75f},
        .radius = {.8f, 1.25f},
        .stops = {{.1f, {1, 0, 0, 0}}, {.5f, {0, 1, 0, .5f}}, {.5f, {0, 0, 1, .75f}}, {.9f, {1, 1, 1, 1}}}};
    source.steps[0] = tessera::ReplayReload{source.document, source.styles};
    for (const auto version : {tessera::replay_version, tessera::replay_host_version}) {
        source.version = version;
        const auto radial = save(source);
        check(load(radial) == source && save(load(radial)) == radial &&
                  tessera::compare_replay(play(source), play(load(radial))).empty(),
              "Radial geometry, stops and rendered observations must round-trip in initial/reloaded views in v1/v2");
        auto rejected = tessera::load_replay(changed(radial, "\"kind\":\"radial\"", "\"kind\":\"conic\""));
        check(!rejected && has(rejected.diagnostics, "unknown_value", "/steps/0/styles/0/background_gradient/kind"),
              "Unknown gradient kinds must reject explicitly");
        rejected = tessera::load_replay(changed(radial, "\"radius\":", "\"start\":"));
        check(!rejected && has(rejected.diagnostics, "unknown_field", "/steps/0/styles/0/background_gradient/start"),
              "Radial encoding must reject linear geometry rather than ignore it");
        rejected = tessera::load_replay(changed(radial, "\"height\":1.25", "\"height\":0"));
        check(!rejected &&
                  has(rejected.diagnostics, "out_of_range", "/steps/0/styles/0/background_gradient/radius/height"),
              "Invalid radial radii must retain their replay location");
    }
    std::get<tessera::RadialGradient>(*source.styles[0].background_gradient).stops.clear();
    source.steps[0] = tessera::ReplayReload{source.document, source.styles};
    check(load(save(source)) == source, "Radial two-color shorthand must round-trip");
}

void outline_styles_round_trip() {
    auto source = recording();
    source.styles[0].outline = tessera::Outline{1.5f, 2, {.2f, .4f, .8f, .75f}, 4, 2, -3};
    source.steps.push_back(tessera::ReplayReload{source.document, source.styles});
    for (auto version : {tessera::replay_version, tessera::replay_host_version}) {
        source.version = version;
        const auto json = save(source);
        check(load(json) == source && save(load(json)) == json &&
                  tessera::compare_replay(play(source), play(load(json))).empty(),
              "Outline styles must reproduce initial/reloaded paint and canonical encoding in Replay v1/v2");
        auto rejected = tessera::load_replay(changed(json, "\"dash_length\":4", "\"dash_length\":0"));
        check(!rejected && has(rejected.diagnostics, "invalid_dash", "/steps/0/styles/0/outline/dash_length"),
              "Invalid replay dash patterns must reject before playback");
        rejected = tessera::load_replay(changed(json, "\"dash_offset\":-3", "\"phase\":-3"));
        check(!rejected && has(rejected.diagnostics, "unknown_field", "/steps/0/styles/0/outline/phase"),
              "Replay must reject unknown outline fields instead of ignoring them");
    }
    check(save(recording()).find("\"outline\"") == std::string::npos,
          "Absent outlines must preserve legacy replay encoding");
}

void fill_shapes_round_trip() {
    auto source = recording();
    source.styles[0].background_shape = tessera::FillShape::ellipse;
    source.styles[0].background_antialias = true;
    source.steps.push_back(tessera::ReplayReload{source.document, source.styles});
    for (auto version : {tessera::replay_version, tessera::replay_host_version}) {
        source.version = version;
        const auto json = save(source);
        check(load(json) == source && save(load(json)) == json &&
                  tessera::compare_replay(play(source), play(load(json))).empty(),
              "Analytic fill must round-trip initial/reloaded style and pixels in Replay v1/v2");
        auto rejected =
            tessera::load_replay(changed(json, "\"background_shape\":\"ellipse\"", "\"background_shape\":\"blob\""));
        check(!rejected && has(rejected.diagnostics, "unknown_value", "/steps/0/styles/0/background_shape"),
              "Unknown replay shapes must reject before playback");
        rejected = tessera::load_replay(changed(json, "\"background_antialias\":true", "\"background_antialias\":1"));
        check(!rejected && has(rejected.diagnostics, "schema_type", "/steps/0/styles/0/background_antialias"),
              "Replay coverage must require a boolean");
    }
    const auto legacy = save(recording());
    check(legacy.find("\"background_shape\"") == std::string::npos &&
              legacy.find("\"background_antialias\"") == std::string::npos,
          "Default fill settings must preserve legacy Replay bytes");
}

void procedural_styles_round_trip() {
    auto source = recording();
    source.styles[0].background_procedural = tessera::ProceduralFill{
        "example.wave", {.25f, tessera::Point{1, -2}, tessera::Color{.1f, .2f, .3f, .4f}}, 1.5f, .75f};
    source.steps.push_back(tessera::ReplayReload{source.document, source.styles});
    for (auto version : {tessera::replay_version, tessera::replay_host_version}) {
        source.version = version;
        const auto json = save(source);
        check(load(json) == source && save(load(json)) == json &&
                  tessera::compare_replay(play(source), play(load(json))).empty(),
              "Procedural fills must round-trip identity, typed parameters, time and opacity in Replay v1/v2");
        auto rejected = tessera::load_replay(changed(json, "{\"number\":0.25}", "{\"angle\":0.25}"));
        check(!rejected && has(rejected.diagnostics, "unknown_field",
                               "/steps/0/styles/0/background_procedural/parameters/0/angle"),
              "Unknown parameter kinds must reject before playback");
        rejected = tessera::load_replay(changed(json, "\"shader\":\"example.wave\"", "\"shader\":\"\""));
        check(!rejected &&
                  has(rejected.diagnostics, "invalid_identifier", "/steps/0/styles/0/background_procedural/shader"),
              "Replay must validate procedural identities");
    }
    check(save(recording()).find("\"background_procedural\"") == std::string::npos,
          "Absent procedural fills must preserve legacy Replay bytes");
}

void shadow_styles_round_trip() {
    auto source = recording();
    source.styles[0].box_shadow = tessera::BoxShadow{{-2, 4}, 7, {.2f, .3f, .4f, .5f}};
    source.steps.push_back(tessera::ReplayReload{source.document, source.styles});
    source.steps.push_back(tessera::ReplayResize{{160, 100}});
    for (auto version : {tessera::replay_version, tessera::replay_host_version}) {
        source.version = version;
        const auto json = save(source);
        check(json.find("\"inset\"") == std::string::npos,
              "Outer shadows must retain their pre-inset canonical encoding");
        check(load(json) == source && save(load(json)) == json &&
                  tessera::compare_replay(play(source), play(load(json))).empty(),
              "Shadow styles must round-trip initial/reloaded views and reproduce paint in Replay v1/v2");
        auto rejected = tessera::load_replay(changed(json, "\"softness\":7", "\"softness\":-1"));
        check(!rejected && has(rejected.diagnostics, "out_of_range", "/steps/0/styles/0/box_shadow/softness"),
              "Invalid encoded shadows must reject before playback with located diagnostics");
        rejected = tessera::load_replay(changed(json, "\"softness\":7", "\"blur\":7"));
        check(!rejected && has(rejected.diagnostics, "unknown_field", "/steps/0/styles/0/box_shadow/blur"),
              "Shadow encoding must not silently accept unknown effect parameters");
        auto inset = source;
        inset.styles[0].box_shadow->inset = true;
        auto& reload = std::get<tessera::ReplayReload>(inset.steps[0]);
        reload.styles[0].box_shadow->inset = true;
        const auto inset_json = save(inset);
        check(load(inset_json) == inset && save(load(inset_json)) == inset_json &&
                  tessera::compare_replay(play(inset), play(load(inset_json))).empty(),
              "Inset styles and reloads must round-trip and reproduce drawing in Replay v1/v2");
        rejected = tessera::load_replay(changed(inset_json, "\"inset\":true", "\"inset\":1"));
        check(!rejected && has(rejected.diagnostics, "schema_type", "/steps/0/styles/0/box_shadow/inset"),
              "Encoded inset flags must reject nonboolean values");
        const auto explicit_outer = load(
            changed(changed(inset_json, "\"inset\":true", "\"inset\":false"), "\"inset\":true", "\"inset\":false"));
        check(explicit_outer == source && save(explicit_outer) == json,
              "Explicit false inset flags must normalize to legacy outer-shadow bytes");
        inset.styles[0].box_shadow->inset = false;
        check(load(save(inset)) == inset && tessera::compare_replay(play(inset), play(load(save(inset)))).empty(),
              "Replay must preserve replacement of an outer shadow by an inset shadow on reload");
    }
    const auto legacy = save(recording());
    check(legacy.find("box_shadow") == std::string::npos && load(legacy) == recording(),
          "Absent shadows must preserve legacy canonical Replay bytes");
}

void compatible_pointer_policy_round_trips() {
    for (const auto version : {tessera::replay_version, tessera::replay_host_version}) {
        auto source = recording();
        source.version = version;
        const auto legacy = save(source);
        check(legacy.find("compatible_pointer") == std::string::npos && load(legacy) == source,
              "Default pointer policy must not change legacy canonical JSON");
        source.policy.compatible_pointer = true;
        const auto point = play(source).generations[0].boxes[1].border_box.origin;
        source.steps = {tessera::InputEvent{1us, tessera::PointerDown{{1}, point}},
                        tessera::ReplayReload{source.document, source.styles},
                        tessera::InputEvent{2us, tessera::PointerUp{{1}, point}}};
        const auto json = save(source);
        check(json.find("\"compatible_pointer\":true") != std::string::npos && load(json) == source &&
                  save(load(json)) == json && play(load(json)) == play(source) && play(source).actions.size() == 1,
              "The opt-in must round-trip and reproduce replacement activation in replay v1 and v2");
        auto invalid = tessera::load_replay(changed(json, "\"compatible_pointer\":true", "\"compatible_pointer\":0"));
        check(!invalid && has(invalid.diagnostics, "schema_type", "/policy/compatible_pointer") &&
                  invalid.diagnostics[0].byte_offset.has_value(),
              "A malformed pointer policy needs a located diagnostic");
        const auto explicit_false = load(changed(json, "\"compatible_pointer\":true", "\"compatible_pointer\":false"));
        check(!explicit_false.policy.compatible_pointer && play(explicit_false).actions.empty() &&
                  save(explicit_false).find("compatible_pointer") == std::string::npos,
              "Explicit false must retain tree-identity behavior and canonical omission");
    }
}

void loading_rejects_malformed_input() {
    auto base_recording = recording();
    base_recording.steps.push_back(tessera::InputEvent{1us, tessera::FocusNext{}});
    base_recording.steps.push_back(tessera::ReplayFocus{tessera::AuthorIdTarget{"start"}});
    const auto base = save(base_recording);
    const auto reject = [&](const std::string& json, std::string_view code, std::string_view path) {
        const auto loaded = tessera::load_replay(json);
        check(!loaded && has(loaded.diagnostics, code, path),
              "Expected " + std::string(code) + " at " + std::string(path));
        for (const auto& diagnostic : loaded.diagnostics)
            check(diagnostic.byte_offset.has_value(), "Load diagnostics must carry a byte offset");
    };
    reject(changed(base, "\"version\":1,\"viewport\"", "\"version\":3,\"viewport\""), "unsupported_version",
           "/version");
    reject(changed(base, "\"version\":1,\"viewport\"", "\"version\":1.5,\"viewport\""), "schema_type", "/version");
    reject(changed(base, "\"actions\":[", "\"x/~\":0,\"actions\":["), "unknown_field", "/x~1~0");
    reject(changed(base, "\"actions\":[", "\"actions\":[],\"actions\":["), "duplicate_member", "/actions");
    reject(changed(base, "\"actions\":[\"quit-game\"", "\"actions\":[\"quit-game\",\"quit-game\""), "duplicate_action",
           "/actions/1");
    reject(changed(base, "\"actions\":[\"quit-game\"", "\"actions\":[\"\""), "invalid_identifier", "/actions/0");
    reject(changed(base, "\"policy\":{\"press_focus\":false,", "\"policy\":{"), "missing_field", "/policy/press_focus");
    reject(changed(base, "\"press_focus\":false", "\"press_focus\":0"), "schema_type", "/policy/press_focus");
    reject(changed(base, "\"scale\":1", "\"scale\":0"), "out_of_range", "/environment/scale");
    reject(changed(base, "\"scale\":1", "\"scale\":1e39"), "out_of_range", "/environment/scale");
    reject(changed(base, "\"locale\":\"und\"", "\"locale\":\"ja_JP\""), "invalid_locale", "/environment/locale");
    reject(changed(base, "\"locale\":\"und\"", "\"locale\":\"x-private\""), "invalid_locale", "/environment/locale");
    reject(changed(base, "{\"kind\":\"placeholder\"}", "{\"kind\":\"system\"}"), "unknown_value",
           "/environment/text/kind");
    reject(changed(base, "{\"kind\":\"placeholder\"}", "{\"id\":\"fonts\",\"kind\":\"placeholder\"}"), "unknown_field",
           "/environment/text/id");
    reject(changed(base, "{\"kind\":\"placeholder\"}", "{\"id\":\"fonts\",\"kind\":\"font_profile\"}"), "missing_field",
           "/environment/text/sha256");
    reject(changed(base, "{\"kind\":\"placeholder\"}",
                   "{\"id\":\"fonts\",\"kind\":\"font_profile\",\"sha256\":\"" + std::string(64, 'A') + "\"}"),
           "invalid_digest", "/environment/text/sha256");
    reject(changed(base, "{\"kind\":\"placeholder\"}",
                   "{\"id\":\"\",\"kind\":\"font_profile\",\"sha256\":\"" + std::string(64, 'a') + "\"}"),
           "invalid_identifier", "/environment/text/id");
    reject(changed(base, "\"viewport\":{\"height\":120,", "\"viewport\":{\"height\":-1,"), "out_of_range",
           "/viewport/height");
    reject(changed(base, "\"opacity\":1", "\"opacity\":2"), "out_of_range", "/styles/0/opacity");
    reject(changed(base, "\"display\":\"flex\"", "\"display\":\"table\""), "unknown_value", "/styles/0/display");
    reject(changed(base, "\"max_width\":\"none\"", "\"max_width\":\"auto\""), "unknown_value", "/styles/0/max_width");
    reject(changed(base, "\"width\":\"auto\"", "\"width\":null"), "schema_type", "/styles/0/width");
    reject(changed(base, "\"weight\":400", "\"weight\":70000"), "schema_type", "/styles/0/text/weight");
    reject(changed(base, "\"weight\":400", "\"weight\":0"), "out_of_range", "/styles/0/text/weight");
    reject(changed(base, "\"styles\":[{", "\"styles\":[{\"extra\":0,"), "unknown_field", "/styles/0/extra");
    reject(changed(base, "\"document\":{", "\"document\":{\"x\":0,"), "unknown_field", "/document/x");
    reject(changed(base, "\"version\":1},\"environment\"", "\"version\":2},\"environment\""), "unsupported_version",
           "/document/version");
    reject(changed(base, "\"events\":{\"activate\":\"quit-game\"}", "\"events\":{\"activate\":\"exit\"}"),
           "unknown_action", "/document/root/children/1/events/activate");
    reject(changed(base, "{\"timestamp\":1,\"type\":\"focus_next\"}",
                   "{\"key\":\"tab\",\"timestamp\":1,\"type\":\"key_down\"}"),
           "unsupported_event", "/steps/0/type");
    reject(changed(base, "\"type\":\"focus_next\"", "\"type\":\"hover\""), "unknown_value", "/steps/0/type");
    reject(changed(base, "{\"timestamp\":1,\"type\":\"focus_next\"}", "{\"type\":\"focus_next\"}"), "missing_field",
           "/steps/0/timestamp");
    reject(changed(base, "\"timestamp\":1,", "\"timestamp\":1.5,"), "schema_type", "/steps/0/timestamp");
    reject(changed(base, "\"timestamp\":1,", "\"timestamp\":-1,"), "out_of_range", "/steps/0/timestamp");
    reject(changed(base, "\"timestamp\":1,", "\"timestamp\":9007199254740992,"), "schema_type", "/steps/0/timestamp");
    reject(changed(base, "{\"id\":\"start\",\"kind\":\"id\"}", "{\"id\":\"start\",\"kind\":\"name\"}"), "unknown_value",
           "/steps/1/target/kind");
    reject(changed(base, "{\"id\":\"start\",\"kind\":\"id\"}", "{\"children\":[-1],\"kind\":\"path\"}"), "schema_type",
           "/steps/1/target/children/0");
    reject(changed(base, "{\"id\":\"start\",\"kind\":\"id\"}",
                   "{\"kind\":\"semantic\",\"name\":\"Start\",\"role\":\"link\"}"),
           "unknown_value", "/steps/1/target/role");
    reject(changed(base, "\"type\":\"focus\"}", "\"type\":\"focus\",\"binding\":\"activate\"}"), "unknown_field",
           "/steps/1/binding");

    const auto parse = [](const std::string& json, std::string_view code) {
        const auto loaded = tessera::load_replay(json);
        check(!loaded && loaded.diagnostics.size() == 1 && loaded.diagnostics[0].code == code,
              "Expected parser diagnostic " + std::string(code));
    };
    parse(base.substr(0, base.size() - 2), "json_syntax");
    parse(std::string(tessera::max_serialized_replay_bytes + 1, ' '), "size_limit");
    std::string values = "[";
    for (std::size_t i = 0; i < tessera::max_replay_json_values; ++i) values += "0,";
    values += "0]";
    parse(values, "json_value_limit");

    std::string many =
        "{\"actions\":[],\"document\":{\"root\":{\"type\":\"Box\"},\"version\":1},\"environment\":"
        "{\"locale\":\"und\",\"scale\":1,\"text\":{\"kind\":\"placeholder\"}},\"policy\":{\"press_focus\":"
        "false,\"reveal_focus\":false},\"steps\":[";
    for (std::size_t i = 0; i <= tessera::max_replay_steps; ++i)
        many += (i ? ",{" : "{") + std::string("\"timestamp\":0,\"type\":\"activate\"}");
    many += "],\"styles\":[],\"version\":1,\"viewport\":{\"height\":1,\"width\":1}}";
    reject(many, "out_of_range", "/steps");
}

void saving_rejects_invalid_recordings() {
    const auto reject = [](auto mutate, std::string_view code, std::string_view path) {
        auto value = recording();
        mutate(value);
        const auto saved = tessera::save_replay(value);
        check(!saved && has(saved.diagnostics, code, path),
              "Expected " + std::string(code) + " at " + std::string(path));
    };
    using R = tessera::ReplayRecording;
    reject([](R& r) { r.version = 0; }, "unsupported_version", "/version");
    reject([](R& r) { r.environment.scale = std::numeric_limits<float>::quiet_NaN(); }, "invalid_number",
           "/environment/scale");
    reject([](R& r) { r.environment.scale = -1; }, "out_of_range", "/environment/scale");
    reject([](R& r) { r.environment.locale = ""; }, "invalid_locale", "/environment/locale");
    reject([](R& r) { r.environment.locale = "en--US"; }, "invalid_locale", "/environment/locale");
    reject([](R& r) { r.environment.locale = "en-abcdefghi"; }, "invalid_locale", "/environment/locale");
    reject([](R& r) { r.environment.text.id = "fonts"; }, "invalid_text_service", "/environment/text");
    reject([](R& r) { r.environment.text.kind = static_cast<tessera::ReplayText::Kind>(9); }, "unknown_value",
           "/environment/text/kind");
    reject(
        [](R& r) {
            r.environment.text = {tessera::ReplayText::Kind::font_profile, std::string(257, 'x'), std::string(64, '0')};
        },
        "invalid_identifier", "/environment/text/id");
    reject([](R& r) { r.environment.text = {tessera::ReplayText::Kind::font_profile, "\xff", std::string(64, '0')}; },
           "invalid_utf8", "/environment/text/id");
    reject([](R& r) { r.environment.text = {tessera::ReplayText::Kind::font_profile, "fonts", std::string(63, '0')}; },
           "invalid_digest", "/environment/text/sha256");
    reject([](R& r) { r.context.actions.insert(""); }, "invalid_identifier", "/actions/0");
    reject([](R& r) { r.context.actions.erase("quit-game"); }, "unknown_action",
           "/document/root/children/1/events/activate");
    reject([](R& r) { r.viewport.width = std::numeric_limits<float>::infinity(); }, "invalid_number",
           "/viewport/width");
    reject([](R& r) { r.styles[1].opacity = -1; }, "out_of_range", "/styles/1/opacity");
    reject([](R& r) { r.styles[0].text.size = std::numeric_limits<float>::infinity(); }, "invalid_number",
           "/styles/0/text/size");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{1us, tessera::KeyDown{tessera::Key::tab}}); },
           "unsupported_event", "/steps/0/event");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{1us, tessera::TextInput{"a"}}); }, "unsupported_event",
           "/steps/0/event");
    reject(
        [](R& r) { r.steps.push_back(tessera::InputEvent{std::chrono::microseconds{1LL << 60}, tessera::Activate{}}); },
        "out_of_range", "/steps/0/timestamp");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{-1us, tessera::Activate{}}); }, "out_of_range",
           "/steps/0/timestamp");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{1us, tessera::PointerDown{{1}, {std::nanf(""), 0}}}); },
           "invalid_number", "/steps/0/position/x");
    reject([](R& r) { r.steps.push_back(tessera::ReplayResize{{-1, 1}}); }, "out_of_range", "/steps/0/viewport/width");
    reject(
        [](R& r) {
            auto styles = menu_styles(1);
            styles[1].gap = -1;
            r.steps.push_back(tessera::ReplayReload{menu({button("x", "X", "missing")}), styles});
        },
        "out_of_range", "/steps/0/styles/1/gap");
    reject([](R& r) { r.steps.push_back(tessera::ReplayReload{menu({button("x", "X", "missing")}), menu_styles(1)}); },
           "unknown_action", "/steps/0/document/root/children/0/events/activate");
    reject(
        [](R& r) {
            r.steps.push_back(tessera::ReplayFocus{tessera::PointTarget{{0, std::numeric_limits<float>::infinity()}}});
        },
        "invalid_number", "/steps/0/target/position/y");
    reject([](R& r) { r.steps.push_back(tessera::ReplayFocus{tessera::AuthorIdTarget{"\xc0"}}); }, "invalid_utf8",
           "/steps/0/target/id");
    reject([](R& r) { r.steps.push_back(tessera::ReplaySemanticAction{tessera::AuthorIdTarget{"start"}, "\xff"}); },
           "invalid_utf8", "/steps/0/binding");
    reject([](R& r) { r.steps.resize(tessera::max_replay_steps + 1, tessera::InputEvent{0us, tessera::Activate{}}); },
           "out_of_range", "/steps");
}

void playback_checks_the_environment() {
    tessera::PlaceholderTextShaper text;
    auto value = recording();
    value.environment.scale = 0;
    value.environment.locale = "日本";
    const auto played = tessera::play_replay(value, text);
    check(!played && has(played.diagnostics, "out_of_range", "/environment/scale") &&
              has(played.diagnostics, "invalid_locale", "/environment/locale"),
          "Playback must reject an invalid environment");
    const auto opened = tessera::ReplaySession::open(value, text);
    check(!opened && has(opened.diagnostics, "out_of_range", "/environment/scale"),
          "Sessions must check the environment");
    value = recording();
    value.environment = {1.25f, "en-US", {tessera::ReplayText::Kind::font_profile, "fonts", std::string(64, 'f')}};
    auto declared = play(value);
    check(declared.generations[0].device_scale == 1.25f, "Initial scale was not observed");
    declared.generations[0].device_scale = 1;
    check(declared == play(recording()), "Environment changed logical geometry or interaction");
    value = recording();
    value.steps = {tessera::ReplayScale{0}, tessera::ReplayTick{-1us}};
    check(has(tessera::save_replay(value).diagnostics, "out_of_range", "/steps/0/scale") &&
              has(tessera::save_replay(value).diagnostics, "out_of_range", "/steps/1/time"),
          "Invalid scale/tick saved");
    value.steps = {tessera::ReplayTick{9007199254740992us}};
    check(has(tessera::save_replay(value).diagnostics, "out_of_range", "/steps/0/time"),
          "Inexact animation time saved");
}

void sha256_matches_fips_vectors() {
    check(tessera::detail::sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "empty");
    check(tessera::detail::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "abc");
    check(tessera::detail::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "two-block message");
    check(tessera::detail::sha256_hex(std::string(1000000, 'a')) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "million a");
}

} // namespace

int main() {
    try {
        round_trip_preserves_every_field();
        loaded_recordings_replay_identically();
        stack_styles_round_trip();
        grid_styles_round_trip();
        gradient_styles_round_trip();
        shadow_styles_round_trip();
        outline_styles_round_trip();
        fill_shapes_round_trip();
        procedural_styles_round_trip();
        loading_rejects_malformed_input();
        compatible_pointer_policy_round_trips();
        saving_rejects_invalid_recordings();
        playback_checks_the_environment();
        sha256_matches_fips_vectors();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "replay serialization tests passed\n";
    return 0;
}
