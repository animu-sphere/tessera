#include <tessera/ui/authoring.hpp>
#include <tessera/commands/execution.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/inspection/inspection.hpp>
#include <tessera/render/cpu_renderer.hpp>
#include <tessera/render/paint.hpp>
#include <iostream>
#include <stdexcept>
#include <tuple>

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
    return {{
        {StyleSelector::of_class("form"), {.padding = Edges{12, 12, 12, 12}, .gap = 12.f,
            .background = Color{0.95f, 0.95f, 0.98f, 1}}},
        {StyleSelector::of_class("control"), {.padding = Edges{4, 8, 4, 8},
            .background = Color{0.8f, 0.9f, 1, 1}}},
        {StyleSelector::of_class("status"), {.color = Color{0.25f, 0.3f, 0.4f, 1}}}}};
}
struct FieldProps { std::string label; float gap = 12; };
ui::Element Field(FieldProps props, std::vector<ui::Element> children) {
    return ui::Row({ui::Text(std::move(props.label)), ui::Column(std::move(children))},
        {.style = {.gap = props.gap}});
}
const std::vector<std::string> sections{"account", "feature", "about"};
ui::Element Settings(const std::string& selected, ui::Value<TextEditState> username, ui::Value<bool> enabled,
    ui::Value<std::string> summary, ui::Value<std::string> saved) {
    std::vector<ui::Element> choices;
    for (const auto& id : sections)
        choices.push_back(ui::Button((selected == id ? "* " : "") + id, "settings.section." + id,
            {.node = {.id = "section-" + id, .classes = {"control"}}}));
    std::vector<ui::Element> rows{
        ui::Text("Settings"),
        ui::Row(std::move(choices), {.style = {.gap = 12.f}}),
        ui::Text("Section " + selected, {.id = "selected-section", .classes = {"status"}})};
    if (selected == "account")
        rows.push_back(Field({"Account"}, {ui::TextInput("Username", username, "settings.edit",
            {.node = {.id = "username", .classes = {"control"}}})}));
    else if (selected == "feature")
        rows.push_back(Field({"Feature"}, {ui::Checkbox("Enable feature", enabled, "settings.toggle",
            {.node = {.id = "toggle", .classes = {"control"}}})}));
    else rows.push_back(Field({"About"}, {ui::Text("Apply saves the whole draft.", {.id = "section-help"})}));
    rows.push_back(ui::Text(summary, {.id = "summary", .classes = {"status"}}));
    rows.push_back(ui::Button("Apply", "settings.apply", {.node = {.id = "apply", .classes = {"control"},
        .style = {.background = Color{0.6f, 0.75f, 0.95f, 1}}}}));
    rows.push_back(ui::Text(saved, {.id = "save-result", .classes = {"status"}}));
    return ui::Column(std::move(rows), {.id = "settings", .classes = {"form"}});
}

// Host state and synchronous in-memory saves own values, never UI references.
struct SettingsData {
    std::string username;
    bool feature = false;
    bool operator==(const SettingsData&) const = default;
};
std::string saved_caption(std::size_t count, const SettingsData& data) {
    return "Saved " + std::to_string(count) + ": " + data.username +
        (data.feature ? "; feature enabled" : "; feature disabled");
}
struct Host final : CommandHost {
    TextEditState username = take(make_text_edit_state(1, "Ada"));
    SettingsData draft{username.text, false};
    std::vector<SettingsData> saves;
    std::string result = "Not saved";
    std::string selected = "account", next_selected;
    std::optional<TextEditRequest> edit;
    TextEditState next_username;
    SettingsData next_draft;
    std::string next_result;
    bool applying = false;

    CommandOutcome execute(const CommandInvocationRecord& invocation) override {
        next_username = username; next_draft = draft; next_result = result;
        next_selected = selected;
        applying = invocation.command == "settings.apply";
        if (invocation.command == "settings.edit") {
            require(edit.has_value(), "Editing requires an owned request");
            auto candidate = edit_text(username, *edit);
            if (!candidate) return {CommandOutcomeStatus::error, {}, std::move(candidate.diagnostics)};
            next_username = std::move(*candidate.value);
            next_draft.username = next_username.text;
        } else if (invocation.command == "settings.toggle") next_draft.feature = !draft.feature;
        else if (invocation.command.starts_with("settings.section.")) next_selected = invocation.command.substr(17);
        else {
            require(applying, "Unexpected command");
            next_result = saved_caption(saves.size() + 1, next_draft);
        }
        return {};
    }
};

