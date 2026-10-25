#include "host.hpp"
#include <atomic>
#include <charconv>
#include <limits>
#include <cmath>
#include <algorithm>
#include <type_traits>

namespace diagnostics {
using namespace tessera;
namespace {
bool supports(const Msaa& msaa, unsigned samples) {
    return std::find(msaa.supported.begin(), msaa.supported.end(), samples) != msaa.supported.end();
}
bool controls_focus(const std::optional<std::string>& id) {
    return id && (id->starts_with("direct-") || id->starts_with("ambient-") || id->starts_with("msaa-") || *id == "outlines-toggle" || *id == "scene-lights-toggle");
}
Result<View> prepare(authoring::Session& session, const Snapshot& snapshot, const Disclosure& disclosure,
    const CommandSnapshot& commands, Point offset, const Numeric& numeric, const Msaa& msaa, const Outlines& outlines, const SceneLights& scene_lights, const Numeric& ambient, TextShaper& text) {
    auto authored = session.lower(Panel(snapshot, disclosure, numeric, msaa, outlines, scene_lights, ambient), context);
    if (!authored) return {std::nullopt, std::move(authored.diagnostics)};
    auto document = commands.apply_eligibility(authored.value->document, context);
    if (!document) return {std::nullopt, std::move(document.diagnostics)};
    auto tree = UiTree::create(*document.value, context);
    if (!tree) return {std::nullopt, std::move(tree.diagnostics)};
    const auto sheet = panel_styles();
    auto styles = resolve_styles({tree.value->get(), &sheet, {}, authored.value->styles});
    if (!styles) return {std::nullopt, std::move(styles.diagnostics)};
    std::vector<Point> offsets((*tree.value)->size());
    offsets[(*tree.value)->find("renderer-diagnostics")->index] = offset;
    auto layout = compute_layout({tree.value->get(), *styles.value, viewport, &text, offsets});
    if (!layout) return {std::nullopt, std::move(layout.diagnostics)};
    auto inspection = capture_inspection({tree.value->get(), *styles.value, &*layout.value, &authored.value->sources});
    if (!inspection) return {std::nullopt, std::move(inspection.diagnostics)};
    auto paint = build_paint_list({tree.value->get(), *styles.value, &*layout.value, &text});
    if (!paint) return {std::nullopt, std::move(paint.diagnostics)};
    return {View{commands, std::move(*tree.value), std::move(*styles.value), std::move(*layout.value),
        std::move(*inspection.value), std::move(*paint.value)}, {}};
}
std::vector<Diagnostic> bounded_diagnostics(std::vector<Diagnostic> diagnostics) {
    if (diagnostics.size() > max_command_result_diagnostics) {
        const auto omitted = diagnostics.size() - (max_command_result_diagnostics - 1);
        diagnostics.resize(max_command_result_diagnostics - 1);
        diagnostics.push_back({"diagnostics_truncated", Severity::error, "/presentation",
            std::to_string(omitted) + " further presentation diagnostics omitted"});
    }
    return diagnostics;
}
std::vector<Diagnostic> numeric_diagnostics(std::vector<Diagnostic> diagnostics, NumericField field) {
    const auto path = "/controls/" + field_name(field);
    for (auto& diagnostic : diagnostics)
        if (!diagnostic.path.starts_with(path)) diagnostic.path = path + diagnostic.path;
    return bounded_diagnostics(std::move(diagnostics));
}
CommandOutcome command_error(std::vector<Diagnostic> diagnostics) {
    return {CommandOutcomeStatus::error, {}, bounded_diagnostics(std::move(diagnostics))};
}
Result<bool> completed(Result<CommandExecution> execution) {
    if (!execution) return {std::nullopt, std::move(execution.diagnostics)};
    if (execution.value->outcome.status != CommandOutcomeStatus::success)
        return {std::nullopt, std::move(execution.value->outcome.diagnostics)};
    return {true, {}};
}
}


Result<std::unique_ptr<Host>> Host::open(Snapshot snapshot, TextShaper& text) {
    std::vector<CommandDescriptor> descriptors;
    CommandBindings bindings;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        descriptors.push_back({actions[i], "Toggle " + std::string(groups[i])});
        bindings[actions[i]] = actions[i];
    }
    for (const auto field : numeric_fields) {
        const auto name = field_name(field);
        for (const auto* operation : {"decrease", "increase", "apply", "cancel"}) {
            const auto id = name + "." + operation;
            descriptors.push_back({id, id}); bindings[id] = id;
        }
        descriptors.push_back({name + ".set", "Set " + name + " strength", {}, {}, CommandAvailability::both,
            {{"field", CommandParameterType::enumeration, true, {}, {}, {}, 32, {name}},
             {"value", CommandParameterType::number, true, {}, 0, 4}}});
    }
    for (const auto* action : msaa_actions) {
        descriptors.push_back({action, action}); bindings[action] = action;
    }
    descriptors.push_back({"msaa.set", "Set MSAA", {}, {}, CommandAvailability::both, {
        {"field", CommandParameterType::enumeration, true, {}, {}, {}, 32, {"msaa"}},
        {"value", CommandParameterType::number, true, {}, 1, 8}}});
    descriptors.push_back({"outlines.toggle", "Toggle outlines"});
    bindings["outlines.toggle"] = "outlines.toggle";
    descriptors.push_back({"outlines.set", "Set outlines", {}, {}, CommandAvailability::both, {
        {"field", CommandParameterType::enumeration, true, {}, {}, {}, 32, {"outlines"}},
        {"value", CommandParameterType::boolean, true}}});
    descriptors.push_back({"scene_lights.toggle", "Toggle scene lights"});
    bindings["scene_lights.toggle"] = "scene_lights.toggle";
    descriptors.push_back({"scene_lights.set", "Set scene lights", {}, {}, CommandAvailability::both, {
        {"field", CommandParameterType::enumeration, true, {}, {}, {}, 32, {"scene_lights"}},
        {"value", CommandParameterType::boolean, true}}});
    for (const auto field : numeric_fields) {
        const auto name = field_name(field);
        descriptors.push_back({name + ".edit", "Edit " + name + " strength", {}, {}, CommandAvailability::both, {
            {"owner", CommandParameterType::string, true, {}, {}, {}, 20},
            {"revision", CommandParameterType::string, true, {}, {}, {}, 20},
            {"kind", CommandParameterType::enumeration, true, {}, {}, {}, 32,
                {"focus", "selection", "insert", "backward", "forward", "left", "right", "home", "end"}},
            {"text", CommandParameterType::string, false, std::string{}, {}, {}, 32},
            {"anchor", CommandParameterType::number, false, 0.0, 0, 32},
            {"caret", CommandParameterType::number, false, 0.0, 0, 32},
            {"extend", CommandParameterType::boolean, false, false}}});
        bindings[name + ".edit"] = name + ".edit";
    }
    auto registry = CommandRegistry::create(std::move(descriptors), std::move(bindings));
    if (!registry) return {std::nullopt, std::move(registry.diagnostics)};
    auto host = std::unique_ptr<Host>(new Host(std::move(*registry.value)));
    // Never reuse draft lifetimes, even across closed/reopened synthetic panels.
    static std::atomic<std::uint64_t> owners{1};
    for (const auto field : numeric_fields) {
        auto owner = owners.load();
        while (owner != std::numeric_limits<std::uint64_t>::max() && !owners.compare_exchange_weak(owner, owner + 1)) {}
        if (owner == std::numeric_limits<std::uint64_t>::max())
            return {std::nullopt, {{"numeric_owner", Severity::error, "/controls/" + field_name(field), "Draft lifetime IDs exhausted"}}};
        auto& numeric = field == NumericField::direct ? host->numeric_ : host->ambient_;
        auto draft = make_text_edit_state(owner, canonical(numeric.accepted));
        if (!draft) return {std::nullopt, std::move(draft.diagnostics)};
        numeric.draft = std::move(*draft.value);
    }
    auto staged = host->stage(snapshot, host->disclosure_, {}, {}, false, host->eligible_, host->numeric_, host->msaa_, host->outlines_, host->scene_lights_, host->ambient_, text);
    if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
    host->accept(std::move(*staged.value), std::move(snapshot), host->disclosure_, {}, host->eligible_, host->numeric_, host->msaa_, host->outlines_, host->scene_lights_, host->ambient_);
    return {std::move(host), {}};
}

