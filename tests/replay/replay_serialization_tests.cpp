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
    result.environment = {2.5f, "ja-JP", {tessera::ReplayText::Kind::font_profile, "menu fonts/日本語",
                                          std::string(64, 'a')}};
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
    steps.push_back(tessera::InputEvent{2us, tessera::PointerDown{{4}, {10, 20}, tessera::PointerButton::secondary, keys}});
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
    for (const auto* fragment : {"\"version\":1", "\"locale\":\"ja-JP\"", "\"scale\":2.5", "\"max_width\":\"none\"",
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
    check(load(spelled) == original && save(load(spelled)) == json, "Whitespace, escapes, and number notation normalize");

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
    reject(changed(base, "\"version\":1,\"viewport\"", "\"version\":2,\"viewport\""), "unsupported_version", "/version");
    reject(changed(base, "\"version\":1,\"viewport\"", "\"version\":1.5,\"viewport\""), "schema_type", "/version");
    reject(changed(base, "\"actions\":[", "\"x/~\":0,\"actions\":["), "unknown_field", "/x~1~0");
    reject(changed(base, "\"actions\":[", "\"actions\":[],\"actions\":["), "duplicate_member", "/actions");
    reject(changed(base, "\"actions\":[\"quit-game\"", "\"actions\":[\"quit-game\",\"quit-game\""),
           "duplicate_action", "/actions/1");
    reject(changed(base, "\"actions\":[\"quit-game\"", "\"actions\":[\"\""), "invalid_identifier", "/actions/0");
    reject(changed(base, "\"policy\":{\"press_focus\":false,", "\"policy\":{"), "missing_field", "/policy/press_focus");
    reject(changed(base, "\"press_focus\":false", "\"press_focus\":0"), "schema_type", "/policy/press_focus");
    reject(changed(base, "\"scale\":1", "\"scale\":0"), "out_of_range", "/environment/scale");
    reject(changed(base, "\"scale\":1", "\"scale\":1e39"), "out_of_range", "/environment/scale");
    reject(changed(base, "\"locale\":\"und\"", "\"locale\":\"ja_JP\""), "invalid_locale", "/environment/locale");
    reject(changed(base, "\"locale\":\"und\"", "\"locale\":\"x-private\""), "invalid_locale", "/environment/locale");
    reject(changed(base, "{\"kind\":\"placeholder\"}", "{\"kind\":\"system\"}"), "unknown_value", "/environment/text/kind");
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
    reject(changed(base, "\"display\":\"flex\"", "\"display\":\"grid\""), "unknown_value", "/styles/0/display");
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
    reject(changed(base, "{\"timestamp\":1,\"type\":\"focus_next\"}", "{\"key\":\"tab\",\"timestamp\":1,\"type\":\"key_down\"}"),
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
    reject(changed(base, "{\"id\":\"start\",\"kind\":\"id\"}", "{\"kind\":\"semantic\",\"name\":\"Start\",\"role\":\"link\"}"),
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

    std::string many = "{\"actions\":[],\"document\":{\"root\":{\"type\":\"Box\"},\"version\":1},\"environment\":"
                       "{\"locale\":\"und\",\"scale\":1,\"text\":{\"kind\":\"placeholder\"}},\"policy\":{\"press_focus\":"
                       "false,\"reveal_focus\":false},\"steps\":[";
    for (std::size_t i = 0; i <= tessera::max_replay_steps; ++i) many += (i ? ",{" : "{") + std::string("\"timestamp\":0,\"type\":\"activate\"}");
    many += "],\"styles\":[],\"version\":1,\"viewport\":{\"height\":1,\"width\":1}}";
    reject(many, "out_of_range", "/steps");
}

void saving_rejects_invalid_recordings() {
    const auto reject = [](auto mutate, std::string_view code, std::string_view path) {
        auto value = recording();
        mutate(value);
        const auto saved = tessera::save_replay(value);
        check(!saved && has(saved.diagnostics, code, path), "Expected " + std::string(code) + " at " + std::string(path));
    };
    using R = tessera::ReplayRecording;
    reject([](R& r) { r.version = 0; }, "unsupported_version", "/version");
    reject([](R& r) { r.environment.scale = std::numeric_limits<float>::quiet_NaN(); }, "invalid_number", "/environment/scale");
    reject([](R& r) { r.environment.scale = -1; }, "out_of_range", "/environment/scale");
    reject([](R& r) { r.environment.locale = ""; }, "invalid_locale", "/environment/locale");
    reject([](R& r) { r.environment.locale = "en--US"; }, "invalid_locale", "/environment/locale");
    reject([](R& r) { r.environment.locale = "en-abcdefghi"; }, "invalid_locale", "/environment/locale");
    reject([](R& r) { r.environment.text.id = "fonts"; }, "invalid_text_service", "/environment/text");
    reject([](R& r) { r.environment.text.kind = static_cast<tessera::ReplayText::Kind>(9); }, "unknown_value",
           "/environment/text/kind");
    reject([](R& r) { r.environment.text = {tessera::ReplayText::Kind::font_profile, std::string(257, 'x'), std::string(64, '0')}; },
           "invalid_identifier", "/environment/text/id");
    reject([](R& r) { r.environment.text = {tessera::ReplayText::Kind::font_profile, "\xff", std::string(64, '0')}; },
           "invalid_utf8", "/environment/text/id");
    reject([](R& r) { r.environment.text = {tessera::ReplayText::Kind::font_profile, "fonts", std::string(63, '0')}; },
           "invalid_digest", "/environment/text/sha256");
    reject([](R& r) { r.context.actions.insert(""); }, "invalid_identifier", "/actions/0");
    reject([](R& r) { r.context.actions.erase("quit-game"); }, "unknown_action",
           "/document/root/children/1/events/activate");
    reject([](R& r) { r.viewport.width = std::numeric_limits<float>::infinity(); }, "invalid_number", "/viewport/width");
    reject([](R& r) { r.styles[1].opacity = -1; }, "out_of_range", "/styles/1/opacity");
    reject([](R& r) { r.styles[0].text.size = std::numeric_limits<float>::infinity(); }, "invalid_number",
           "/styles/0/text/size");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{1us, tessera::KeyDown{tessera::Key::tab}}); },
           "unsupported_event", "/steps/0/event");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{1us, tessera::TextInput{"a"}}); }, "unsupported_event",
           "/steps/0/event");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{std::chrono::microseconds{1LL << 60}, tessera::Activate{}}); },
           "out_of_range", "/steps/0/timestamp");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{-1us, tessera::Activate{}}); }, "out_of_range",
           "/steps/0/timestamp");
    reject([](R& r) { r.steps.push_back(tessera::InputEvent{1us, tessera::PointerDown{{1}, {std::nanf(""), 0}}}); },
           "invalid_number", "/steps/0/position/x");
    reject([](R& r) { r.steps.push_back(tessera::ReplayResize{{-1, 1}}); }, "out_of_range", "/steps/0/viewport/width");
    reject([](R& r) {
        auto styles = menu_styles(1);
        styles[1].gap = -1;
        r.steps.push_back(tessera::ReplayReload{menu({button("x", "X", "missing")}), styles});
    }, "out_of_range", "/steps/0/styles/1/gap");
    reject([](R& r) { r.steps.push_back(tessera::ReplayReload{menu({button("x", "X", "missing")}), menu_styles(1)}); },
           "unknown_action", "/steps/0/document/root/children/0/events/activate");
    reject([](R& r) { r.steps.push_back(tessera::ReplayFocus{tessera::PointTarget{{0, std::numeric_limits<float>::infinity()}}}); },
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
    check(!opened && has(opened.diagnostics, "out_of_range", "/environment/scale"), "Sessions must check the environment");
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
    check(tessera::detail::sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "empty");
    check(tessera::detail::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
    check(tessera::detail::sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "two-block message");
    check(tessera::detail::sha256_hex(std::string(1000000, 'a')) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "million a");
}

} // namespace

int main() {
    try {
        round_trip_preserves_every_field();
        loaded_recordings_replay_identically();
        loading_rejects_malformed_input();
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
