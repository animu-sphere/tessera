#include <tessera/inspection/capabilities.hpp>
#include <tessera/layout/layout_box.hpp>
#include <tessera/text/text.hpp>
#include <tessera/ui/serialization.hpp>
#include <tessera/ui/tree.hpp>
#include "../check.hpp"
#include <algorithm>
#include <iostream>

using namespace tessera;
using tessera::test::check;
using tessera::test::has;

namespace {
constexpr std::string_view json = R"({"version":1,"root":{"type":"Box","id":"panel","children":[
 {"type":"Box","id":"save","properties":{"focusable":true},"events":{"activate":"doc.save"},
  "children":[{"type":"Text","properties":{"text":"Save"}}]},
 {"type":"Box","id":"local","events":{"activate":"local.toggle"},
  "children":[{"type":"Text","properties":{"text":"Local"}}]},
 {"type":"Box","id":"dump","events":{"activate":"debug.dump"},
  "children":[{"type":"Text","properties":{"text":"Dump"}}]},
 {"type":"Box","id":"off","properties":{"disabled":true,"focusable":true},"events":{"activate":"doc.save"},
  "children":[{"type":"Text","properties":{"text":"Off"}}]},
 {"type":"Text","id":"field","properties":{"text":"Field","focusable":true}},
 {"type":"Text","id":"caption","properties":{"text":"Caption"}}
]}})";
const ValidationContext context{{"doc.save", "local.toggle", "debug.dump"}};

CommandRegistry registry() {
    std::vector<CommandDescriptor> descriptors{
        {.id = "doc.save", .label = "Save", .description = "Save the document.", .category = "File"},
        {.id = "doc.open",
         .label = "Open",
         .category = "File",
         .parameters =
             {{.name = "path",
               .type = CommandParameterType::string,
               .default_value = CommandValue{std::string("a.txt")},
               .max_bytes = 64},
              {.name = "count", .type = CommandParameterType::number, .required = false, .minimum = 0., .maximum = 10.},
              {.name = "placement", .type = CommandParameterType::enumeration, .choices = {"before", "after"}},
              {.name = "item", .type = CommandParameterType::object_id},
              {.name = "flag", .type = CommandParameterType::boolean, .default_value = true}},
         .results = {{.name = "opened", .type = CommandParameterType::object_id}}},
        {.id = "doc.print", .label = "Print"},
        {.id = "debug.dump", .label = "Dump", .availability = CommandAvailability::development},
        {.id = "prod.only", .label = "Production", .availability = CommandAvailability::production}};
    auto created =
        CommandRegistry::create(std::move(descriptors), {{"doc.save", "doc.save"}, {"debug.dump", "debug.dump"}});
    check(static_cast<bool>(created), "Fixture registry validates");
    return std::move(*created.value);
}
CommandSnapshot publish(const CommandRegistry& commands, std::uint64_t generation, CommandMode mode) {
    CommandStates states{{"doc.save", {true}},
                         {"doc.open", {false, false, "Nothing to open."}},
                         {"doc.print", {true, true, "", "printer query failed"}},
                         {"debug.dump", {true}},
                         {"prod.only", {true, true}}};
    auto published = CommandSnapshot::publish(commands, generation, mode, std::move(states), {"sphere", "cube"});
    check(static_cast<bool>(published), "Fixture command snapshot publishes");
    return std::move(*published.value);
}
// Commands are applied to the authored document before projection, as a host does each generation.
SemanticSnapshot semantics(const CommandSnapshot& commands, std::uint64_t generation) {
    auto loaded = load_document(json, context);
    check(static_cast<bool>(loaded), "Fixture document loads");
    auto lowered = commands.apply_eligibility(*loaded.value, context);
    check(static_cast<bool>(lowered), "Eligibility applies");
    auto tree = UiTree::create(*lowered.value, context);
    check(static_cast<bool>(tree), "Fixture tree validates");
    std::vector<ResolvedStyle> styles((*tree.value)->size());
    PlaceholderTextShaper text;
    auto layout = compute_layout({tree.value->get(), styles, {240, 160}, &text});
    check(static_cast<bool>(layout), "Fixture layout computes");
    auto captured = capture_semantics({tree.value->get(), styles, &*layout.value}, generation);
    check(static_cast<bool>(captured), "Fixture semantics capture");
    return std::move(*captured.value);
}
const std::vector<CapabilityDeclaration> declarations{
    {.operation = CapabilityOperation::capture_frame,
     .entry_point = "fixture capture",
     .request = "fixture-scenario",
     .result = "tessera-snapshot-bundle/1",
     .preconditions = {CapabilityPrecondition::target, CapabilityPrecondition::generation}},
    {.operation = CapabilityOperation::inspect_tree,
     .entry_point = "fixture capture",
     .request = "fixture-scenario",
     .result = "tessera-snapshot-bundle/1"},
    {.operation = CapabilityOperation::edit_source,
     .availability = CommandAvailability::development,
     .entry_point = "fixture edit",
     .request = "fixture-edit/1",
     .result = "fixture-edit-result/1"}};