Result<View> Host::stage(const Snapshot& snapshot, const Disclosure& disclosure, Point offset,
    FocusDispatcher focus, bool reveal, std::array<bool, 3> eligible, const Numeric& numeric, const Msaa& msaa, const Outlines& outlines, const SceneLights& scene_lights, const Numeric& ambient, TextShaper& text) {
    auto errors = validate(snapshot);
    for (const auto field : numeric_fields) {
        const auto& value = field == NumericField::direct ? numeric : ambient;
        if (!std::isfinite(value.accepted) || value.accepted < 0 || value.accepted > 4 || canonical(value.accepted).size() > 32)
            errors.push_back({field_name(field) + "_value", Severity::error, "/controls/" + field_name(field),
                "Expected an accepted strength in [0,4] with a bounded decimal"});
    }
    std::set<unsigned> samples;
    for (const auto sample : msaa.supported)
        if (std::find(sample_counts.begin(), sample_counts.end(), sample) == sample_counts.end() || !samples.insert(sample).second)
            errors.push_back({"msaa_capabilities", Severity::error, "/controls/msaa/supported", "Expected unique samples from {1,2,4,8}"});
    if (!supports(msaa, msaa.accepted))
        errors.push_back({"msaa_capabilities", Severity::error, "/controls/msaa/supported", "Supported samples must include the accepted MSAA value"});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    CommandStates states;
    for (std::size_t i = 0; i < groups.size(); ++i) states[actions[i]] = {i == 3 || eligible[i]};
    for (const auto field : numeric_fields) {
        const auto& value = field == NumericField::direct ? numeric : ambient;
        const auto name = field_name(field);
        const bool enabled = value.enabled && disclosure.expanded[3];
        const bool dirty = value.draft.text != canonical(value.accepted);
        states[name + ".edit"] = {enabled}; states[name + ".apply"] = {enabled}; states[name + ".set"] = {enabled};
        states[name + ".cancel"] = {enabled && dirty};
        states[name + ".decrease"] = {enabled && !dirty && value.accepted > 0};
        states[name + ".increase"] = {enabled && !dirty && value.accepted < 4};
    }
    const bool msaa_enabled = msaa.enabled && disclosure.expanded[3];
    states["msaa.set"] = {msaa_enabled};
    for (std::size_t i = 0; i < sample_counts.size(); ++i)
        states[msaa_actions[i]] = {msaa_enabled && supports(msaa, sample_counts[i])};
    states["outlines.toggle"] = {outlines.enabled && disclosure.expanded[3]};
    states["outlines.set"] = {outlines.enabled && disclosure.expanded[3]};
    states["scene_lights.toggle"] = {scene_lights.enabled && disclosure.expanded[3]};
    states["scene_lights.set"] = {scene_lights.enabled && disclosure.expanded[3]};
    auto commands = CommandSnapshot::publish(registry_, ++generation_, CommandMode::production, std::move(states));
    if (!commands) return {std::nullopt, std::move(commands.diagnostics)};
    auto staged = prepare(session_, snapshot, disclosure, *commands.value, offset, numeric, msaa, outlines, scene_lights, ambient, text);
    if (!staged) return staged;
    // Removing controls has an explicit recovery target instead of adjacent-field heuristics.
    if (!disclosure.expanded[3] && controls_focus(focused())) {
        auto recovered = focus.focus(staged.value->hit(), *staged.value->tree->find("disclosure-controls"));
        if (!recovered) return {std::nullopt, std::move(recovered.diagnostics)};
    }
    auto recovered = focus.refresh(staged.value->hit());
    if (!recovered) return {std::nullopt, std::move(recovered.diagnostics)};
    if (reveal && recovered.value->focused) {
        auto updates = scroll_into_view(staged.value->hit(), *recovered.value->focused);
        if (!updates) return {std::nullopt, std::move(updates.diagnostics)};
        if (!updates.value->empty()) staged = prepare(session_, snapshot, disclosure, *commands.value,
            updates.value->front().offset, numeric, msaa, outlines, scene_lights, ambient, text);
    }
    return staged;
}