constexpr Size viewport{640, 360};
const ValidationContext actions{{"settings.edit", "settings.toggle", "settings.apply",
    "settings.section.account", "settings.section.feature", "settings.section.about"}};
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
// Every borrowed input is used synchronously; returned observations are owned.
Result<Presentation> prepare(ui::Session& session, const ui::Element& form,
    CommandSnapshot commands, const StyleSheet& sheet, TextShaper& text) {
    auto authored = session.lower(form, actions);
    if (!authored) return {std::nullopt, std::move(authored.diagnostics)};
    auto document = commands.apply_eligibility(authored.value->document, actions);
    if (!document) return {std::nullopt, std::move(document.diagnostics)};
    auto tree = UiTree::create(*document.value, actions);
    if (!tree) return {std::nullopt, std::move(tree.diagnostics)};
    auto styles = resolve_styles({tree.value->get(), &sheet, {}, authored.value->styles});
    if (!styles) return {std::nullopt, std::move(styles.diagnostics)};
    auto layout = compute_layout({tree.value->get(), *styles.value, viewport, &text});
    if (!layout) return {std::nullopt, std::move(layout.diagnostics)};
    auto inspection = capture_inspection({tree.value->get(), *styles.value, &*layout.value, &authored.value->sources});
    if (!inspection) return {std::nullopt, std::move(inspection.diagnostics)};
    auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &text});
    if (!paint) return {std::nullopt, std::move(paint.diagnostics)};
    return {Presentation{std::move(commands), std::move(*tree.value), std::move(*styles.value),
        std::move(*layout.value), std::move(*inspection.value), std::move(*paint.value)}, {}};
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

