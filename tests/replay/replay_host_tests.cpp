#include <tessera/inspection/offscreen_runner.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include "../check.hpp"
#include <iostream>
#include <limits>

using namespace tessera;
using namespace std::chrono_literals;
using tessera::test::check;
using tessera::test::has;

namespace {
ReplayReload view(bool ready, std::string caption = "Continue 続ける") {
    UiDocument doc;
    doc.root.id = "menu";
    UiNode button;
    button.id = "continue";
    button.properties = {{"focusable", true}, {"disabled", !ready}};
    button.events["activate"] = "continue-game";
    UiNode label;
    label.kind = NodeKind::text;
    label.properties["text"] = std::move(caption);
    button.children.push_back(std::move(label));
    doc.root.children.push_back(std::move(button));
    std::vector<ResolvedStyle> styles(3);
    styles[0].padding = {0.25f, 0.5f, 0.75f, 1.25f};
    styles[1].height = Dimension::points(32.5f);
    styles[1].background = ready ? Color{0, 0.5f, 0, 1} : Color{0.3f, 0.3f, 0.3f, 1};
    return {std::move(doc), std::move(styles)};
}
ReplayRecording recording() {
    ReplayRecording r;
    r.version = replay_host_version;
    r.context.actions = {"continue-game"};
    r.environment.host = {{"save-ready", "menu"}};
    r.environment.scale = 1.25f;
    r.viewport = {180.5f, 80.25f};
    auto initial = view(false, "Loading 読み込み中");
    r.document = std::move(initial.document);
    r.styles = std::move(initial.styles);
    return r;
}
ReplayHostUpdate update(std::uint32_t request, ReplayReadiness state, bool value = false) {
    return {{"save-ready", "menu", request, state, value}, view(value)};
}
void accepted_menu() {
    PlaceholderTextShaper text;
    auto session = ReplaySession::open(recording(), text);
    check(bool(session), "Host fixture must open");
    check(bool(session.value->apply(update(1, ReplayReadiness::loading))), "Loading must publish");
    auto snapshot = session.value->capture();
    auto target = resolve_target(*snapshot.value, AuthorIdTarget{"continue"});
    auto rejected = session.value->invoke(*target.value);
    check(!rejected && has(rejected.diagnostics, "disabled_target", "/steps/1/target"),
          "Loading menu must reject semantic activation");
    check(bool(session.value->apply(InputEvent{1us, FocusNext{}})), "Navigation with no eligible node must be safe");
    check(bool(session.value->apply(InputEvent{2us, Activate{}})), "Disabled keyboard activation must be safe");
    check(session.value->output().actions.empty(), "Loading must request no action");
    check(bool(session.value->apply(update(1, ReplayReadiness::ready, true))), "Current completion must publish");
    check(bool(session.value->apply(InputEvent{3us, FocusNext{}})), "Ready menu must focus");
    check(bool(session.value->apply(InputEvent{4us, Activate{}})), "Ready menu must activate");
    snapshot = session.value->capture();
    target = resolve_target(*snapshot.value, AuthorIdTarget{"continue"});
    check(bool(session.value->invoke(*target.value)), "Ready semantic activation must agree");
    check(bool(session.value->apply(InputEvent{5us, PointerDown{{1}, {10, 10}}})), "Ready pointer down must dispatch");
    check(bool(session.value->apply(InputEvent{6us, PointerUp{{1}, {10, 10}}})), "Ready pointer up must dispatch");
    const auto& out = session.value->output();
    check(out.actions.size() == 3, "Keyboard, semantics and pointer must activate the ready menu");
    for (const auto& action : out.actions) check(action.action == "continue-game", "Action routes must agree");
    check(out.generations.size() == 3 && out.generations[1].host[0].readiness == ReplayReadiness::loading &&
          out.generations[2].host[0].value, "Generations must own their readiness and state");
    check(!out.semantics[1].nodes[1].enabled && out.semantics[4].nodes[1].enabled,
          "Semantic eligibility must change with the published view");
    const auto saved = save_replay(session.value->recording());
    check(bool(saved), "Host recording must save");
    const auto loaded = load_replay(*saved.value);
    check(loaded && *loaded.value == session.value->recording(), "Host JSON must preserve declarations and updates");
    check(*save_replay(*loaded.value).value == *saved.value, "Host JSON must be canonical");
    const auto replayed = play_replay(*loaded.value, text);
    check(replayed && compare_replay(out, *replayed.value).empty(), "Loaded host transitions must reproduce observations");
    const auto first = run_offscreen({&*loaded.value, &text});
    const auto second = run_offscreen({&*loaded.value, &text});
    check(first && second && *first.value == *second.value && first.value->output == out,
          "Offscreen runner must reproduce host generations");
    auto different = out;
    different.generations[2].host[0].value = false;
    check(has(compare_replay(out, different), "replay_mismatch", "/generations/2/host"), "Compare must locate host differences");
}
void rejection_and_lifetime() {
    PlaceholderTextShaper text;
    auto session = ReplaySession::open(recording(), text);
    const auto reject = [&](ReplayStep step, const char* code, const char* suffix) {
        const auto before = session.value->output();
        const auto accepted = session.value->recording();
        const auto path = "/steps/" + std::to_string(accepted.steps.size()) + suffix;
        const auto result = session.value->apply(std::move(step));
        check(!result && has(result.diagnostics, code, path), "Rejected host step must have a located diagnostic");
        check(!session.value->closed() && session.value->output() == before && session.value->recording() == accepted,
              "Rejected host update must preserve every observation and accepted input");
    };
    reject(update(1, ReplayReadiness::ready, true), "stale_request", "/slot/request");
    auto invalid = update(1, ReplayReadiness::loading);
    invalid.slot.id = "private-state";
    reject(invalid, "undeclared_state", "/slot/id");
    invalid.slot.id = "save-ready"; invalid.slot.owner = "continue";
    reject(invalid, "stale_owner", "/slot/owner");
    check(bool(session.value->apply(update(1, ReplayReadiness::loading))), "First request must start");
    check(bool(session.value->apply(update(2, ReplayReadiness::loading))), "New request must supersede");
    reject(update(1, ReplayReadiness::ready, true), "stale_request", "/slot/request");
    invalid = update(2, ReplayReadiness::ready, true);
    invalid.view.styles.clear();
    reject(invalid, "style_count", "/view/styles");
    check(bool(session.value->apply(update(2, ReplayReadiness::failed))), "Failure must close current request");
    reject(update(2, ReplayReadiness::ready, true), "stale_request", "/slot/request");
    check(bool(session.value->apply(update(3, ReplayReadiness::loading))), "Retry must use newer request");
    check(bool(session.value->apply(update(3, ReplayReadiness::cancelled))), "Cancel must close request");
    reject(update(3, ReplayReadiness::ready, true), "stale_request", "/slot/request");
    check(bool(session.value->apply(update(4, ReplayReadiness::loading))), "Request before reload must start");
    check(bool(session.value->apply(view(false))), "Raw reload with reused owner ID must apply");
    reject(update(4, ReplayReadiness::ready, true), "stale_request", "/slot/request");
    check(session.value->output().generations.back().host[0].readiness == ReplayReadiness::cancelled,
          "Reload must record cancellation before publishing the generation");
    check(bool(session.value->apply(update(std::numeric_limits<std::uint32_t>::max(), ReplayReadiness::loading))),
          "Request must accept full uint32 range");
    reject(update(0, ReplayReadiness::loading), "stale_request", "/slot/request");
}
void declarations_and_schema() {
    PlaceholderTextShaper text;
    auto r = recording();
    r.environment.host.push_back(r.environment.host.front());
    check(has(ReplaySession::open(r, text).diagnostics, "duplicate_state", "/environment/host/1/id"), "Duplicate declarations fail");
    r = recording(); r.environment.host.resize(65, r.environment.host.front());
    check(has(save_replay(r).diagnostics, "out_of_range", "/environment/host"), "Declaration bounds must apply");
    r = recording(); r.environment.host[0].owner = "missing";
    check(has(ReplaySession::open(r, text).diagnostics, "target_not_found", "/environment/host/0/owner"), "Initial owner must exist");
    r = recording(); r.version = replay_version;
    check(has(save_replay(r).diagnostics, "unsupported_event", "/environment/host"), "v1 cannot encode host declarations");
    r.environment.host.clear(); r.steps.push_back(update(1, ReplayReadiness::loading));
    check(has(save_replay(r).diagnostics, "unsupported_event", "/steps/0"), "v1 cannot encode host updates");
    r = recording(); r.steps.push_back(update(1, ReplayReadiness::loading));
    const auto base = *save_replay(r).value;
    const auto malformed = [&](std::string from, std::string to, const char* code, const char* path) {
        auto json = base; const auto index = json.find(from);
        check(index != std::string::npos, "Malformed fixture fragment must exist");
        json.replace(index, from.size(), to);
        const auto result = load_replay(json);
        check(!result && has(result.diagnostics, code, path), "Malformed host JSON must fail at its field");
        for (const auto& diagnostic : result.diagnostics) check(diagnostic.byte_offset.has_value(), "JSON errors must be located");
    };
    malformed("\"readiness\":\"idle\"", "\"readiness\":\"unknown\"", "unknown_value", "/environment/host/0/readiness");
    malformed("\"request\":0", "\"request\":-1", "schema_type", "/environment/host/0/request");
    malformed("\"value\":false", "\"value\":0", "schema_type", "/environment/host/0/value");
    malformed("\"owner\":\"menu\"", "\"owner\":\"\"", "invalid_identifier", "/environment/host/0/owner");
}
void replacement_cancels_other_owners() {
    PlaceholderTextShaper text;
    auto r = recording();
    r.environment.host.push_back({"other", "continue"});
    auto session = ReplaySession::open(r, text);
    check(bool(session.value->apply(update(1, ReplayReadiness::loading))), "First owner must start");
    auto other = update(1, ReplayReadiness::loading);
    other.slot.id = "other"; other.slot.owner = "continue";
    check(bool(session.value->apply(other)), "Second owner must start");
    auto first = update(1, ReplayReadiness::ready, true);
    const auto rejected = session.value->apply(first);
    check(!rejected && has(rejected.diagnostics, "stale_request", "/steps/2/slot/request"),
          "A host view replacement must invalidate other owner incarnations");
    auto invalid = other;
    invalid.slot.request = 2;
    invalid.view.document.root.children.clear(); invalid.view.styles.resize(1);
    const auto missing = session.value->apply(invalid);
    check(!missing && has(missing.diagnostics, "stale_owner", "/steps/2/slot/owner"),
          "A loading request cannot publish a view that removes its owner");
    check(bool(session.value->apply(update(2, ReplayReadiness::loading))), "Explicit newer request must restart");
    check(bool(session.value->apply(update(2, ReplayReadiness::ready, true))), "New request must complete");
    check(bool(session.value->apply(update(3, ReplayReadiness::loading, true))), "Replacement loading must retain accepted value");
    check(bool(session.value->apply(update(3, ReplayReadiness::failed, true))), "Failure must retain accepted value");
    check(session.value->output().generations.back().host[0].value, "Failure cannot silently erase the accepted value");
}
} // namespace
int main() {
    try { accepted_menu(); rejection_and_lifetime(); declarations_and_schema(); replacement_cancels_other_owners();
        std::cout << "Host readiness, declared state, menu eligibility, stale completion, rollback, JSON and offscreen checks passed.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