void Host::accept(View view, Snapshot snapshot, Disclosure disclosure, FocusDispatcher focus,
    std::array<bool, 3> eligible, Numeric numeric, Msaa msaa, Outlines outlines, SceneLights scene_lights, Numeric ambient) {
    const bool recover_header = !disclosure.expanded[3] && controls_focus(focused());
    const bool changed = ambient.accepted != ambient_.accepted || numeric.accepted != numeric_.accepted || msaa.accepted != msaa_.accepted || outlines.accepted != outlines_.accepted ||
        scene_lights.accepted != scene_lights_.accepted;
    offset_ = view.layout.boxes[view.tree->find("renderer-diagnostics")->index].scroll->offset;
    snapshot_ = std::move(snapshot); disclosure_ = disclosure; eligible_ = eligible; numeric_ = std::move(numeric); ambient_ = std::move(ambient); msaa_ = std::move(msaa); outlines_ = outlines; scene_lights_ = scene_lights;
    accepted_ = std::move(view);
    focus_ = std::move(focus);
    if (recover_header) (void)focus_.focus(accepted_->hit(), *accepted_->tree->find("disclosure-controls"));
    else (void)focus_.refresh(accepted_->hit());
    (void)pointers_.refresh(accepted_->hit()); // Claimed input streams outlive this cancellation.
    if (changed) ++setting_changes_;
    session_.publish();
}

