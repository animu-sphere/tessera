#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/input/split_pane.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <string_view>
#ifdef _WIN32
#include "native_win32.hpp"
#endif

using namespace tessera;
namespace ui = tessera::authoring;
using namespace std::chrono_literals;

namespace {
template<class T> T take(Result<T> result) {
    if (!result) {
        std::string message;
        for (const auto& error : result.diagnostics)
            message += error.code + " at " + error.path + ": " + error.message + "\n";
        throw std::runtime_error(message);
    }
    return std::move(*result.value);
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Application styles are reusable declarations, separate from composition.
StyleSheet application_styles() {
    return {
        {{StyleSelector::of_class("form"),
          {.padding = Edges{12, 12, 12, 12}, .gap = 12.f, .background = Color{0.95f, 0.95f, 0.98f, 1}}},
         {StyleSelector::of_class("control"), {.padding = Edges{4, 8, 4, 8}, .background = Color{0.8f, 0.9f, 1, 1}}},
         {StyleSelector::of_class("status"), {.color = Color{0.25f, 0.3f, 0.4f, 1}}}}};
}
struct FieldProps {
    std::string label;
    float gap = 12;
};
ui::Element Field(FieldProps props, std::vector<ui::Element> children) {
    return ui::Row({ui::Text(std::move(props.label)), ui::Column(std::move(children))}, {.style = {.gap = props.gap}});
}
const std::vector<std::string> sections{"account", "feature", "about"};
constexpr ui::SliderRange volume_range{0, 100, 5};
constexpr ui::SplitPaneLimits pane_limits{96, 240, 8};
ui::Element Settings(const std::string& selected, ui::Value<TextEditState> username, ui::Value<bool> enabled,
                     ui::Value<std::string> summary, ui::Value<std::string> saved, const std::string& theme,
                     bool theme_open, int volume, float sidebar_size) {
    std::vector<ui::Element> choices;
    for (const auto& id : sections)
        choices.push_back(ui::Button((selected == id ? "* " : "") + id, "settings.section." + id,
                                     {.node = {.id = "section-" + id, .classes = {"control"}}}));
    std::vector<ui::Element> rows{ui::Text("Section " + selected, {.id = "selected-section", .classes = {"status"}})};
    if (selected == "account")
        rows.push_back(Field({"Account"}, {ui::TextInput("Username", username, "settings.edit",
                                                         {.node = {.id = "username", .classes = {"control"}}})}));
    else if (selected == "feature")
        rows.push_back(Field({"Feature"}, {ui::Checkbox("Enable feature", enabled, "settings.toggle",
                                                        {.node = {.id = "toggle", .classes = {"control"}}})}));
    else
        rows.push_back(Field({"About"}, {ui::Text("Apply saves the whole draft.", {.id = "section-help"})}));
    rows.push_back(ui::Dropdown("Theme", theme,
                                {{"system", "System", "settings.theme.system"},
                                 {"light", "Light", "settings.theme.light"},
                                 {"dark", "Dark", "settings.theme.dark"}},
                                "settings.theme.toggle",
                                {.node = {.id = "theme", .classes = {"control"}}, .expanded = theme_open}));
    rows.push_back(ui::Text(summary, {.id = "summary", .classes = {"status"}}));
    rows.push_back(ui::Row(
        {ui::Button(
             "Apply", "settings.apply",
             {.node = {.id = "apply", .classes = {"control"}, .style = {.background = Color{0.6f, 0.75f, 0.95f, 1}}}}),
         ui::Button("Cancel", "settings.cancel", {.node = {.id = "cancel", .classes = {"control"}}})},
        {.style = {.gap = 12.f}}));
    rows.push_back(ui::Text(saved, {.id = "save-result", .classes = {"status"}}));
    rows.push_back(ui::Slider("Volume", volume, volume_range, "settings.volume",
                              {.node = {.id = "volume", .classes = {"control"}}}));
    return ui::Column(
        {ui::Text("Settings"),
         ui::HorizontalSplitPane(
             ui::ScrollView(std::move(choices),
                            {.id = "categories", .style = {.height = Dimension::points(64), .gap = 12.f}}),
             ui::ScrollView(std::move(rows),
                            {.id = "settings-form", .style = {.height = Dimension::points(144), .gap = 12.f}}),
             sidebar_size, "settings.split",
             {.node = {.id = "settings-panes", .style = {.height = Dimension::points(144)}},
              .limits = pane_limits,
              .label = "Resize sidebar"})},
        {.id = "settings", .classes = {"form"}});
}

// Host state and synchronous in-memory saves own values, never UI references.
struct SettingsData {
    std::string username;
    bool feature = false;
    std::string theme = "system";
    int volume = 50;
    bool operator==(const SettingsData&) const = default;
};
std::string saved_caption(std::size_t count, const SettingsData& data) {
    return "Saved " + std::to_string(count) + ": " + data.username +
           (data.feature ? "; feature enabled" : "; feature disabled") + "; theme " + data.theme + "; volume " +
           std::to_string(data.volume);
}
struct Host final : CommandHost {
    TextEditState username = take(make_text_edit_state(1, "Ada"));
    SettingsData draft{username.text, false};
    SettingsData applied = draft;
    std::vector<SettingsData> saves;
    std::string result = "Not saved";
    std::string selected = "account", next_selected;
    std::optional<TextEditRequest> edit;
    TextEditState next_username;
    SettingsData next_draft;
    std::string next_result;
    bool applying = false;
    bool theme_open = false, next_theme_open = false;
    std::optional<int> volume_request;
    float sidebar_size = 140, next_sidebar_size = 140;
    std::optional<float> split_request;

    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        next_username = username;
        next_draft = draft;
        next_result = result;
        next_selected = selected;
        next_theme_open = theme_open;
        next_sidebar_size = sidebar_size;
        applying = invocation.command == "settings.apply";
        if (invocation.command == "settings.edit") {
            require(edit.has_value(), "Editing requires an owned request");
            auto candidate = edit_text(username, *edit);
            if (!candidate) return {CommandOutcomeStatus::error, {}, std::move(candidate.diagnostics)};
            next_username = std::move(*candidate.value);
            next_draft.username = next_username.text;
        } else if (invocation.command == "settings.cancel") {
            // Restore through the public edit API, retaining the owner and advancing revisions.
            auto selection =
                edit_text(username, {username.owner, username.revision, TextEditSelection{0, username.text.size()}});
            if (!selection) return {CommandOutcomeStatus::error, {}, std::move(selection.diagnostics)};
            auto restored = edit_text(*selection.value,
                                      {username.owner, selection.value->revision, TextEditInsert{applied.username}});
            if (!restored) return {CommandOutcomeStatus::error, {}, std::move(restored.diagnostics)};
            next_username = std::move(*restored.value);
            next_draft = applied;
        } else if (invocation.command == "settings.split") {
            require(split_request.has_value(), "Split adjustment requires an owned size request");
            next_sidebar_size = *split_request;
        } else if (invocation.command == "settings.volume") {
            next_draft.volume =
                volume_request.value_or(std::min(volume_range.maximum, draft.volume + volume_range.step));
        } else if (invocation.command == "settings.toggle")
            next_draft.feature = !draft.feature;
        else if (invocation.command == "settings.theme.toggle")
            next_theme_open = !theme_open;
        else if (invocation.command.starts_with("settings.theme.")) {
            const auto key = invocation.command.substr(15);
            require(key == "system" || key == "light" || key == "dark", "Unknown theme");
            next_draft.theme = key;
            next_theme_open = false;
        } else if (invocation.command.starts_with("settings.section."))
            next_selected = invocation.command.substr(17);
        else {
            require(applying, "Unexpected command");
            next_result = saved_caption(saves.size() + 1, next_draft);
        }
        return {};
    }
};

constexpr Size viewport{640, 240};
using ScrollOffsets = std::map<std::string, Point>;
void update_offsets(ScrollOffsets& offsets, const UiTree& tree, const std::vector<ScrollUpdate>& updates) {
    for (const auto& update : updates) {
        const auto* node = tree.get(update.container);
        require(node && node->id.has_value(), "Scroll owners require stable author IDs");
        offsets[*node->id] = update.offset;
    }
}
const ValidationContext actions{{"settings.edit", "settings.toggle", "settings.apply", "settings.cancel",
                                 "settings.volume", "settings.split", "settings.theme.toggle", "settings.theme.system",
                                 "settings.theme.light", "settings.theme.dark", "settings.section.account",
                                 "settings.section.feature", "settings.section.about"}};
struct Presentation {
    CommandSnapshot commands;
    std::unique_ptr<UiTree> tree;
    std::vector<ResolvedStyle> styles;
    LayoutResult layout;
    InspectionSnapshot inspection;
    UiDrawList paint;
    HitTestInput hit() const { return {tree.get(), styles, &layout}; }
    SemanticInput semantic() const { return {tree.get(), styles, &layout}; }
};
const LayoutBox& box_for(const Presentation& presentation, NodeHandle node) {
    const auto found = std::find_if(presentation.layout.boxes.begin(), presentation.layout.boxes.end(),
                                    [&](const auto& box) { return box.node == node; });
    require(found != presentation.layout.boxes.end(), "Missing layout geometry");
    return *found;
}
// Every borrowed input is used synchronously; returned observations are owned.
Result<Presentation> prepare(ui::Session& session, const ui::Element& form, CommandSnapshot commands,
                             const StyleSheet& sheet, TextShaper& text, const ScrollOffsets& offsets, Size available) {
    auto authored = session.lower(form, actions);
    if (!authored) return {std::nullopt, std::move(authored.diagnostics)};
    auto document = commands.apply_eligibility(authored.value->document, actions);
    if (!document) return {std::nullopt, std::move(document.diagnostics)};
    auto tree = UiTree::create(*document.value, actions);
    if (!tree) return {std::nullopt, std::move(tree.diagnostics)};
    auto styles = resolve_styles({tree.value->get(), &sheet, {}, authored.value->styles});
    if (!styles) return {std::nullopt, std::move(styles.diagnostics)};
    std::vector<Point> requested((*tree.value)->size());
    for (const auto& [id, offset] : offsets)
        if (const auto owner = (*tree.value)->find(id)) requested[owner->index] = offset;
    auto layout = compute_layout({tree.value->get(), *styles.value, available, &text, requested});
    if (!layout) return {std::nullopt, std::move(layout.diagnostics)};
    auto inspection = capture_inspection({tree.value->get(), *styles.value, &*layout.value, &authored.value->sources});
    if (!inspection) return {std::nullopt, std::move(inspection.diagnostics)};
    auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &text});
    if (!paint) return {std::nullopt, std::move(paint.diagnostics)};
    return {Presentation{std::move(commands), std::move(*tree.value), std::move(*styles.value),
                         std::move(*layout.value), std::move(*inspection.value), std::move(*paint.value)},
            {}};
}
std::string name(const Presentation& presentation, std::string id) {
    const auto target = take(resolve_target(presentation.inspection, AuthorIdTarget{std::move(id)}));
    const auto semantic = presentation.inspection.elements[target.index].semantic;
    require(semantic.has_value(), "Missing semantic observation");
    return presentation.inspection.semantics.nodes[*semantic].name;
}
// Placeholder glyph bit marks. Rasterizer -> cache -> capture borrowing order.
struct SyntheticGlyphs final : GlyphRasterizer {
    std::uint64_t face_revision(FontId font) const override { return font.value == 0 ? 1 : 0; }
    Result<GlyphBitmap> rasterize(const GlyphRasterRequest& request) override {
        GlyphBitmap bitmap{5, 7, 0, 7, std::vector<std::uint8_t>(35)};
        for (std::size_t i = 0; i < bitmap.coverage.size(); ++i)
            bitmap.coverage[i] = (request.glyph >> (i % 21)) & 1 ? 255 : 0;
        return {std::move(bitmap), {}};
    }
};
} // namespace