CapabilityManifest capture(const CommandSnapshot& commands, const SemanticSnapshot& semantic,
                           std::span<const CapabilityDeclaration> operations = declarations) {
    auto manifest = capture_capabilities({"capabilities-test", &commands, &semantic, operations});
    for (const auto& d : manifest.diagnostics) std::cerr << d.code << ' ' << d.path << ' ' << d.message << '\n';
    check(static_cast<bool>(manifest), "Valid input captures a manifest");
    return std::move(*manifest.value);
}
const CapabilityTarget* target(const CapabilityManifest& manifest, std::string_view id) {
    for (const auto& entry : manifest.targets())
        if (entry.id == id) return &entry;
    return nullptr;
}
std::vector<std::string> command_ids(const CapabilityManifest& manifest) {
    std::vector<std::string> ids;
    for (const auto& info : manifest.commands()) ids.push_back(info.descriptor.id);
    return ids;
}

void production_projection() {
    const auto commands = registry();
    const auto snapshot = publish(commands, 7, CommandMode::production);
    const auto manifest = capture(snapshot, semantics(snapshot, 7));
    check(manifest.version() == 1 && manifest.producer() == "capabilities-test" &&
              manifest.mode() == CommandMode::production && manifest.generation() == 7,
          "The manifest identifies its producer, mode and generation");
    check(manifest.operations().size() == capability_operation_count, "Every operation is reported");
    for (std::size_t i = 0; i < manifest.operations().size(); ++i)
        check(manifest.operations()[i].operation == static_cast<CapabilityOperation>(i), "Operations keep enum order");
    const auto& frame = manifest.operation(CapabilityOperation::capture_frame);
    check(frame.status == CapabilityStatus::supported && frame.result == "tessera-snapshot-bundle/1" &&
              frame.preconditions == std::vector{CapabilityPrecondition::generation, CapabilityPrecondition::target},
          "A supported operation keeps its schemas and sorted preconditions");
    const auto& edit = manifest.operation(CapabilityOperation::edit_source);
    check(edit.status == CapabilityStatus::unavailable && edit.entry_point.empty() && edit.preconditions.empty(),
          "A development-only operation is unavailable in production without its entry point");
    check(manifest.operation(CapabilityOperation::invoke_command).status == CapabilityStatus::not_declared,
          "An undeclared operation is explicit");

    check(command_ids(manifest) == std::vector<std::string>{"doc.open", "doc.print", "doc.save", "prod.only"},
          "Production discovery omits development commands and orders by ID");
    const auto& open = manifest.commands()[0];
    check(!open.state.enabled && open.state.disabled_reason == "Nothing to open." &&
              open.descriptor.parameters.size() == 5 && open.descriptor.results.size() == 1,
          "Commands keep eligibility, disabled reasons and schemas");
    const auto& print = manifest.commands()[1];
    check(!print.state.enabled && !print.state.checked && print.state.disabled_reason == "printer query failed",
          "A failed query is disabled with the reason discovery reports");
    check(manifest.objects() == CommandObjectIds{"cube", "sphere"}, "Permitted object IDs are listed");

    const auto* save = target(manifest, "save");
    check(save && save->role == SemanticRole::button && save->name == "Save" && save->focusable &&
              save->actions == std::vector<CapabilityAction>{{"activate", "doc.save", "doc.save"}},
          "A mapped action names its command");
    const auto* local = target(manifest, "local");
    check(local && local->actions == std::vector<CapabilityAction>{{"activate", "local.toggle", std::nullopt}},
          "An unmapped action has no command");
    check(!target(manifest, "dump"), "A development command disables its owner in production");
    const auto* field = target(manifest, "field");
    check(field && field->focusable && field->actions.empty() && field->role == SemanticRole::text,
          "Focusable entries without actions are targets");
    check(!target(manifest, "off") && !target(manifest, "caption") && !target(manifest, "panel"),
          "Disabled, static and root entries without actions are not targets");
    check(save->node == 1 && local->node == 3, "Targets carry document preorder indices");
}

void development_projection() {
    const auto commands = registry();
    const auto snapshot = publish(commands, 3, CommandMode::development);
    const auto manifest = capture(snapshot, semantics(snapshot, 3));
    check(command_ids(manifest) == std::vector<std::string>{"debug.dump", "doc.open", "doc.print", "doc.save"},
          "Development discovery omits production-only commands");
    check(manifest.operation(CapabilityOperation::edit_source).status == CapabilityStatus::supported,
          "A development operation is supported in development");
    const auto* dump = target(manifest, "dump");
    check(dump && dump->actions == std::vector<CapabilityAction>{{"activate", "debug.dump", "debug.dump"}},
          "A development command is mapped in development");
}