int main() {
    try {
        ui::Session session;
        Host host;
        const auto username = session.state("username", host.username);
        const auto enabled = session.state("enabled", host.draft.feature);
        const auto saved = session.state("save-result", host.result);
        const auto summary = session.computed("summary", [username, enabled] {
            return "User " + username.read().text + (enabled.read() ? "; feature enabled" : "; feature disabled");
        });
        auto form = [&](const std::string& selected) { return Settings(selected, username, enabled, summary, saved); };
        const auto sheet = application_styles();
        const auto registry = take(CommandRegistry::create(
            {{"settings.edit", "Edit username"}, {"settings.toggle", "Toggle feature"}, {"settings.apply", "Apply settings"},
                {"settings.section.account", "Select Account"}, {"settings.section.feature", "Select Feature"},
                {"settings.section.about", "Select About"}},
            {{"settings.edit", "settings.edit"}, {"settings.toggle", "settings.toggle"}, {"settings.apply", "settings.apply"},
                {"settings.section.account", "settings.section.account"}, {"settings.section.feature", "settings.section.feature"},
                {"settings.section.about", "settings.section.about"}}));
        std::uint64_t generation = 0, sequence = 0;
        auto commands = [&] {
            return take(CommandSnapshot::publish(registry, ++generation, CommandMode::production,
                {{"settings.edit", {true}}, {"settings.toggle", {true}}, {"settings.apply", {true}},
                    {"settings.section.account", {true}}, {"settings.section.feature", {true}}, {"settings.section.about", {true}}}));
        };
        PlaceholderTextShaper text;
        auto accepted = take(prepare(session, form(host.selected), commands(), sheet, text));
        session.publish();
        FocusDispatcher focus;
        CommandExecutor executor;
        auto focused = [&](const char* id) {
            require(take(focus.refresh(accepted.hit())).focused == accepted.tree->find(id), "Unexpected focus");
        };
        auto operate = [&](const ActionRequest& action, CommandSource source, std::optional<TextEditRequest> edit = {}) {
            const auto current = accepted.semantic();
            const auto invocation = take(accepted.commands.resolve_action(current, action,
                accepted.commands.generation(), {}, source, ++sequence));
            host.edit = std::move(edit);
            const auto execution = take(executor.execute(accepted.commands, invocation, host, &current));
            host.edit.reset();
            if (execution.outcome.status != CommandOutcomeStatus::success) {
                (void)take(Result<Presentation>{std::nullopt, execution.outcome.diagnostics});
            }
            session.update([&] {
                username.write(host.next_username); enabled.write(host.next_draft.feature); saved.write(host.next_result);
            });
            auto candidate = prepare(session, form(host.next_selected), commands(), sheet, text);
            if (!candidate) {
                // Returned stage failure: restore observations, retain host values,
                // save records and presentation, then exit with diagnostics.
                session.update([&] {
                    username.write(host.username); enabled.write(host.draft.feature); saved.write(host.result);
                });
                (void)take(std::move(candidate));
            }
            if (host.applying) host.saves.push_back(host.next_draft); // Owned synchronous copy, once.
            host.username = std::move(host.next_username);
            host.draft = std::move(host.next_draft); host.result = std::move(host.next_result);
            host.selected = std::move(host.next_selected);
            session.publish(); accepted = std::move(*candidate.value);
            (void)take(focus.refresh(accepted.hit())); // Recover by explicit author ID.
        };
        auto input = [&](InputEvent event, CommandSource source = CommandSource::keyboard) {
            require(validate(event).empty(), "Invalid normalized input");
            std::optional<decltype(TextEditRequest::operation)> edit;
            const auto owner = accepted.tree->find("username");
            const auto current_focus = take(focus.refresh(accepted.hit())).focused;
            if (owner && current_focus == owner) { // An absent owner must never match an empty focus.
                if (const auto* insert = std::get_if<tessera::TextInput>(&event.data)) edit = TextEditInsert{insert->text};
                if (const auto* key = std::get_if<KeyDown>(&event.data);
                    key && !key->modifiers.control && !key->modifiers.alt && !key->modifiers.super) {
                    switch (key->key) {
                    case Key::left: edit = TextEditMove{TextMove::left, key->modifiers.shift}; break;
                    case Key::right: edit = TextEditMove{TextMove::right, key->modifiers.shift}; break;
                    case Key::home: edit = TextEditMove{TextMove::home, key->modifiers.shift}; break;
                    case Key::end: edit = TextEditMove{TextMove::end, key->modifiers.shift}; break;
                    case Key::backspace: edit = TextEditErase{TextErase::backward}; break;
                    case Key::delete_key: edit = TextEditErase{TextErase::forward}; break;
                    default: break;
                    }
                }
            }
            if (edit) {
                const auto action = take(request_semantic_action(accepted.semantic(), *accepted.tree->find("username"), "activate"));
                operate(action, source, TextEditRequest{host.username.owner, host.username.revision, std::move(*edit)});
                return; // Supported editing keys precede logical navigation.
            }
            // Other raw keys/text have no mapping in this bounded host.
            if (std::holds_alternative<KeyDown>(event.data) || std::holds_alternative<tessera::TextInput>(event.data)) return;
            const auto dispatched = take(focus.dispatch(accepted.hit(), event));
            for (const auto& action : dispatched.actions) operate(action, source);
        };

        SyntheticGlyphs raster;
        GlyphCache cache(raster);
        CpuFrameCapture frames(&cache);
        const auto extent = take(capture_extent(viewport, 1));
        auto capture = [&] { return take(frames.capture({0, viewport, 1, extent, &accepted.paint, 0us})); };
        auto observe = [&] {
            require(name(accepted, "selected-section") == "Section " + host.selected, "Section name differs");
            for (const auto& id : sections)
                require(name(accepted, "section-" + id) == (host.selected == id ? "* " : "") + id,
                    "Selected marker differs");
            require(accepted.tree->find("username").has_value() == (host.selected == "account") &&
                accepted.tree->find("toggle").has_value() == (host.selected == "feature") &&
                accepted.tree->find("section-help").has_value() == (host.selected == "about"), "Conditional branch differs");
            require(name(accepted, "summary") == "User " + host.draft.username +
                (host.draft.feature ? "; feature enabled" : "; feature disabled"), "Summary differs");
            if (host.selected == "account")
                require(name(accepted, "username") == "Username " + host.username.text + " Selection " +
                    std::to_string(host.username.anchor) + ":" + std::to_string(host.username.caret), "Edit observation differs");
            if (host.selected == "feature")
                require(name(accepted, "toggle") == (host.draft.feature ? "[x] Enable feature" : "[ ] Enable feature"), "Checkbox differs");
            if (host.selected == "about")
                require(name(accepted, "section-help") == "Apply saves the whole draft.", "Help differs");
            require(name(accepted, "save-result") == host.result, "Saved observation differs");
            // This onboarding viewport fits every branch; no reveal/scroll policy is needed.
            for (const auto& element : accepted.inspection.elements) {
                require(element.geometry.has_value() && element.geometry->visible, "Missing visible geometry");
                const auto& box = element.geometry->border_box;
                require(box.size.width > 0 && box.size.height > 0 && box.origin.x >= 0 && box.origin.y >= 0 &&
                    box.origin.x + box.size.width <= viewport.width && box.origin.y + box.size.height <= viewport.height,
                    "Element lies outside the viewport");
            }
            const auto image = capture();
            require(capture() == image, "Repeated CPU capture differs");
            std::cout << name(accepted, "selected-section") << '\n' << name(accepted, "summary") << '\n'
                << name(accepted, "save-result") << '\n';
            return image;
        };
        std::int64_t tick = 0;
        auto send = [&](auto event, CommandSource source = CommandSource::keyboard) {
            input({std::chrono::microseconds{++tick}, std::move(event)}, source);
        };
        auto ignore_editing = [&] {
            auto observations = [&] { return std::tuple{host.selected, host.username, host.draft, host.saves, host.result,
                sequence, accepted.commands.generation(), accepted.inspection, accepted.paint,
                take(focus.refresh(accepted.hit())).focused}; };
            const auto before = observations();
            send(tessera::TextInput{"ignored"});
            for (const auto key : {Key::left, Key::right, Key::home, Key::end, Key::backspace, Key::delete_key})
                send(KeyDown{key});
            require(observations() == before, "Absent username consumed editing input");
        };
        const auto initial = observe();
        for (const auto* id : {"section-account", "section-feature", "section-about", "username"}) {
            send(FocusNext{}); focused(id);
        }
        send(tessera::TextInput{"本"}); focused("username");
        require(host.username.text == "Ada本" && host.username.anchor == 6 && host.username.caret == 6 &&
            host.username.revision == 2 && host.saves.empty(), "First edit differs or saved prematurely");
        const auto retained_edit = host.username;
        const auto edited_account = observe();
        require(edited_account != initial, "Editing did not change pixels");
        send(FocusPrevious{}); focused("section-about");
        send(FocusPrevious{}); focused("section-feature");
        send(Activate{}); focused("section-feature");
        require(host.selected == "feature" && host.username == retained_edit &&
            host.draft == SettingsData{"Ada本", false} && host.saves.empty(), "Feature selection changed the draft or saved");
        require(observe() != edited_account, "Branch selection did not change pixels");
        ignore_editing(); // Absent username while a section choice holds focus.
        send(FocusNext{}); focused("section-about");
        send(FocusNext{}); focused("toggle");
        send(Activate{}); focused("toggle");
        require(host.saves.empty(), "Toggle saved prematurely");
        const auto toggled = observe();
        send(FocusNext{}); focused("apply");
        send(Activate{}, CommandSource::gamepad); focused("apply");
        require(host.saves == std::vector<SettingsData>{{"Ada本", true}}, "First Apply differs");
        const auto first_saved = observe();
        require(first_saved != toggled, "Apply did not change pixels");
        send(FocusPrevious{}); focused("toggle");
        send(FocusPrevious{}); focused("section-about");
        send(Activate{}, CommandSource::gamepad); focused("section-about");
        require(host.selected == "about", "About selection differs");
        const auto about = observe();
        require(about != first_saved, "About branch did not change pixels");
        focus.reset(); // Exercise absent owner plus empty focus, as well as focused absence above.
        ignore_editing();
        send(FocusNext{}); focused("section-account");
        send(Activate{}); focused("section-account");
        require(host.selected == "account" && host.username == retained_edit &&
            host.draft == SettingsData{"Ada本", true} && host.saves == std::vector<SettingsData>{{"Ada本", true}} &&
            host.result == "Saved 1: Ada本; feature enabled", "Branch return changed retained observations");
        const auto returned_account = observe();
        require(returned_account != about, "Account return did not change pixels");
        send(FocusNext{}); focused("section-feature");
        send(FocusNext{}); focused("section-about");
        send(FocusNext{}); focused("username");
        send(tessera::TextInput{"!"}); focused("username");
        require(host.draft == SettingsData{"Ada本!", true} && host.username.revision == 3 &&
            host.saves == std::vector<SettingsData>{{"Ada本", true}} &&
            host.result == "Saved 1: Ada本; feature enabled", "Later edit changed the owned save");
        require(observe() != returned_account, "Later draft edit did not change pixels");
        send(FocusNext{}); focused("apply");
        send(Activate{}); focused("apply");
        require(host.saves == std::vector<SettingsData>{{"Ada本", true}, {"Ada本!", true}} && sequence == 8,
            "Second Apply or once-only command count differs");
        (void)observe();
        std::cout << "2 owned saves; 8 host commands; " << extent.width << 'x' << extent.height
            << " RGBA8 capture in memory (placeholder text, synthetic glyphs; no window)\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