std::optional<std::string> Host::focused() const {
    if (!accepted_) return {};
    auto copy = focus_;
    const auto refreshed = copy.refresh(view().hit());
    if (!refreshed || !refreshed.value->focused) return {};
    return view().tree->get(*refreshed.value->focused)->id;
}

Result<bool> Host::refresh(Snapshot snapshot, TextShaper& text) {
    auto staged = stage(snapshot, disclosure_, offset_, focus_, true, eligible_, numeric_, msaa_, outlines_, scene_lights_, ambient_, text);
    if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
    accept(std::move(*staged.value), std::move(snapshot), disclosure_, focus_, eligible_, numeric_, msaa_, outlines_, scene_lights_, ambient_);
    return {true, {}};
}
Result<bool> Host::eligibility(std::array<bool, 3> eligible, TextShaper& text) {
    auto staged = stage(snapshot_, disclosure_, offset_, focus_, true, eligible, numeric_, msaa_, outlines_, scene_lights_, ambient_, text);
    if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
    accept(std::move(*staged.value), snapshot_, disclosure_, focus_, eligible, numeric_, msaa_, outlines_, scene_lights_, ambient_);
    return {true, {}};
}
Result<bool> Host::numeric_eligibility(bool enabled, TextShaper& text) {
    return numeric_eligibility(NumericField::direct, enabled, text);
}
Result<bool> Host::numeric_eligibility(NumericField field, bool enabled, TextShaper& text) {
    auto numeric = numeric_, ambient = ambient_;
    (field == NumericField::direct ? numeric : ambient).enabled = enabled;
    auto staged = stage(snapshot_, disclosure_, offset_, focus_, true, eligible_, numeric, msaa_, outlines_, scene_lights_, ambient, text);
    if (!staged) return {std::nullopt, numeric_diagnostics(std::move(staged.diagnostics), field)};
    accept(std::move(*staged.value), snapshot_, disclosure_, focus_, eligible_, std::move(numeric), msaa_, outlines_, scene_lights_, std::move(ambient));
    return {true, {}};
}
Result<bool> Host::msaa_capabilities(std::vector<unsigned> supported, bool enabled, TextShaper& text) {
    auto msaa = msaa_; msaa.supported = std::move(supported); msaa.enabled = enabled;
    auto staged = stage(snapshot_, disclosure_, offset_, focus_, true, eligible_, numeric_, msaa, outlines_, scene_lights_, ambient_, text);
    if (!staged) return {std::nullopt, bounded_diagnostics(std::move(staged.diagnostics))};
    accept(std::move(*staged.value), snapshot_, disclosure_, focus_, eligible_, numeric_, std::move(msaa), outlines_, scene_lights_, ambient_);
    return {true, {}};
}
Result<bool> Host::outlines_eligibility(bool enabled, TextShaper& text) {
    auto outlines = outlines_; outlines.enabled = enabled;
    auto staged = stage(snapshot_, disclosure_, offset_, focus_, true, eligible_, numeric_, msaa_, outlines, scene_lights_, ambient_, text);
    if (!staged) return {std::nullopt, bounded_diagnostics(std::move(staged.diagnostics))};
    accept(std::move(*staged.value), snapshot_, disclosure_, focus_, eligible_, numeric_, msaa_, outlines, scene_lights_, ambient_);
    return {true, {}};
}
Result<CommandInvocation> Host::request_outlines(std::string field, bool value, CommandSource source) {
    return view().commands.resolve({view().commands.generation(), "outlines.set",
        {{"field", std::move(field)}, {"value", value}}, source, ++sequence_});
}
Result<bool> Host::scene_lights_eligibility(bool enabled, TextShaper& text) {
    auto scene_lights = scene_lights_; scene_lights.enabled = enabled;
    auto staged = stage(snapshot_, disclosure_, offset_, focus_, true, eligible_, numeric_, msaa_, outlines_, scene_lights, ambient_, text);
    if (!staged) return {std::nullopt, bounded_diagnostics(std::move(staged.diagnostics))};
    accept(std::move(*staged.value), snapshot_, disclosure_, focus_, eligible_, numeric_, msaa_, outlines_, scene_lights, ambient_);
    return {true, {}};
}
Result<CommandInvocation> Host::request_scene_lights(std::string field, bool value, CommandSource source) {
    return view().commands.resolve({view().commands.generation(), "scene_lights.set",
        {{"field", std::move(field)}, {"value", value}}, source, ++sequence_});
}
namespace {
CommandArguments edit_arguments(const TextEditRequest& edit) {
    CommandArguments args{{"owner", std::to_string(edit.owner)}, {"revision", std::to_string(edit.revision)}};
    std::visit([&](const auto& operation) {
        using T = std::decay_t<decltype(operation)>;
        if constexpr (std::is_same_v<T, TextEditInsert>) {
            args["kind"] = std::string{"insert"}; args["text"] = operation.text;
        } else if constexpr (std::is_same_v<T, TextEditSelection>) {
            args["kind"] = std::string{"selection"};
            args["anchor"] = static_cast<double>(operation.anchor); args["caret"] = static_cast<double>(operation.caret);
        } else if constexpr (std::is_same_v<T, TextEditErase>) {
            args["kind"] = std::string{operation.direction == TextErase::backward ? "backward" : "forward"};
        } else {
            constexpr const char* moves[]{"left", "right", "home", "end"};
            const auto index = static_cast<unsigned>(operation.direction);
            args["kind"] = std::string{index < 4 ? moves[index] : "invalid"}; args["extend"] = operation.extend;
        }
    }, edit.operation);
    return args;
}
}
Result<CommandInvocation> Host::request_edit(const TextEditRequest& edit, CommandSource source) {
    return request_edit(NumericField::direct, edit, source);
}
Result<CommandInvocation> Host::request_edit(NumericField field, const TextEditRequest& edit, CommandSource source) {
    const auto name = field_name(field);
    auto owner = view().tree->find(name + "-input");
    if (!owner) return {std::nullopt, {{name + "_target", Severity::error, "/controls/" + name, "Expected a live numeric field"}}};
    auto action = request_semantic_action(view().semantic(), *owner, "activate");
    if (!action) return {std::nullopt, std::move(action.diagnostics)};
    return view().commands.resolve_action(view().semantic(), *action.value, view().commands.generation(), edit_arguments(edit), source, ++sequence_);
}
Result<CommandInvocation> Host::request_value(std::string field, double value, CommandSource source) {
    if (field != "direct" && field != "ambient" && field != "msaa")
        return {std::nullopt, {{"setting_field", Severity::error, "/controls/" + field, "Expected direct, ambient or msaa field identity"}}};
    const auto command = field + ".set";
    return view().commands.resolve({view().commands.generation(), command,
        {{"field", std::move(field)}, {"value", value}}, source, ++sequence_});
}
Result<CommandInvocation> Host::request(const std::string& id, CommandSource source) {
    const auto owner = view().tree->find(id);
    if (!owner) return {std::nullopt, {{"telemetry_target", Severity::error, "/target/" + id, "Expected a live activation owner"}}};
    auto action = request_semantic_action(view().semantic(), *owner, "activate");
    if (!action) return {std::nullopt, std::move(action.diagnostics)};
    CommandArguments args;
    if (id == "direct-input" || id == "ambient-input") {
        const auto& draft = id == "direct-input" ? numeric_.draft : ambient_.draft;
        args = {{"owner", std::to_string(draft.owner)}, {"revision", std::to_string(draft.revision)}, {"kind", std::string{"focus"}}};
    }
    return view().commands.resolve_action(view().semantic(), *action.value, view().commands.generation(), std::move(args), source, ++sequence_);
}