#ifdef TESSERA_EXAMPLE_LIBRARY
#include "../fixtures.hpp"
int tessera::examples::run_settings_starter(const FrameObserver& observer) {
    const int argc = 1;
    char** argv = nullptr;
    const auto frame_observer = [&](std::string_view name, Size size, const UiDrawList& paint) {
        if (observer) observer(name, size, paint);
    };
#else
int main(int argc, char** argv) {
    const auto frame_observer = [](std::string_view, Size, const UiDrawList&) {};
#endif
    try {
        const bool native =
            argc == 2 && (std::string_view(argv[1]) == "--native" || std::string_view(argv[1]) == "--native-smoke");
        require(argc == 1 || native, "Usage: settings_starter [--native | --native-smoke]");
#ifndef _WIN32
        require(!native, "The Settings native host currently requires Windows");
#endif
        ui::Session session;
        Host host;
        const auto username = session.state("username", host.username);
        const auto enabled = session.state("enabled", host.draft.feature);
        const auto saved = session.state("save-result", host.result);
        const auto summary = session.computed("summary", [username, enabled] {
            return "User " + username.read().text + (enabled.read() ? "; feature enabled" : "; feature disabled");
        });
        auto form = [&](const std::string& selected, const std::string& theme, bool open, int volume,
                        float sidebar_size) {
            return Settings(selected, username, enabled, summary, saved, theme, open, volume, sidebar_size);
        };
        const auto sheet = application_styles();
        const auto registry = take(CommandRegistry::create({{"settings.edit", "Edit username"},
                                                            {"settings.toggle", "Toggle feature"},
                                                            {"settings.apply", "Apply settings"},
                                                            {"settings.cancel", "Cancel pending settings"},
                                                            {"settings.volume", "Adjust volume"},
                                                            {"settings.split", "Resize sidebar"},
                                                            {"settings.theme.toggle", "Open or close theme choices"},
                                                            {"settings.theme.system", "Use system theme"},
                                                            {"settings.theme.light", "Use light theme"},
                                                            {"settings.theme.dark", "Use dark theme"},
                                                            {"settings.section.account", "Select Account"},
                                                            {"settings.section.feature", "Select Feature"},
                                                            {"settings.section.about", "Select About"}},
                                                           {{"settings.edit", "settings.edit"},
                                                            {"settings.toggle", "settings.toggle"},
                                                            {"settings.apply", "settings.apply"},
                                                            {"settings.cancel", "settings.cancel"},
                                                            {"settings.volume", "settings.volume"},
                                                            {"settings.split", "settings.split"},
                                                            {"settings.theme.toggle", "settings.theme.toggle"},
                                                            {"settings.theme.system", "settings.theme.system"},
                                                            {"settings.theme.light", "settings.theme.light"},
                                                            {"settings.theme.dark", "settings.theme.dark"},
                                                            {"settings.section.account", "settings.section.account"},
                                                            {"settings.section.feature", "settings.section.feature"},
                                                            {"settings.section.about", "settings.section.about"}}));
        std::uint64_t generation = 0, sequence = 0;
        auto commands = [&] {
            return take(CommandSnapshot::publish(registry, ++generation, CommandMode::production,
                                                 {{"settings.edit", {true}},
                                                  {"settings.toggle", {true}},
                                                  {"settings.apply", {true}},
                                                  {"settings.cancel", {true}},
                                                  {"settings.volume", {true}},
                                                  {"settings.split", {true}},
                                                  {"settings.theme.toggle", {true}},
                                                  {"settings.theme.system", {true}},
                                                  {"settings.theme.light", {true}},
                                                  {"settings.theme.dark", {true}},
                                                  {"settings.section.account", {true}},
                                                  {"settings.section.feature", {true}},
                                                  {"settings.section.about", {true}}}));
        };
        PlaceholderTextShaper text;
        ScrollOffsets offsets;
        FocusDispatcher focus;
        PointerDispatcher pointer;
        SplitPaneDispatcher splitter("settings-panes");
        Size canvas = viewport;
        auto stage = [&](const std::string& selected, ScrollOffsets requested, FocusDispatcher candidate_focus,
                         bool reveal, const std::string& theme, bool open, int volume) -> Result<Presentation> {
            const auto snapshot = commands();
            auto element = form(selected, theme, open, volume, host.next_sidebar_size);
            auto candidate = prepare(session, element, snapshot, sheet, text, requested, canvas);
            if (!candidate) return candidate;
            const auto geometry = take(split_pane_geometry(candidate.value->hit(), "settings-panes"));
            require(geometry.has_value(), "Missing Settings splitter");
            if (geometry->size != host.next_sidebar_size) {
                element = form(selected, theme, open, volume, geometry->size);
                candidate = prepare(session, element, snapshot, sheet, text, requested, canvas);
                if (!candidate) return candidate;
            }
            const auto target = take(candidate_focus.refresh(candidate.value->hit())).focused;
            if (reveal && target) {
                const auto updates = take(scroll_into_view(candidate.value->hit(), *target));
                if (!updates.empty()) {
                    update_offsets(requested, *candidate.value->tree, updates);
                    candidate = prepare(session, element, snapshot, sheet, text, requested, canvas);
                }
            }
            return candidate;
        };
        auto accepted =
            take(stage(host.selected, offsets, focus, false, host.draft.theme, host.theme_open, host.draft.volume));
        session.publish();
        auto accept = [&](Presentation candidate) {
            for (const auto& box : candidate.layout.boxes)
                if (box.scroll) offsets[*candidate.tree->get(box.node)->id] = box.scroll->offset;
            accepted = std::move(candidate);
            (void)take(focus.refresh(accepted.hit()));
            (void)take(pointer.refresh(accepted.hit()));
            const auto split = take(splitter.refresh(accepted.hit()));
            require(split.size.has_value(), "Settings splitter lost its size");
            host.sidebar_size = host.next_sidebar_size = *split.size;
        };
        CommandExecutor executor;
        auto focused = [&](const char* id) {
            require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find(id), "Unexpected focus");
            require(take(scroll_into_view(accepted.hit(), *accepted.tree->find(id))).empty(),
                    "Navigation did not reveal the focused control");
        };
        auto operate = [&](const ActionRequest& action, CommandSource source,
                           std::optional<TextEditRequest> edit = {}) {
            // A TextInput click/activation focuses it; only committed edits carry a request.
            if (action.action == "settings.edit" && !edit) return;
            if (action.action == "settings.split" && !host.split_request) return;
            const auto current = accepted.semantic();
            const auto invocation = take(accepted.commands.resolve_action(
                current, action, accepted.commands.generation(), {}, source, ++sequence));
            host.edit = std::move(edit);
            const auto execution = take(executor.execute(accepted.commands, invocation, host, &current));
            host.edit.reset();
            if (execution.outcome.status != CommandOutcomeStatus::success) {
                (void)take(Result<Presentation>{std::nullopt, execution.outcome.diagnostics});
            }
            session.update([&] {
                username.write(host.next_username);
                enabled.write(host.next_draft.feature);
                saved.write(host.next_result);
            });
            auto candidate_focus = focus;
            // Closing choices returns keyboard focus to their stable trigger.
            if (host.theme_open && !host.next_theme_open)
                (void)take(candidate_focus.focus(accepted.hit(), *accepted.tree->find("theme-trigger")));
            auto candidate = stage(host.next_selected, offsets, candidate_focus, true, host.next_draft.theme,
                                   host.next_theme_open, host.next_draft.volume);
            if (!candidate) {
                // Returned stage failure: restore observations, retain host values,
                // save records and presentation, then exit with diagnostics.
                session.update([&] {
                    username.write(host.username);
                    enabled.write(host.draft.feature);
                    saved.write(host.result);
                });
                host.next_sidebar_size = host.sidebar_size;
                (void)take(std::move(candidate));
            }
            if (host.applying) {
                host.saves.push_back(host.next_draft); // Owned synchronous copy, once.
                host.applied = host.next_draft;
            }
            host.username = std::move(host.next_username);
            host.draft = std::move(host.next_draft);
            host.result = std::move(host.next_result);
            host.selected = std::move(host.next_selected);
            host.theme_open = host.next_theme_open;
            focus = std::move(candidate_focus);
            session.publish();
            accept(std::move(*candidate.value));
        };
        // Host capture uses a stable author ID and pointer identity, never a tree handle.
        std::optional<PointerId> volume_pointer;
        auto set_volume = [&](int value, CommandSource source) {
            if (value == host.draft.volume) return;
            const auto action =
                take(request_semantic_action(accepted.semantic(), *accepted.tree->find("volume"), "activate"));
            host.volume_request = value;
            operate(action, source);
            host.volume_request.reset();
        };
        auto volume_at = [&](Point position) {
            const auto& track = box_for(accepted, *accepted.tree->find("volume-track")).border_box;
            require(track.size.width > 8, "Slider track has no travel");
            return take(ui::slider_value_at(volume_range, (position.x - track.origin.x - 4) / (track.size.width - 8)));
        };
        auto input = [&](InputEvent event, CommandSource source = CommandSource::keyboard) {
            require(validate(event).empty(), "Invalid normalized input");
            const auto split =
                take(splitter.dispatch(accepted.hit(), event, take(focus.refresh(accepted.hit())).focused));
            if (split.handled) {
                volume_pointer.reset();
                pointer.reset();
                if (split.focus) (void)take(focus.focus(accepted.hit(), *split.focus));
                if (split.action) {
                    host.split_request = split.size;
                    operate(*split.action, source);
                    host.split_request.reset();
                }
                return;
            }
            if (const auto* cancel = std::get_if<PointerCancel>(&event.data);
                cancel && volume_pointer == cancel->pointer) {
                volume_pointer.reset();
                return; // Keep the last accepted draft; no deferred release action.
            }
            if (const auto* move = std::get_if<PointerMove>(&event.data); move && volume_pointer == move->pointer) {
                set_volume(volume_at(move->position), source);
                return;
            }
            if (const auto* up = std::get_if<PointerUp>(&event.data);
                up && volume_pointer == up->pointer && up->button == PointerButton::primary) {
                volume_pointer.reset();
                set_volume(volume_at(up->position), source);
                return;
            }
            if (const auto* down = std::get_if<PointerDown>(&event.data);
                down && !volume_pointer && down->button == PointerButton::primary) {
                // hit_test enforces the form clip, inherited disabled state and occlusion.
                const auto hit = take(hit_test(accepted.hit(), down->position)).target;
                if (hit && (hit == accepted.tree->find("volume-track") || hit == accepted.tree->find("volume-fill") ||
                            hit == accepted.tree->find("volume-thumb"))) {
                    pointer.reset();
                    (void)take(focus.focus(accepted.hit(), *accepted.tree->find("volume")));
                    volume_pointer = down->pointer;
                    set_volume(volume_at(down->position), source);
                    // A press at the current value still reveals newly focused content.
                    if (!take(scroll_into_view(accepted.hit(), *accepted.tree->find("volume"))).empty()) {
                        auto candidate = take(stage(host.selected, offsets, focus, true, host.draft.theme,
                                                    host.theme_open, host.draft.volume));
                        session.publish();
                        accept(std::move(candidate));
                    }
                    return;
                }
            }
            if (const auto* scroll = std::get_if<Scroll>(&event.data)) {
                auto requested = offsets;
                const auto routed = take(route_scroll(accepted.hit(), *scroll));
                if (!routed.updates.empty()) {
                    update_offsets(requested, *accepted.tree, routed.updates);
                    volume_pointer.reset();
                    auto candidate = take(stage(host.selected, requested, focus, false, host.draft.theme,
                                                host.theme_open, host.draft.volume));
                    session.publish();
                    accept(std::move(candidate));
                }
                return;
            }
            if (std::holds_alternative<PointerMove>(event.data) || std::holds_alternative<PointerDown>(event.data) ||
                std::holds_alternative<PointerUp>(event.data) || std::holds_alternative<PointerCancel>(event.data)) {
                if (volume_pointer) return;
                const auto dispatched = take(pointer.dispatch(accepted.hit(), event));
                if (const auto* down = std::get_if<PointerDown>(&event.data);
                    down && down->button == PointerButton::primary)
                    for (const auto& state : dispatched.pointers)
                        if (state.pointer == down->pointer && state.pressed)
                            (void)take(focus.focus(accepted.hit(), *state.pressed));
                for (const auto& action : dispatched.actions) operate(action, source);
                return;
            }
            volume_pointer.reset();
            std::optional<decltype(TextEditRequest::operation)> edit;
            const auto* key_down = std::get_if<KeyDown>(&event.data);
            if (host.theme_open && (std::holds_alternative<Cancel>(event.data) ||
                                    (key_down && key_down->key == Key::escape && key_down->modifiers == Modifiers{} &&
                                     !key_down->repeat))) {
                const auto action = take(
                    request_semantic_action(accepted.semantic(), *accepted.tree->find("theme-trigger"), "activate"));
                operate(action, source);
                return;
            }
            const auto owner = accepted.tree->find("username");
            const auto current_focus = take(focus.refresh(accepted.hit())).focused;
            if (key_down && key_down->modifiers == Modifiers{} && current_focus == accepted.tree->find("volume")) {
                int value = host.draft.volume;
                switch (key_down->key) {
                    case Key::left:
                    case Key::down:
                        value -= volume_range.step;
                        break;
                    case Key::right:
                    case Key::up:
                        value += volume_range.step;
                        break;
                    case Key::home:
                        value = volume_range.minimum;
                        break;
                    case Key::end:
                        value = volume_range.maximum;
                        break;
                    default:
                        break;
                }
                volume_pointer.reset();
                set_volume(std::clamp(value, volume_range.minimum, volume_range.maximum), source);
                return;
            }
            if (owner && current_focus == owner) { // An absent owner must never match an empty focus.
                if (const auto* insert = std::get_if<tessera::TextInput>(&event.data))
                    edit = TextEditInsert{insert->text};
                if (const auto* key = std::get_if<KeyDown>(&event.data);
                    key && key->key == Key::a && key->modifiers == Modifiers{.control = true} && !key->repeat)
                    edit = TextEditSelection{0, host.username.text.size()};
                if (const auto* key = std::get_if<KeyDown>(&event.data);
                    key && !key->modifiers.control && !key->modifiers.alt && !key->modifiers.super) {
                    switch (key->key) {
                        case Key::left:
                            edit = TextEditMove{TextMove::left, key->modifiers.shift};
                            break;
                        case Key::right:
                            edit = TextEditMove{TextMove::right, key->modifiers.shift};
                            break;
                        case Key::home:
                            edit = TextEditMove{TextMove::home, key->modifiers.shift};
                            break;
                        case Key::end:
                            edit = TextEditMove{TextMove::end, key->modifiers.shift};
                            break;
                        case Key::backspace:
                            edit = TextEditErase{TextErase::backward};
                            break;
                        case Key::delete_key:
                            edit = TextEditErase{TextErase::forward};
                            break;
                        default:
                            break;
                    }
                }
            }
            if (edit) {
                const auto action =
                    take(request_semantic_action(accepted.semantic(), *accepted.tree->find("username"), "activate"));
                operate(action, source, TextEditRequest{host.username.owner, host.username.revision, std::move(*edit)});
                return; // Supported editing keys precede logical navigation.
            }
            // Other raw keys/text have no mapping in this bounded host.
            if (std::holds_alternative<KeyDown>(event.data) || std::holds_alternative<tessera::TextInput>(event.data))
                return;
            const auto dispatched = take(focus.dispatch(accepted.hit(), event));
            for (const auto& action : dispatched.actions) operate(action, source);
            if (dispatched.actions.empty() && dispatched.focused &&
                !take(scroll_into_view(accepted.hit(), *dispatched.focused)).empty()) {
                auto candidate = take(
                    stage(host.selected, offsets, focus, true, host.draft.theme, host.theme_open, host.draft.volume));
                session.publish();
                accept(std::move(candidate));
            }
        };

#ifdef _WIN32
        if (native) {
            settings_native::Glyphs glyphs;
            GlyphCache native_cache(glyphs);
            CpuFrameCapture native_frames(&native_cache);
            settings_native::Window window;
            window.input = input;
            window.capture = [&] {
                return take(native_frames.capture({accepted.commands.generation(), canvas, 1,
                                                   take(capture_extent(canvas, 1)), &accepted.paint, 0us}));
            };
            window.focused = [&]() -> std::optional<Rect> {
                const auto target = take(focus.refresh(accepted.hit())).focused;
                if (!target || !take(scroll_into_view(accepted.hit(), *target)).empty()) return {};
                return box_for(accepted, *target).border_box;
            };
            window.editing = [&] {
                const auto owner = accepted.tree->find("username");
                return owner && take(focus.refresh(accepted.hit())).focused == owner;
            };
            window.resized = [&](Size available) {
                const auto target = take(focus.refresh(accepted.hit())).focused;
                const bool reveal = target && take(scroll_into_view(accepted.hit(), *target)).empty();
                canvas = available;
                auto candidate = take(
                    stage(host.selected, offsets, focus, reveal, host.draft.theme, host.theme_open, host.draft.volume));
                session.publish();
                accept(std::move(candidate));
            };
            window.resize_cursor = [&](Point point) {
                const auto geometry = take(split_pane_geometry(accepted.hit(), "settings-panes"));
                return geometry && geometry->enabled &&
                       (splitter.dragging() || take(hit_test(accepted.hit(), point)).target == geometry->handle);
            };
            auto smoke = [&](settings_native::Window& native_window) {
                auto key = [&](WPARAM code) { native_window.message(WM_KEYDOWN, code, 1); };
                auto seek = [&](const char* id) {
                    for (int i = 0; i < 24; ++i) {
                        if (take(focus.refresh(accepted.hit())).focused == accepted.tree->find(id)) {
                            focused(id);
                            return;
                        }
                        key(VK_TAB);
                    }
                    throw std::runtime_error(std::string("Native Tab cannot reach ") + id);
                };
                auto center = [&](const char* id) {
                    const auto& rect = box_for(accepted, *accepted.tree->find(id)).border_box;
                    return native_window.physical(
                        {rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2});
                };
                auto mouse = [&](UINT type, POINT point) {
                    native_window.message(type, type == WM_LBUTTONDOWN ? MK_LBUTTON : 0, MAKELPARAM(point.x, point.y));
                };
                auto click = [&](const char* id) {
                    const auto point = center(id);
                    mouse(WM_LBUTTONDOWN, point);
                    mouse(WM_LBUTTONUP, point);
                };
                auto wheel = [&](const char* pane, short delta) {
                    auto point = center(pane);
                    ClientToScreen(native_window.handle, &point);
                    native_window.message(WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(delta)),
                                          MAKELPARAM(point.x, point.y));
                };
                require(native_window.presentations > 0, "Native window did not present");
                const auto glyph = take(native_cache.get({{}, 'A', 16 * 64}));
                require(
                    std::any_of(glyph->coverage.begin(), glyph->coverage.end(), [](auto value) { return value != 0; }),
                    "Native glyph coverage is empty");
                seek("username");
                const auto before_click = sequence;
                click("username");
                key(VK_RETURN);
                require(sequence == before_click && window.editing(), "TextInput activation executed an empty edit");
                for (const wchar_t unit : std::wstring_view(L" 本\U0001f600")) native_window.message(WM_CHAR, unit);
                const auto edited = host.username;
                require(edited.text == "Ada 本😀", "Native UTF-16 text mapping differs");
                seek("section-feature");
                key(VK_RETURN);
                seek("toggle");
                click("toggle");
                require(host.draft.feature && host.username == edited && host.saves.empty(),
                        "Native category/toggle lost the draft");
                seek("theme-trigger");
                key(VK_SPACE);
                native_window.message(WM_KEYDOWN, VK_ESCAPE, 1 | (LPARAM{1} << 30));
                require(host.theme_open, "Repeated Escape dismissed native choices");
                key(VK_ESCAPE);
                require(!host.theme_open && host.draft.theme == "system", "Native Escape edited the preference");
                key(VK_SPACE);
                seek("theme-option-dark");
                key(VK_RETURN);
                require(host.draft.theme == "dark" && !host.theme_open, "Native Dropdown did not select Dark");
                seek("volume");
                key(VK_END);
                key(VK_LEFT);
                require(host.draft.volume == 95 && host.username == edited, "Native slider keyboard mapping differs");
                auto track = [&](float fraction) {
                    const auto& rect = box_for(accepted, *accepted.tree->find("volume-track")).border_box;
                    return native_window.physical(
                        {rect.origin.x + 4 + fraction * (rect.size.width - 8), rect.origin.y + rect.size.height / 2});
                };
                mouse(WM_LBUTTONDOWN, track(.25f));
                require(GetCapture() == native_window.handle, "Native slider did not capture the mouse");
                mouse(WM_MOUSEMOVE, track(2));
                require(host.draft.volume == 100, "Native outside-track drag did not clamp");
                mouse(WM_LBUTTONUP, track(.75f));
                require(host.draft.volume == 75 && GetCapture() != native_window.handle,
                        "Native release/capture mapping differs");
                mouse(WM_LBUTTONDOWN, track(.5f));
                ReleaseCapture();
                const auto cancelled_volume = host.draft.volume;
                mouse(WM_LBUTTONUP, track(1));
                require(host.draft.volume == cancelled_volume, "Capture loss executed a deferred slider edit");
                mouse(WM_LBUTTONDOWN, track(.75f));
                mouse(WM_LBUTTONUP, track(.75f));
                seek("settings-panes-handle");
                const auto split_draft = std::tuple{host.username, host.draft, host.saves, host.selected, offsets};
                key(VK_HOME);
                key(VK_RIGHT);
                require(host.sidebar_size == 104, "Native SplitPane keyboard adjustment differs");
                auto split_point = center("settings-panes-handle");
                require(
                    window.resize_cursor(take(split_pane_geometry(accepted.hit(), "settings-panes"))->bounds.origin),
                    "Native SplitPane did not expose a resize cursor");
                mouse(WM_LBUTTONDOWN, split_point);
                require(GetCapture() == native_window.handle && splitter.dragging(),
                        "Native SplitPane did not capture");
                split_point.x += static_cast<LONG>(std::lround(40.f * GetDpiForWindow(native_window.handle) / 96));
                mouse(WM_MOUSEMOVE, split_point);
                require(std::abs(host.sidebar_size - 144) < 1 && splitter.dragging(),
                        "Native SplitPane drag lost logical coordinates or capture on replacement");
                ReleaseCapture();
                const auto cancelled_split = host.sidebar_size;
                split_point.x += 80;
                mouse(WM_LBUTTONUP, split_point);
                require(host.sidebar_size == cancelled_split && !splitter.dragging() &&
                            split_draft == std::tuple{host.username, host.draft, host.saves, host.selected, offsets},
                        "Native SplitPane cancellation changed pending edits, scroll or saves");
                seek("apply");
                const auto apply_point = center("apply");
                mouse(WM_LBUTTONDOWN, apply_point);
                native_window.message(WM_CANCELMODE);
                mouse(WM_LBUTTONUP, apply_point);
                require(host.saves.empty(), "Cancelled native press executed Apply");
                key(VK_RETURN);
                const auto applied = host.draft;
                require(host.saves == std::vector<SettingsData>{applied} && applied.volume == 75,
                        "Native Apply did not save exactly once");
                // Repeated activation must not append another save.
                native_window.message(WM_KEYDOWN, VK_RETURN, 1 | (LPARAM{1} << 30));
                require(host.saves.size() == 1, "Native activation auto-repeat saved twice");
                seek("section-account");
                key(VK_RETURN);
                seek("username");
                native_window.message(WM_CHAR, '!');
                seek("volume");
                key(VK_HOME);
                seek("cancel");
                key(VK_RETURN);
                require(host.draft == applied && host.selected == "account" && host.saves.size() == 1,
                        "Native Cancel lost applied values, category or saves");
                wheel("categories", -WHEEL_DELTA * 10);
                click("section-about");
                require(host.selected == "about" && host.draft == applied, "Native wheel/pointer did not reach About");
                wheel("settings-form", -WHEEL_DELTA * 10);
                require(offsets.at("settings-form").y > 0, "Native wheel did not scroll the form");
                // Client size and DPI define the logical viewport, not a scaled fixed canvas.
                SetWindowPos(native_window.handle, nullptr, 0, 0, 1000, 620,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                RECT dpi_rect{};
                GetWindowRect(native_window.handle, &dpi_rect);
                dpi_rect.right += 40;
                native_window.message(WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&dpi_rect));
                RECT actual_rect{};
                GetWindowRect(native_window.handle, &actual_rect);
                require(actual_rect.right == dpi_rect.right && host.draft == applied,
                        "Native DPI resize lost bounds or draft");
                wheel("categories", WHEEL_DELTA * 10);
                click("section-account");
                require(host.selected == "account" && host.draft == applied,
                        "Resized pointer coordinates differ from presentation");
                seek("username");
                wheel("settings-form", -WHEEL_DELTA * 10);
                const auto user_scroll = offsets.at("settings-form");
                require(!take(scroll_into_view(accepted.hit(), *accepted.tree->find("username"))).empty(),
                        "Native scroll-away fixture did not hide focus");
                SetWindowPos(native_window.handle, nullptr, 0, 0, 1100, 620,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                require(offsets.at("settings-form") == user_scroll,
                        "Native resize revealed focus the user deliberately scrolled away from");
                seek("settings-panes-handle");
                key(VK_END);
                const auto large_split = host.sidebar_size;
                mouse(WM_LBUTTONDOWN, center("settings-panes-handle"));
                SetWindowPos(native_window.handle, nullptr, 0, 0, 440, 420, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                require(!splitter.dragging() && host.sidebar_size < large_split &&
                            host.sidebar_size == take(split_pane_geometry(accepted.hit(), "settings-panes"))->maximum &&
                            host.draft == applied,
                        "Native resize did not cancel and clamp the SplitPane");
                const auto clamped_split = host.sidebar_size;
                SetWindowPos(native_window.handle, nullptr, 0, 0, 1000, 620,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                require(host.sidebar_size == clamped_split && host.saves.size() == 1,
                        "Native viewport growth restored an obsolete split or saved state");
                seek("username");
                native_window.message(WM_CHAR, 0xd83d);
                native_window.message(WM_KILLFOCUS);
                native_window.message(WM_CHAR, 0xde00);
                require(host.draft == applied, "Focus loss retained an incomplete surrogate pair");
                require(native_window.presentations > 10, "Native updates were not presented");
                std::cout
                    << "Native Settings smoke passed: window, text, focus, scroll, Dropdown, Slider, SplitPane, Apply/Cancel and capture loss; "
                    << native_window.presentations << " presentations\n";
            };
            if (std::string_view(argv[1]) == "--native-smoke") return window.run(smoke);
            return window.run();
        }
#endif
        SyntheticGlyphs raster;
        GlyphCache cache(raster);
        CpuFrameCapture frames(&cache);
        const auto extent = take(capture_extent(viewport, 1));
        auto capture = [&] { return take(frames.capture({0, viewport, 1, extent, &accepted.paint, 0us})); };
        auto observe = [&] {
            require(name(accepted, "volume") == "Volume: " + std::to_string(host.draft.volume), "Slider value differs");
            require(name(accepted, "selected-section") == "Section " + host.selected, "Section name differs");
            for (const auto& id : sections)
                require(name(accepted, "section-" + id) == (host.selected == id ? "* " : "") + id,
                        "Selected marker differs");
            require(accepted.tree->find("username").has_value() == (host.selected == "account") &&
                        accepted.tree->find("toggle").has_value() == (host.selected == "feature") &&
                        accepted.tree->find("section-help").has_value() == (host.selected == "about"),
                    "Conditional branch differs");
            require(name(accepted, "summary") == "User " + host.draft.username +
                                                     (host.draft.feature ? "; feature enabled" : "; feature disabled"),
                    "Summary differs");
            if (host.selected == "account")
                require(name(accepted, "username") == "Username " + host.username.text + " Selection " +
                                                          std::to_string(host.username.anchor) + ":" +
                                                          std::to_string(host.username.caret),
                        "Edit observation differs");
            if (host.selected == "feature")
                require(name(accepted, "toggle") == (host.draft.feature ? "[x] Enable feature" : "[ ] Enable feature"),
                        "Checkbox differs");
            if (host.selected == "about")
                require(name(accepted, "section-help") == "Apply saves the whole draft.", "Help differs");
            require(name(accepted, "save-result") == host.result, "Saved observation differs");
            require(name(accepted, "theme-trigger") == "Theme: " +
                                                           std::string(host.draft.theme == "system"  ? "System"
                                                                       : host.draft.theme == "light" ? "Light"
                                                                                                     : "Dark") +
                                                           (host.theme_open ? " [-]" : " [+]"),
                    "Dropdown observation differs");
            require(accepted.tree->find("theme-option-system").has_value() == host.theme_open,
                    "Collapsed Dropdown retained options");
            // Offscreen content retains geometry; focus must be revealed in its pane.
            for (const auto& element : accepted.inspection.elements) {
                if (element.id == "settings-panes-handle-label") continue;
                require(element.geometry.has_value() && element.geometry->visible, "Missing visible geometry");
                const auto& box = element.geometry->border_box;
                require((box.size.width > 0 ||
                         (element.id == "volume-fill" && host.draft.volume == volume_range.minimum)) &&
                            box.size.height > 0,
                        "Empty element geometry");
            }
            if (const auto target = take(focus.refresh(accepted.hit())).focused)
                require(take(scroll_into_view(accepted.hit(), *target)).empty(), "Focused control is clipped");
            const auto image = capture();
            require(capture() == image, "Repeated CPU capture differs");
            std::cout << name(accepted, "selected-section") << '\n'
                      << name(accepted, "summary") << '\n'
                      << name(accepted, "save-result") << '\n';
            return image;
        };
        std::int64_t tick = 0;
        auto send = [&](auto event, CommandSource source = CommandSource::keyboard) {
            input({std::chrono::microseconds{++tick}, std::move(event)}, source);
        };
        auto observations = [&] {
            return std::tuple{host.selected,
                              host.username,
                              host.draft,
                              host.applied,
                              host.saves,
                              host.result,
                              sequence,
                              accepted.commands.generation(),
                              accepted.inspection,
                              accepted.paint,
                              take(focus.refresh(accepted.hit())).focused};
        };
        auto ignore_editing = [&] {
            const auto before = observations();
            send(tessera::TextInput{"ignored"});
            for (const auto key : {Key::left, Key::right, Key::home, Key::end, Key::backspace, Key::delete_key})
                send(KeyDown{key});
            send(KeyDown{Key::a, {.control = true}});
            require(observations() == before, "Absent username consumed editing input");
        };
        // Before the first Apply, Cancel restores initial values even with Account absent.
        const auto initial_edit = host.username;
        for (const auto* id :
             {"section-account", "section-feature", "section-about", "settings-panes-handle", "username"}) {
            send(FocusNext{});
            focused(id);
        }
        send(tessera::TextInput{"discard日😀"});
        send(FocusPrevious{});
        send(FocusPrevious{});
        send(FocusPrevious{});
        focused("section-feature");
        send(Activate{});
        send(FocusNext{});
        send(FocusNext{});
        send(FocusNext{});
        focused("toggle");
        send(Activate{});
        require(host.draft != host.applied && host.draft.feature, "Initial pending edits missing");
        (void)observe();
        const auto before_initial_cancel = host.username;
        send(FocusNext{});
        focused("theme-trigger");
        send(FocusNext{});
        focused("apply");
        send(FocusNext{});
        focused("cancel");
        send(Activate{});
        focused("cancel");
        require(host.selected == "feature" && host.draft == SettingsData{"Ada", false} && host.applied == host.draft &&
                    host.saves.empty() && host.result == "Not saved" && host.username.owner == initial_edit.owner &&
                    host.username.text == "Ada" && host.username.revision == before_initial_cancel.revision + 2 &&
                    host.username.anchor == 3 && host.username.caret == 3 && sequence == 4,
                "Initial Cancel changed selection, saved values or edit lifetime");
        (void)observe();
        focus.reset();
        send(FocusNext{});
        send(Activate{});
        focused("section-account");
        focus.reset();
        const auto command_start = sequence, edit_start = host.username.revision;
        const auto initial = observe();
        frame_observer("settings-initial", canvas, accepted.paint);
        const auto unfocused = observations();
        send(KeyDown{Key::a, {.control = true}});
        require(observations() == unfocused, "Unfocused username consumed Select All");
        for (const auto* id :
             {"section-account", "section-feature", "section-about", "settings-panes-handle", "username"}) {
            send(FocusNext{});
            focused(id);
        }
        send(tessera::TextInput{"本"});
        focused("username");
        require(host.username.text == "Ada本" && host.username.anchor == 6 && host.username.caret == 6 &&
                    host.username.revision == edit_start + 1 && host.saves.empty(),
                "First edit differs or saved prematurely");
        const auto retained_edit = host.username;
        const auto edited_account = observe();
        require(edited_account != initial, "Editing did not change pixels");
        send(FocusPrevious{});
        focused("settings-panes-handle");
        send(FocusPrevious{});
        focused("section-about");
        send(FocusPrevious{});
        focused("section-feature");
        send(Activate{});
        focused("section-feature");
        require(host.selected == "feature" && host.username == retained_edit &&
                    host.draft == SettingsData{"Ada本", false} && host.saves.empty(),
                "Feature selection changed the draft or saved");
        require(observe() != edited_account, "Branch selection did not change pixels");
        ignore_editing(); // Absent username while a section choice holds focus.
        send(FocusNext{});
        focused("section-about");
        send(FocusNext{});
        focused("settings-panes-handle");
        send(FocusNext{});
        focused("toggle");
        send(Activate{});
        focused("toggle");
        require(host.saves.empty(), "Toggle saved prematurely");
        (void)observe();
        send(FocusNext{});
        focused("theme-trigger");
        send(FocusNext{});
        focused("apply");
        send(Activate{}, CommandSource::gamepad);
        focused("apply");
        require(host.saves == std::vector<SettingsData>{{"Ada本", true}}, "First Apply differs");
        const auto first_saved = observe();
        send(FocusPrevious{});
        focused("theme-trigger");
        send(FocusPrevious{});
        focused("toggle");
        send(FocusPrevious{});
        focused("settings-panes-handle");
        send(FocusPrevious{});
        focused("section-about");
        send(Activate{}, CommandSource::gamepad);
        focused("section-about");
        require(host.selected == "about", "About selection differs");
        const auto about = observe();
        require(about != first_saved, "About branch did not change pixels");
        focus.reset(); // Exercise absent owner plus empty focus, as well as focused absence above.
        ignore_editing();
        send(FocusNext{});
        focused("section-account");
        send(Activate{});
        focused("section-account");
        require(host.selected == "account" && host.username == retained_edit &&
                    host.draft == SettingsData{"Ada本", true} &&
                    host.saves == std::vector<SettingsData>{{"Ada本", true}} &&
                    host.result == "Saved 1: Ada本; feature enabled; theme system; volume 50",
                "Branch return changed retained observations");
        const auto returned_account = observe();
        require(returned_account != about, "Account return did not change pixels");
        send(FocusNext{});
        focused("section-feature");
        send(FocusNext{});
        focused("section-about");
        send(FocusNext{});
        focused("settings-panes-handle");
        send(FocusNext{});
        focused("username");
        send(tessera::TextInput{"!"});
        focused("username");
        require(host.draft == SettingsData{"Ada本!", true} && host.username.revision == edit_start + 2 &&
                    host.saves == std::vector<SettingsData>{{"Ada本", true}} &&
                    host.result == "Saved 1: Ada本; feature enabled; theme system; volume 50",
                "Later edit changed the owned save");
        require(observe() != returned_account, "Later draft edit did not change pixels");
        send(FocusNext{});
        focused("theme-trigger");
        send(FocusNext{});
        focused("apply");
        send(Activate{});
        focused("apply");
        require(host.saves == std::vector<SettingsData>{{"Ada本", true}, {"Ada本!", true}} &&
                    sequence == command_start + 8,
                "Second Apply or once-only command count differs");
        (void)observe();
        const auto apply_focused = observations();
        send(KeyDown{Key::a, {.control = true}});
        require(observations() == apply_focused, "Apply focus consumed Select All");
        send(FocusPrevious{});
        focused("theme-trigger");
        send(FocusPrevious{});
        focused("username");
        send(KeyDown{Key::home, {.shift = true}});
        require(host.username.anchor == 7 && host.username.caret == 0, "Expected reversed selection");
        const auto before_select = host.username;
        const auto ignored_keys = observations();
        for (const auto key :
             {KeyDown{Key::a}, KeyDown{Key::a, {.shift = true, .control = true}},
              KeyDown{Key::a, {.control = true, .alt = true}}, KeyDown{Key::a, {.control = true, .super = true}},
              KeyDown{Key::a, {.control = true}, true}})
            send(key);
        require(observations() == ignored_keys, "Unsupported Select All chord changed observations");
        send(KeyDown{Key::a, {.control = true}});
        focused("username");
        require(host.username.owner == before_select.owner && host.username.revision == before_select.revision + 1 &&
                    host.username.text == before_select.text && host.username.anchor == 0 && host.username.caret == 7 &&
                    host.draft == SettingsData{"Ada本!", true} && host.saves.size() == 2 &&
                    sequence == command_start + 10,
                "Select All did not publish one full UTF-8 selection command");
        const auto selected_all = observe();
        send(tessera::TextInput{"日😀"});
        focused("username");
        require(host.username.text == "日😀" && host.username.anchor == 7 && host.username.caret == 7 &&
                    host.username.revision == before_select.revision + 2 && host.draft == SettingsData{"日😀", true} &&
                    host.saves == std::vector<SettingsData>{{"Ada本", true}, {"Ada本!", true}} &&
                    host.result == "Saved 2: Ada本!; feature enabled; theme system; volume 50",
                "Replacement changed a saved copy or split UTF-8");
        require(observe() != selected_all, "Whole-field replacement did not change pixels");
        send(FocusNext{});
        focused("theme-trigger");
        send(FocusNext{});
        focused("apply");
        send(Activate{});
        focused("apply");
        require(host.saves == std::vector<SettingsData>{{"Ada本", true}, {"Ada本!", true}, {"日😀", true}} &&
                    host.applied == host.draft && sequence == command_start + 12,
                "Replacement Apply or once-only command count differs");
        (void)observe();
        // Cancel both fields against the latest Apply, retaining all earlier save copies.
        const auto saved_copies = host.saves;
        const auto saved_result = host.result;
        send(FocusPrevious{});
        focused("theme-trigger");
        send(FocusPrevious{});
        focused("username");
        send(tessera::TextInput{"!"});
        send(FocusPrevious{});
        send(FocusPrevious{});
        send(FocusPrevious{});
        focused("section-feature");
        send(Activate{});
        send(FocusNext{});
        send(FocusNext{});
        send(FocusNext{});
        focused("toggle");
        send(Activate{});
        require(host.draft == SettingsData{"日😀!", false}, "Later pending edits missing");
        (void)observe();
        const auto before_cancel = host.username;
        send(FocusNext{});
        focused("theme-trigger");
        send(FocusNext{});
        send(FocusNext{});
        focused("cancel");
        send(Activate{}, CommandSource::gamepad);
        focused("cancel");
        require(host.selected == "feature" && host.draft == SettingsData{"日😀", true} && host.applied == host.draft &&
                    host.saves == saved_copies && host.result == saved_result &&
                    host.username.owner == before_cancel.owner && host.username.text == "日😀" &&
                    host.username.revision == before_cancel.revision + 2 && host.username.anchor == 7 &&
                    host.username.caret == 7 && sequence == command_start + 16,
                "Cancel did not restore the latest Apply or changed unrelated state");
        (void)observe();
        focus.reset();
        send(FocusNext{});
        send(Activate{});
        focused("section-account");
        require(host.draft == host.applied && host.saves == saved_copies && host.result == saved_result,
                "Returning to Account lost cancelled values");
        (void)observe();
        // Wheel input preserves focus and application data; each pane owns its offset.
        auto box = [&](const char* id) -> const LayoutBox& { return box_for(accepted, *accepted.tree->find(id)); };
        auto center = [](Rect rect) {
            return Point{rect.origin.x + rect.size.width / 2, rect.origin.y + rect.size.height / 2};
        };
        const auto before_wheel =
            std::tuple{host.selected, host.username, host.draft, host.applied, host.saves, host.result, sequence};
        for (const auto* pane : {"categories", "settings-form"}) {
            const auto other = std::string(pane) == "categories" ? "settings-form" : "categories";
            const auto other_offset = offsets[other];
            send(Scroll{center(box(pane).padding_box()), {0, 4096}});
            require(offsets[pane] == box(pane).scroll_limit() && offsets[pane].y > 0 && offsets[other] == other_offset,
                    "Wheel did not clamp the intended pane independently");
            require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find("section-account"),
                    "Wheel changed logical focus"); // Wheel deliberately does not reveal focus.
            send(Scroll{center(box(pane).padding_box()), {0, -4096}});
            require(offsets[pane] == Point{} && offsets[other] == other_offset,
                    "Wheel did not return the pane to zero");
        }
        require(before_wheel == std::tuple{host.selected, host.username, host.draft, host.applied, host.saves,
                                           host.result, sequence},
                "Wheel changed application data or executed a command");
        // Reveal About by wheel, then activate it through clipped hit testing.
        send(Scroll{center(box("categories").padding_box()), {0, 4096}});
        send(Scroll{center(box("settings-form").padding_box()), {0, 4096}});
        const auto account_bottom = offsets["settings-form"];
        auto click = [&](const char* id) {
            const auto& target = box(id);
            const auto visible = target.clip ? intersect(target.border_box, *target.clip) : target.border_box;
            require(visible.size.width > 0 && visible.size.height > 0, "Pointer control is clipped");
            const auto position = center(visible);
            const auto before = sequence;
            send(PointerDown{{1}, position}, CommandSource::pointer);
            require(sequence == before, "Pointer press executed a command");
            send(PointerUp{{1}, position}, CommandSource::pointer);
            require(sequence == before + 1, "Pointer release must execute one command");
        };
        click("section-about");
        require(host.selected == "about" && host.username.text == "日😀" && host.saves == saved_copies,
                "Pointer category selection changed retained state");
        require(offsets["settings-form"].y < account_bottom.y &&
                    offsets["settings-form"] == box("settings-form").scroll_limit(),
                "Shorter branch did not clamp the retained form offset");
        focused("section-about");
        send(Scroll{center(box("categories").padding_box()), {0, -4096}});
        click("section-account");
        require(host.selected == "account" && host.draft == host.applied && host.saves == saved_copies &&
                    host.result == saved_result,
                "Pointer category return lost retained state");
        (void)observe();
        // Dropdown commands retain independent edits and save only through Apply.
        auto seek = [&](const char* id) {
            for (int i = 0; i < 16; ++i) {
                const auto owner = accepted.tree->find(id);
                require(owner.has_value(), "Missing navigation target");
                if (take(focus.refresh(accepted.hit())).focused == owner) {
                    focused(id);
                    return;
                }
                send(FocusNext{});
            }
            require(false, "Control unreachable by keyboard");
        };
        seek("username");
        send(tessera::TextInput{"?"});
        const auto pending_username = host.username;
        const auto before_theme = host.draft;
        seek("theme-trigger");
        send(Activate{});
        focused("theme-trigger");
        require(host.theme_open && name(accepted, "theme-option-system") == "* System", "Dropdown did not open");
        frame_observer("settings-dropdown", canvas, accepted.paint);
        (void)observe();
        send(FocusNext{});
        focused("theme-option-system");
        send(Cancel{});
        focused("theme-trigger");
        require(!host.theme_open && host.draft == before_theme, "Dismissal changed the draft");
        send(Activate{});
        const auto before_ignored_escape = observations();
        send(KeyDown{Key::escape, {.control = true}});
        send(KeyDown{Key::escape, {}, true});
        require(observations() == before_ignored_escape, "Unsupported Escape chord changed the Dropdown");
        send(KeyDown{Key::escape});
        focused("theme-trigger");
        require(!host.theme_open && host.draft == before_theme && host.saves == saved_copies,
                "Escape dismissal changed pending values or saves");
        send(Activate{}, CommandSource::gamepad);
        seek("theme-option-dark");
        send(Activate{}, CommandSource::gamepad);
        focused("theme-trigger");
        require(!host.theme_open && host.draft.theme == "dark" && host.username == pending_username &&
                    host.draft.username == before_theme.username && host.draft.feature == before_theme.feature &&
                    host.saves == saved_copies && host.result == saved_result,
                "Theme selection changed independent observations");
        (void)observe();
        seek("section-about");
        send(Activate{});
        require(host.draft.theme == "dark" && host.username == pending_username,
                "Category replacement lost the theme draft");
        seek("theme-trigger");
        send(Activate{});
        seek("theme-option-light");
        click("theme-option-light");
        focused("theme-trigger");
        require(host.draft.theme == "light" && !host.theme_open && host.saves == saved_copies,
                "Pointer choice saved prematurely");
        seek("apply");
        send(Activate{});
        const auto theme_applied = host.draft;
        const auto theme_saves = host.saves;
        const auto theme_result = host.result;
        require(theme_saves.size() == saved_copies.size() + 1 && theme_saves.back() == theme_applied &&
                    theme_applied.theme == "light" &&
                    std::equal(saved_copies.begin(), saved_copies.end(), theme_saves.begin()),
                "Apply did not copy the selected theme exactly once");
        seek("theme-trigger");
        send(Activate{});
        seek("theme-option-dark");
        send(Activate{});
        seek("cancel");
        send(Activate{});
        require(host.selected == "about" && host.draft == theme_applied && host.saves == theme_saves &&
                    host.result == theme_result && host.draft.theme == "light",
                "Cancel did not restore the applied theme");
        (void)observe();
        // Numeric editing shares the same candidate/Apply/Cancel boundary.
        const auto before_volume = std::tuple{host.selected, host.username, host.draft, host.saves, host.result};
        const auto unfocused_volume = observations();
        send(KeyDown{Key::end});
        require(observations() == unfocused_volume, "Unfocused Slider consumed a key");
        seek("volume");
        const auto initial_volume_image = observe();
        const auto ignored_volume = observations();
        send(KeyDown{Key::left, {.control = true}});
        send(KeyDown{Key::home, {.shift = true}});
        require(observations() == ignored_volume, "Modified Slider key changed state");
        const auto volume_start = sequence;
        send(KeyDown{Key::home});
        focused("volume");
        require(host.draft.volume == 0, "Slider Home did not reach minimum");
        const auto minimum = observations();
        send(KeyDown{Key::left});
        send(KeyDown{Key::down, {}, true});
        require(observations() == minimum, "Slider underflow executed a command");
        require(observe() != initial_volume_image, "Slider change did not change visible pixels");
        frame_observer("settings-slider", canvas, accepted.paint);
        send(KeyDown{Key::end});
        require(host.draft.volume == 100, "Slider End did not reach maximum");
        const auto maximum = observations();
        send(KeyDown{Key::right});
        send(KeyDown{Key::up, {}, true});
        require(observations() == maximum, "Slider overflow executed a command");
        send(KeyDown{Key::left, {}, true});
        require(host.draft.volume == 95, "Slider repeat did not move one step");
        send(KeyDown{Key::right});
        require(host.draft.volume == 100 && sequence == volume_start + 4, "Slider step command count differs");
        auto track_point = [&](float fraction) {
            const auto& track = box("volume-track");
            const auto visible = track.clip ? intersect(track.border_box, *track.clip) : track.border_box;
            require(visible.size.height > 0, "Slider track is clipped");
            return Point{track.border_box.origin.x + 4 + fraction * (track.border_box.size.width - 8),
                         visible.origin.y + visible.size.height / 2};
        };
        const auto before_secondary = observations();
        send(PointerDown{{7}, track_point(.5f), PointerButton::secondary}, CommandSource::pointer);
        send(PointerUp{{7}, track_point(.5f), PointerButton::secondary}, CommandSource::pointer);
        require(observations() == before_secondary, "Secondary pointer changed Slider");
        send(PointerDown{{7}, track_point(.5f)}, CommandSource::pointer);
        require(host.draft.volume == 50, "Track press did not set nearest value");
        const auto captured = observations();
        send(PointerMove{{8}, track_point(0)}, CommandSource::pointer);
        send(PointerUp{{7}, track_point(0), PointerButton::secondary}, CommandSource::pointer);
        require(observations() == captured, "Foreign pointer/secondary release changed capture");
        send(PointerMove{{7}, track_point(2)}, CommandSource::pointer);
        require(host.draft.volume == 100, "Captured drag did not clamp beyond the track");
        send(PointerMove{{7}, track_point(-1)}, CommandSource::pointer);
        require(host.draft.volume == 0, "Captured drag lost ownership after tree replacement");
        send(PointerCancel{{7}}, CommandSource::pointer);
        const auto cancelled_drag = observations();
        send(PointerMove{{7}, track_point(1)}, CommandSource::pointer);
        send(PointerUp{{7}, track_point(1)}, CommandSource::pointer);
        require(observations() == cancelled_drag, "Cancelled drag executed a deferred edit");
        send(PointerDown{{7}, track_point(.5f)}, CommandSource::pointer);
        send(PointerUp{{7}, track_point(.75f)}, CommandSource::pointer);
        require(host.draft.volume == 75 && host.selected == std::get<0>(before_volume) &&
                    host.username == std::get<1>(before_volume) &&
                    host.draft.username == std::get<2>(before_volume).username &&
                    host.draft.feature == std::get<2>(before_volume).feature &&
                    host.draft.theme == std::get<2>(before_volume).theme && host.saves == std::get<3>(before_volume) &&
                    host.result == std::get<4>(before_volume),
                "Slider changed independent settings or saved prematurely");
        (void)observe();
        const auto before_invalid_volume = observations();
        auto invalid_volume = stage(host.selected, offsets, focus, true, host.draft.theme, host.theme_open, 101);
        require(!invalid_volume && invalid_volume.diagnostics.front().code == "slider_value" &&
                    observations() == before_invalid_volume,
                "Invalid numeric candidate changed accepted state");
        seek("apply");
        send(Activate{}, CommandSource::gamepad);
        const auto volume_applied = host.draft;
        const auto volume_saves = host.saves;
        const auto volume_result = host.result;
        require(volume_saves.size() == theme_saves.size() + 1 && volume_saves.back().volume == 75 &&
                    std::equal(theme_saves.begin(), theme_saves.end(), volume_saves.begin()),
                "Apply did not copy volume once");
        seek("volume");
        send(KeyDown{Key::left});
        seek("section-account");
        send(Activate{});
        seek("username");
        send(tessera::TextInput{"^"});
        seek("section-feature");
        send(Activate{});
        require(host.draft.volume == 70 && host.draft.username != volume_applied.username,
                "Category replacement lost pending numeric/text edits");
        seek("cancel");
        send(Activate{});
        require(host.selected == "feature" && host.draft == volume_applied && host.saves == volume_saves &&
                    host.result == volume_result && host.username.text == volume_applied.username,
                "Cancel did not restore volume and text while retaining category and saves");
        (void)observe();
        seek("section-account");
        send(Activate{});
        seek("username");
        send(tessera::TextInput{"split"});
        seek("settings-panes-handle");
        const auto split_retained =
            std::tuple{host.selected, host.username, host.draft, host.applied, host.saves, host.result, offsets};
        const auto before_split_image = capture();
        auto split_center = [&] { return center(take(split_pane_geometry(accepted.hit(), "settings-panes"))->bounds); };
        auto split_position = split_center();
        send(PointerDown{{9}, split_position}, CommandSource::pointer);
        require(splitter.dragging(), "Settings splitter did not capture");
        split_position.x += 32;
        send(PointerMove{{9}, split_position}, CommandSource::pointer);
        require(host.sidebar_size == 172 && splitter.dragging() && capture() != before_split_image,
                "Settings split did not resize visible panes through replacement");
        frame_observer("settings-split-dragged", canvas, accepted.paint);
        send(PointerCancel{{9}}, CommandSource::pointer);
        split_position.x += 32;
        send(PointerUp{{9}, split_position}, CommandSource::pointer);
        require(host.sidebar_size == 172 && !splitter.dragging(), "Settings cancelled drag applied a release");
        send(KeyDown{Key::left});
        require(host.sidebar_size == 164, "Settings splitter keyboard step differs");
        send(Navigate{Direction::right}, CommandSource::gamepad);
        require(host.sidebar_size == 172 &&
                    split_retained == std::tuple{host.selected, host.username, host.draft, host.applied, host.saves,
                                                 host.result, offsets},
                "Settings splitter changed pending edits, category, scrolling or Apply/Cancel state");
        canvas.width = 420;
        auto narrow =
            take(stage(host.selected, offsets, focus, true, host.draft.theme, host.theme_open, host.draft.volume));
        session.publish();
        accept(std::move(narrow));
        require(host.sidebar_size == 148, "Settings parent resize did not clamp its canonical split");
        frame_observer("settings-narrow", canvas, accepted.paint);
        canvas = viewport;
        auto wide =
            take(stage(host.selected, offsets, focus, true, host.draft.theme, host.theme_open, host.draft.volume));
        session.publish();
        accept(std::move(wide));
        require(host.sidebar_size == 148 && host.username == std::get<1>(split_retained),
                "Settings growth restored an old split or lost text");
        const auto before_split_apply = host.draft;
        seek("apply");
        send(Activate{});
        require(host.saves.back() == before_split_apply && host.saves.size() == volume_saves.size() + 1 &&
                    host.sidebar_size == 148,
                "Apply lost the split or failed to save the independent draft");
        seek("username");
        send(tessera::TextInput{"cancel"});
        seek("cancel");
        send(Activate{});
        require(host.draft == before_split_apply && host.sidebar_size == 148, "Cancel restored a stale split");
        frame_observer("settings-restored", canvas, accepted.paint);
        (void)observe();
        std::cout << host.saves.size() << " owned saves; " << sequence << " host commands; " << extent.width << 'x'
                  << extent.height << " RGBA8 capture in memory (placeholder text, synthetic glyphs; no window)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