void canonical_json_and_ownership() {
    std::optional<CapabilityManifest> owned;
    {
        const auto commands = registry();
        const auto snapshot = publish(commands, 7, CommandMode::production);
        const auto semantic = semantics(snapshot, 7);
        owned = capture(snapshot, semantic);
        check(*owned == capture(snapshot, semantic), "Repeated capture is deterministic");
    }
    const auto saved = save_capabilities(*owned);
    check(static_cast<bool>(saved) && saved.value->back() == '\n', "A manifest saves with a trailing newline");
    const auto& text = *saved.value;
    check(text == *save_capabilities(*owned).value, "Saving is deterministic");
    for (
        const auto expected : {
            R"("format":"tessera-capabilities")",
            R"("generation":"7")",
            R"("mode":"production")",
            R"({"operation":"inspect_properties","status":"not_declared"})",
            R"({"operation":"edit_source","status":"unavailable"})",
            R"({"entry_point":"fixture capture","operation":"capture_frame","preconditions":["generation","target"],"request":"fixture-scenario","result":"tessera-snapshot-bundle/1","status":"supported"})",
            R"({"choices":["before","after"],"name":"placement","required":true,"type":"enumeration"})",
            R"({"default":"a.txt","max_bytes":64,"name":"path","required":true,"type":"string"})",
            R"({"maximum":10,"minimum":0,"name":"count","required":false,"type":"number"})",
            R"({"default":true,"name":"flag","required":true,"type":"boolean"})",
            R"("disabled_reason":"Nothing to open.")",
            R"("objects":["cube","sphere"])",
            R"({"actions":[],"focusable":true,"id":"field","name":"Field","node":9,"role":"text"})",
            R"({"actions":[{"action":"local.toggle","binding":"activate","command":null}],"focusable":false,"id":"local","name":"Local","node":3,"role":"button"})",
        })
        check(text.find(expected) != std::string::npos, std::string("Canonical JSON contains ") + expected);
    check(text.find("debug.dump") == std::string::npos,
          "Development commands and their owners stay out of a production manifest");
}

void rejection() {
    const auto commands = registry();
    const auto snapshot = publish(commands, 7, CommandMode::production);
    const auto semantic = semantics(snapshot, 7);
    const auto reject = [&](CapabilityInput input, std::string_view code, std::string_view path) {
        const auto result = capture_capabilities(input);
        if (result || !has(result.diagnostics, code, path)) {
            for (const auto& d : result.diagnostics) std::cerr << d.code << ' ' << d.path << '\n';
            check(false, std::string("Rejects with ") + std::string(code) + " at " + std::string(path));
        }
    };
    reject({"p", nullptr, &semantic}, "missing_input", "/commands");
    reject({"p", &snapshot, nullptr}, "missing_input", "/semantics");
    reject({"", &snapshot, &semantic}, "invalid_identifier", "/producer");
    reject({"a\nb", &snapshot, &semantic}, "invalid_identifier", "/producer");
    reject({std::string(257, 'p'), &snapshot, &semantic}, "invalid_identifier", "/producer");
    check(static_cast<bool>(capture_capabilities({std::string(256, 'p'), &snapshot, &semantic})),
          "A 256-byte producer is accepted");

    const auto later = semantics(snapshot, 8);
    reject({"p", &snapshot, &later}, "capability_mismatch", "/semantics/generation");
    auto broken = semantic;
    broken.nodes[1].parent = 99;
    const auto broken_result = capture_capabilities({"p", &snapshot, &broken});
    check(!broken_result && !broken_result.diagnostics.empty() &&
              std::all_of(broken_result.diagnostics.begin(), broken_result.diagnostics.end(),
                          [](const Diagnostic& d) { return d.path.starts_with("/semantics/"); }),
          "Semantic snapshot errors are relocated under /semantics");

    auto twice = declarations;
    twice.push_back(declarations[1]);
    reject({"p", &snapshot, &semantic, twice}, "duplicate_operation", "/operations/3/operation");
    std::vector<CapabilityDeclaration> bad{declarations[0]};
    bad[0].operation = static_cast<CapabilityOperation>(capability_operation_count);
    reject({"p", &snapshot, &semantic, bad}, "unknown_value", "/operations/0/operation");
    bad = {declarations[0]};
    bad[0].availability = static_cast<CommandAvailability>(3);
    reject({"p", &snapshot, &semantic, bad}, "unknown_value", "/operations/0/availability");
    bad = {declarations[0]};
    bad[0].preconditions = {CapabilityPrecondition::target, CapabilityPrecondition::target};
    reject({"p", &snapshot, &semantic, bad}, "duplicate_precondition", "/operations/0/preconditions/1");
    bad[0].preconditions = {static_cast<CapabilityPrecondition>(2)};
    reject({"p", &snapshot, &semantic, bad}, "unknown_value", "/operations/0/preconditions/0");
    bad = {declarations[0]};
    bad[0].entry_point.clear();
    bad[0].request = "\x7f";
    bad[0].result = "\xff";
    const auto strings = capture_capabilities({"p", &snapshot, &semantic, bad});
    check(!strings && has(strings.diagnostics, "invalid_identifier", "/operations/0/entry_point") &&
              has(strings.diagnostics, "invalid_identifier", "/operations/0/request") &&
              has(strings.diagnostics, "invalid_identifier", "/operations/0/result"),
          "Every invalid operation identity is reported");
}
} // namespace

int main() {
    try {
        production_projection();
        development_projection();
        canonical_json_and_ownership();
        rejection();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Capability manifest checks passed.\n";
}