Result<CommandExecution> Host::execute(const CommandInvocation& invocation, TextShaper& text) {
    struct Handler final : CommandHost {
        Host& host;
        TextShaper& text;
        Disclosure disclosure;
        Numeric numeric, ambient;
        NumericField field = NumericField::direct;
        Msaa msaa;
        Outlines outlines;
        SceneLights scene_lights;
        FocusDispatcher focus;
        std::optional<View> candidate;
        Handler(Host& host, TextShaper& text) : host(host), text(text), disclosure(host.disclosure_),
            numeric(host.numeric_), ambient(host.ambient_), msaa(host.msaa_), outlines(host.outlines_), scene_lights(host.scene_lights_), focus(host.focus_) {}
        CommandOutcome error(std::string message) {
            return {CommandOutcomeStatus::error, {}, {{field_name(field) + "_request", Severity::error, "/controls/" + field_name(field), std::move(message)}}};
        }
        CommandOutcome execute(const CommandInvocationRecord& call) override {
            const bool is_numeric = call.command.starts_with("direct.") || call.command.starts_with("ambient.");
            field = call.command.starts_with("ambient.") ? NumericField::ambient : NumericField::direct;
            auto& value = field == NumericField::direct ? numeric : ambient;
            const auto name = field_name(field);
            const auto operation = is_numeric ? call.command.substr(name.size() + 1) : std::string{};
            bool reset = false;
            if (call.command.starts_with("diagnostics.")) {
                ++host.disclosure_requests_;
                for (std::size_t i = 0; i < groups.size(); ++i)
                    if (call.command == actions[i]) disclosure.expanded[i] = !disclosure.expanded[i];
            } else if (call.command.starts_with("outlines.")) {
                ++host.setting_attempts_;
                outlines.accepted = call.command == "outlines.toggle" ? !outlines.accepted : std::get<bool>(call.arguments.at("value"));
            } else if (call.command.starts_with("scene_lights.")) {
                ++host.setting_attempts_;
                scene_lights.accepted = call.command == "scene_lights.toggle" ? !scene_lights.accepted : std::get<bool>(call.arguments.at("value"));
            } else if (call.command.starts_with("msaa.")) {
                ++host.setting_attempts_;
                double samples = 0;
                if (call.command == "msaa.set") samples = std::get<double>(call.arguments.at("value"));
                else for (std::size_t i = 0; i < msaa_actions.size(); ++i)
                    if (call.command == msaa_actions[i]) samples = sample_counts[i];
                if (!std::isfinite(samples) || std::find(sample_counts.begin(), sample_counts.end(), samples) == sample_counts.end() ||
                    !supports(msaa, static_cast<unsigned>(samples)))
                    return {CommandOutcomeStatus::error, {}, {{"msaa_value", Severity::error, "/controls/msaa",
                        "Expected a supported integral sample count from {1,2,4,8}"}}};
                msaa.accepted = static_cast<unsigned>(samples);
            } else if (is_numeric && operation == "edit") {
                ++host.draft_requests_;
                auto integer = [&](const char* key, std::uint64_t& result) {
                    const auto& value = std::get<std::string>(call.arguments.at(key));
                    auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
                    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
                };
                TextEditRequest edit;
                if (!integer("owner", edit.owner) || !integer("revision", edit.revision) ||
                    edit.owner != value.draft.owner || edit.revision != value.draft.revision)
                    return error("Expected the live draft owner and revision");
                const auto& kind = std::get<std::string>(call.arguments.at("kind"));
                if (kind == "focus") {
                    auto focused = focus.focus(host.view().hit(), *host.view().tree->find(name + "-input"));
                    if (!focused) return command_error(std::move(focused.diagnostics));
                } else {
                    if (kind == "insert") edit.operation = TextEditInsert{std::get<std::string>(call.arguments.at("text"))};
                    else if (kind == "selection") {
                        const auto anchor = std::get<double>(call.arguments.at("anchor"));
                        const auto caret = std::get<double>(call.arguments.at("caret"));
                        if (anchor != std::floor(anchor) || caret != std::floor(caret)) return error("Selection offsets must be integral bytes");
                        edit.operation = TextEditSelection{static_cast<std::size_t>(anchor), static_cast<std::size_t>(caret)};
                    } else if (kind == "backward" || kind == "forward")
                        edit.operation = TextEditErase{kind == "backward" ? TextErase::backward : TextErase::forward};
                    else edit.operation = TextEditMove{kind == "left" ? TextMove::left : kind == "right" ? TextMove::right :
                        kind == "home" ? TextMove::home : TextMove::end, std::get<bool>(call.arguments.at("extend"))};
                    auto edited = edit_text(value.draft, edit);
                    if (!edited) return command_error(numeric_diagnostics(std::move(edited.diagnostics), field));
                    if (edited.value->text.size() > 32) return error("Numeric draft is limited to 32 UTF-8 bytes");
                    value.draft = std::move(*edited.value);
                }
            } else if (is_numeric && operation == "cancel") { ++host.draft_requests_; reset = true; }
            else if (is_numeric && (operation == "apply" || operation == "set" || operation == "increase" || operation == "decrease")) {
                ++host.setting_attempts_;
                if (operation == "apply") {
                    auto parsed = parse_numeric(value.draft.text, field);
                    if (!parsed) return command_error(std::move(parsed.diagnostics));
                    value.accepted = *parsed.value;
                } else if (operation == "set") {
                    value.accepted = std::get<double>(call.arguments.at("value"));
                    if (canonical(value.accepted).size() > 32) return error("Accepted value must fit the 32-byte decimal draft");
                }
                else value.accepted = std::clamp(value.accepted + (operation == "increase" ? 0.25 : -0.25), 0.0, 4.0);
                reset = true;
            }
            else return error("Unknown setting command");
            if (reset) {
                if (value.draft.revision == std::numeric_limits<std::uint64_t>::max()) return error("Draft revision exhausted; reopen the panel");
                value.draft.text = canonical(value.accepted);
                value.draft.anchor = value.draft.caret = value.draft.text.size(); ++value.draft.revision;
            }
            auto staged = host.stage(host.snapshot_, disclosure, host.offset_, focus, true, host.eligible_, numeric, msaa, outlines, scene_lights, ambient, text);
            if (!staged) return command_error(is_numeric ? numeric_diagnostics(std::move(staged.diagnostics), field) : std::move(staged.diagnostics));
            candidate = std::move(*staged.value);
            return {};
        }
    } handler(*this, text);
    const auto current = view().semantic();
    auto execution = executor_.execute(view().commands, invocation, handler, &current);
    if (execution && execution.value->outcome.status == CommandOutcomeStatus::success)
        accept(std::move(*handler.candidate), snapshot_, handler.disclosure, std::move(handler.focus), eligible_, std::move(handler.numeric), std::move(handler.msaa), handler.outlines, handler.scene_lights, std::move(handler.ambient));
    return execution;
}

Result<bool> Host::invoke(const std::string& id, CommandSource source, TextShaper& text) {
    auto invocation = request(id, source);
    if (!invocation) return {std::nullopt, std::move(invocation.diagnostics)};
    return completed(execute(*invocation.value, text));
}
Result<bool> Host::operate(const ActionRequest& action, CommandSource source, TextShaper& text) {
    const auto* node = view().tree->get(action.target);
    if (node && (node->id == "direct-input" || node->id == "ambient-input")) return invoke(*node->id, source, text);
    auto invocation = view().commands.resolve_action(view().semantic(), action, view().commands.generation(), {}, source, ++sequence_);
    if (!invocation) return {std::nullopt, std::move(invocation.diagnostics)};
    return completed(execute(*invocation.value, text));
}

Result<bool> Host::route(const InputEvent& event, CommandSource source, TextShaper& text) {
    auto result = input(event, source, text);
    if (result && !last_consumed_) ++camera_events_;
    return result;
}
Result<bool> Host::input(const InputEvent& event, CommandSource source, TextShaper& text) {
    last_consumed_ = false;
    auto errors = tessera::validate(event);
    if (timestamp_ && event.timestamp < *timestamp_)
        errors.push_back({"telemetry_timestamp", Severity::error, "/timestamp", "Expected a nondecreasing normalized event timestamp"});
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    timestamp_ = event.timestamp;
    auto panel_point = [&](Point p) { return p.x >= 0 && p.y >= 0 && p.x < viewport.width && p.y < viewport.height; };
    if (const auto* scroll = std::get_if<Scroll>(&event.data)) {
        last_consumed_ = panel_point(scroll->position);
        if (!last_consumed_) return {false, {}};
        auto updates = route_scroll(view().hit(), *scroll);
        if (!updates) return {std::nullopt, std::move(updates.diagnostics)};
        if (updates.value->updates.empty()) return {true, {}};
        auto staged = stage(snapshot_, disclosure_, updates.value->updates.front().offset, focus_, false, eligible_, numeric_, msaa_, outlines_, scene_lights_, ambient_, text);
        if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
        accept(std::move(*staged.value), snapshot_, disclosure_, focus_, eligible_, numeric_, msaa_, outlines_, scene_lights_, ambient_);
        return {true, {}};
    }
    const auto focused_id = focused().value_or("");
    const auto field = focused_id.starts_with("ambient-") ? NumericField::ambient : NumericField::direct;
    const bool editing = focused_id == field_name(field) + "-input";
    const auto& draft = field == NumericField::direct ? numeric_.draft : ambient_.draft;
    auto edit = [&](auto operation) -> Result<bool> {
        last_consumed_ = true;
        auto call = request_edit(field, {draft.owner, draft.revision, std::move(operation)}, source);
        if (!call) return {std::nullopt, std::move(call.diagnostics)};
        return completed(execute(*call.value, text));
    };
    if (const auto* committed = std::get_if<tessera::TextInput>(&event.data)) {
        if (editing) return edit(TextEditInsert{committed->text});
        return {false, {}};
    }
    std::optional<InputEvent> mapped;
    if (const auto* key = std::get_if<KeyDown>(&event.data)) {
        const auto& m = key->modifiers;
        if (m.control || m.alt || m.super) return {false, {}};
        if (editing) switch (key->key) {
        case Key::home: return edit(TextEditMove{TextMove::home, m.shift});
        case Key::end: return edit(TextEditMove{TextMove::end, m.shift});
        case Key::left: return edit(TextEditMove{TextMove::left, m.shift});
        case Key::right: return edit(TextEditMove{TextMove::right, m.shift});
        case Key::backspace: if (!m.shift) return edit(TextEditErase{TextErase::backward}); break;
        case Key::delete_key: if (!m.shift) return edit(TextEditErase{TextErase::forward}); break;
        case Key::enter: case Key::escape:
            if (!m.shift && !key->repeat) { last_consumed_ = true; return invoke(field_name(field) + (key->key == Key::enter ? "-apply" : "-cancel"), source, text); }
            return {false, {}};
        default: break;
        }
        if (key->key == Key::tab) mapped = {event.timestamp, m.shift ? decltype(InputEvent::data){FocusPrevious{}} : decltype(InputEvent::data){FocusNext{}}};
        else if (!m.shift && (key->key == Key::up || key->key == Key::down || key->key == Key::left || key->key == Key::right))
            mapped = {event.timestamp, Navigate{key->key == Key::up ? Direction::up : key->key == Key::down ? Direction::down :
                key->key == Key::left ? Direction::left : Direction::right, key->repeat}};
        else if (!m.shift && !key->repeat && (key->key == Key::enter || key->key == Key::space)) mapped = {event.timestamp, Activate{}};
        else if (!m.shift && !key->repeat && key->key == Key::escape) mapped = {event.timestamp, Cancel{}};
        else return {false, {}};
    }
    if (std::holds_alternative<KeyUp>(event.data)) return {false, {}};
    if (std::holds_alternative<PointerMove>(event.data) || std::holds_alternative<PointerDown>(event.data) ||
        std::holds_alternative<PointerUp>(event.data) || std::holds_alternative<PointerCancel>(event.data)) {
        std::uint32_t id = 0; bool terminal = false; bool inside = false;
        std::visit([&](const auto& pointer) {
            using T = std::decay_t<decltype(pointer)>;
            if constexpr (std::is_same_v<T, PointerMove> || std::is_same_v<T, PointerDown> || std::is_same_v<T, PointerUp> || std::is_same_v<T, PointerCancel>) {
                id = pointer.pointer.value;
                if constexpr (!std::is_same_v<T, PointerCancel>) inside = panel_point(pointer.position);
                if constexpr (std::is_same_v<T, PointerDown>) {
                    if (pointer.button == PointerButton::primary) {
                        if (inside) claimed_.insert(id); else outside_.insert(id);
                    }
                }
                if constexpr (std::is_same_v<T, PointerUp>) terminal = pointer.button == PointerButton::primary;
                if constexpr (std::is_same_v<T, PointerCancel>) terminal = true;
            }
        }, event.data);
        last_consumed_ = !outside_.contains(id) && (inside || claimed_.contains(id));
        if (terminal) { claimed_.erase(id); outside_.erase(id); }
        if (!last_consumed_) return {false, {}};
        auto pointers = pointers_;
        auto dispatched = pointers.dispatch(view().hit(), event);
        if (!dispatched) return {std::nullopt, std::move(dispatched.diagnostics)};
        // Save release/cancel state even if the subsequent command returns an error.
        pointers_ = std::move(pointers);
        if (const auto* down = std::get_if<PointerDown>(&event.data); down && down->button == PointerButton::primary)
            for (const auto& pointer : dispatched.value->pointers)
                if (pointer.pointer == down->pointer && pointer.pressed) (void)focus_.focus(view().hit(), *pointer.pressed);
        for (const auto& action : dispatched.value->actions) return operate(action, source, text);
        return {true, {}};
    }
    const auto& logical = mapped ? *mapped : event;
    last_consumed_ = true;
    if (std::holds_alternative<Cancel>(logical.data) && (focused_id.starts_with("direct-") || focused_id.starts_with("ambient-")))
        return invoke(field_name(field) + "-cancel", source, text);
    auto focus = focus_;
    auto dispatched = focus.dispatch(view().hit(), logical);
    if (!dispatched) return {std::nullopt, std::move(dispatched.diagnostics)};
    for (const auto& action : dispatched.value->actions) return operate(action, source, text);
    auto staged = stage(snapshot_, disclosure_, offset_, focus, true, eligible_, numeric_, msaa_, outlines_, scene_lights_, ambient_, text);
    if (!staged) return {std::nullopt, std::move(staged.diagnostics)};
    accept(std::move(*staged.value), snapshot_, disclosure_, std::move(focus), eligible_, numeric_, msaa_, outlines_, scene_lights_, ambient_);
    return {true, {}};
}
} // namespace diagnostics
