#include <tessera/semantics/semantic_snapshot.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <set>
#include <utility>

namespace tessera {
namespace {
using Object = JsonValue::Object;
using Array = JsonValue::Array;
constexpr std::array<std::string_view, 3> roles{"root", "button", "text"};
constexpr std::array<std::string_view, 3> sources{"none", "relationship", "content"};
struct Failure { std::vector<Diagnostic> diagnostics; };

[[noreturn]] void fail(std::string code, std::string path, std::string message) {
    throw Failure{{{std::move(code), Severity::error, std::move(path), std::move(message), {}}}};
}
template<class T>
const T& as(const JsonValue& value, const std::string& path) {
    if (const auto* found = std::get_if<T>(&value.value)) return *found;
    fail("schema_type", path, "Use the type declared by Semantic JSON v1.");
}
const Object& object(const JsonValue& value, const std::string& path,
                     std::initializer_list<std::string_view> fields) {
    const auto& result = as<Object>(value, path);
    for (const auto& [key, child] : result) {
        (void)child;
        if (std::find(fields.begin(), fields.end(), key) == fields.end())
            fail("unknown_field", path + "/" + detail::pointer_token(key), "Remove this unknown semantic field.");
    }
    for (const auto field : fields)
        if (!result.contains(field)) fail("missing_field", path + "/" + std::string(field), "Supply this field.");
    return result;
}
std::uint32_t index(const JsonValue& value, const std::string& path) {
    const auto number = as<double>(value, path);
    if (number < 0 || number > std::numeric_limits<std::uint32_t>::max() || std::floor(number) != number)
        fail("schema_type", path, "Use an unsigned 32-bit integer.");
    return static_cast<std::uint32_t>(number);
}
std::optional<std::uint32_t> optional_index(const JsonValue& value, const std::string& path) {
    if (std::holds_alternative<std::nullptr_t>(value.value)) return std::nullopt;
    return index(value, path);
}
std::optional<std::string> optional_string(const JsonValue& value, const std::string& path) {
    if (std::holds_alternative<std::nullptr_t>(value.value)) return std::nullopt;
    return as<std::string>(value, path);
}
template<class E>
E choice(const JsonValue& value, const std::string& path, const std::array<std::string_view, 3>& names) {
    const auto& text = as<std::string>(value, path);
    for (std::size_t i = 0; i < names.size(); ++i) if (text == names[i]) return static_cast<E>(i);
    fail("unknown_value", path, "Use a declared semantic enumeration name.");
}
bool identifier(std::string_view value) {
    return !value.empty() && detail::valid_utf8(value) &&
        std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}
JsonValue number(std::uint32_t value) { return JsonValue{static_cast<double>(value)}; }
JsonValue text(std::string_view value) { return JsonValue{std::string(value)}; }
JsonValue optional_json(std::optional<std::uint32_t> value) { return value ? number(*value) : JsonValue{}; }

SemanticSnapshot read(const JsonValue& json) {
    const auto& root = object(json, "", {"version", "generation", "node_count", "nodes"});
    SemanticSnapshot result;
    result.version = index(root.at("version"), "/version");
    if (result.version != semantic_version) fail("unsupported_version", "/version", "Use semantic version 1.");
    const auto& generation = as<std::string>(root.at("generation"), "/generation");
    const auto parsed = std::from_chars(generation.data(), generation.data() + generation.size(), result.generation);
    if (generation.empty() || (generation.size() > 1 && generation.front() == '0') ||
        parsed.ec != std::errc{} || parsed.ptr != generation.data() + generation.size())
        fail("schema_type", "/generation", "Use the canonical unsigned 64-bit decimal string.");
    result.node_count = index(root.at("node_count"), "/node_count");
    const auto& nodes = as<Array>(root.at("nodes"), "/nodes");
    if (nodes.size() > max_document_nodes) fail("out_of_range", "/nodes", "Use at most 10000 semantic entries.");
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto at = "/nodes/" + std::to_string(i);
        const auto& o = object(nodes[i], at, {"node", "id", "parent", "role", "name", "name_source",
                                             "labelled_by", "enabled", "focusable", "focused", "actions"});
        SemanticRecord entry;
        entry.node = index(o.at("node"), at + "/node");
        entry.id = optional_string(o.at("id"), at + "/id");
        const auto parent = optional_index(o.at("parent"), at + "/parent");
        if (parent == no_semantic_parent)
            fail("invalid_parent", at + "/parent", "Use null for an absent parent, not the in-process sentinel.");
        entry.parent = parent.value_or(no_semantic_parent);
        entry.role = choice<SemanticRole>(o.at("role"), at + "/role", roles);
        entry.name = as<std::string>(o.at("name"), at + "/name");
        entry.name_source = choice<NameSource>(o.at("name_source"), at + "/name_source", sources);
        entry.labelled_by = optional_index(o.at("labelled_by"), at + "/labelled_by");
        entry.enabled = as<bool>(o.at("enabled"), at + "/enabled");
        entry.focusable = as<bool>(o.at("focusable"), at + "/focusable");
        entry.focused = as<bool>(o.at("focused"), at + "/focused");
        const auto& actions = as<Array>(o.at("actions"), at + "/actions");
        if (actions.size() > 1) fail("out_of_range", at + "/actions", "Schema v1 exposes at most one activate action.");
        for (std::size_t a = 0; a < actions.size(); ++a) {
            const auto path = at + "/actions/" + std::to_string(a);
            const auto& action = object(actions[a], path, {"binding", "action"});
            entry.actions.push_back({as<std::string>(action.at("binding"), path + "/binding"),
                                      as<std::string>(action.at("action"), path + "/action")});
        }
        result.nodes.push_back(std::move(entry));
    }
    return result;
}
JsonValue write(const SemanticSnapshot& snapshot) {
    Array nodes;
    for (const auto& entry : snapshot.nodes) {
        Array actions;
        for (const auto& action : entry.actions)
            actions.push_back(JsonValue{Object{{"binding", text(action.binding)}, {"action", text(action.action)}}});
        nodes.push_back(JsonValue{Object{
            {"node", number(entry.node)}, {"id", entry.id ? text(*entry.id) : JsonValue{}},
            {"parent", optional_json(entry.parent == no_semantic_parent ? std::nullopt : std::optional(entry.parent))},
            {"role", text(roles[static_cast<std::size_t>(entry.role)])}, {"name", text(entry.name)},
            {"name_source", text(sources[static_cast<std::size_t>(entry.name_source)])},
            {"labelled_by", optional_json(entry.labelled_by)}, {"enabled", JsonValue{entry.enabled}},
            {"focusable", JsonValue{entry.focusable}}, {"focused", JsonValue{entry.focused}},
            {"actions", JsonValue{std::move(actions)}}}});
    }
    return JsonValue{Object{{"version", number(snapshot.version)}, {"generation", text(std::to_string(snapshot.generation))},
                            {"node_count", number(snapshot.node_count)}, {"nodes", JsonValue{std::move(nodes)}}}};
}
} // namespace

std::vector<SemanticRecord> own_semantic_records(const SemanticTree& tree) {
    std::vector<SemanticRecord> result;
    result.reserve(tree.nodes.size());
    for (const auto& entry : tree.nodes) {
        std::optional<std::uint32_t> labelled_by;
        if (entry.labelled_by) labelled_by = entry.labelled_by->index;
        result.push_back({entry.node.index, entry.id, entry.parent, entry.role, entry.name, entry.name_source,
                          labelled_by, entry.enabled, entry.focusable, entry.focused, entry.actions});
    }
    return result;
}

Result<SemanticSnapshot> capture_semantics(const SemanticInput& input, std::uint64_t generation) {
    auto projected = build_semantic_tree(input);
    if (!projected) return {std::nullopt, std::move(projected.diagnostics)};
    SemanticSnapshot snapshot{semantic_version, generation, static_cast<std::uint32_t>(input.tree->size()),
                               own_semantic_records(*projected.value)};
    return {std::move(snapshot), std::move(projected.diagnostics)};
}

Result<ActionRequest> request_semantic_action(const SemanticInput& input, std::uint64_t current_generation,
                                              SemanticIdentity target, std::string_view binding) {
    auto errors = validate(input);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    if (target.generation != current_generation || target.node >= input.tree->size())
        return {std::nullopt, {{"stale_target", Severity::error, "/target",
                               "Capture and resolve the target in the host's current update generation.", {}}}};
    return request_semantic_action(input, NodeHandle{input.tree->root().tree, target.node}, binding);
}

std::vector<Diagnostic> validate(const SemanticSnapshot& snapshot) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    if (snapshot.version != semantic_version) check.error("unsupported_version", "/version", "Use semantic version 1.");
    if (!snapshot.node_count || snapshot.node_count > max_document_nodes)
        check.error("out_of_range", "/node_count", "Use 1-10000 authored nodes.");
    if (snapshot.nodes.size() > snapshot.node_count || snapshot.nodes.size() > max_document_nodes) {
        check.error("out_of_range", "/nodes", "Semantic entries cannot exceed the authored node count or 10000.");
        return errors;
    }
    std::set<std::string> ids;
    std::vector<std::uint32_t> ancestors;
    bool focused = false;
    for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
        const auto& entry = snapshot.nodes[i];
        const auto at = "/nodes/" + std::to_string(i);
        if (entry.node >= snapshot.node_count || (i && entry.node <= snapshot.nodes[i - 1].node))
            check.error("invalid_identity", at + "/node", "Use strictly increasing authored preorder indices within node_count.");
        if (entry.parent != no_semantic_parent && entry.parent >= i)
            check.error("invalid_parent", at + "/parent", "A parent must precede its child.");
        else {
            while (!ancestors.empty() && ancestors.back() != entry.parent) ancestors.pop_back();
            if (entry.parent != no_semantic_parent && ancestors.empty())
                check.error("invalid_parent", at + "/parent", "Keep each semantic subtree contiguous in preorder.");
            ancestors.push_back(static_cast<std::uint32_t>(i));
        }
        check.enumeration(entry.role, SemanticRole::text, at + "/role");
        check.enumeration(entry.name_source, NameSource::content, at + "/name_source");
        if (entry.role == SemanticRole::root && (entry.node != 0 || entry.parent != no_semantic_parent))
            check.error("invalid_role", at + "/role", "Only the authored root may have the root role.");
        if (entry.id) {
            // Author IDs follow UI document validation, with no extra 256-byte restriction.
            if (entry.id->empty() || !detail::valid_utf8(*entry.id) ||
                std::any_of(entry.id->begin(), entry.id->end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
                check.error("invalid_identifier", at + "/id", "Use a nonempty UTF-8 author ID without ASCII controls.");
            else if (!ids.insert(*entry.id).second) check.error("duplicate_id", at + "/id", "Use unique author IDs.");
        }
        if (!detail::valid_utf8(entry.name)) check.error("invalid_utf8", at + "/name", "Use valid UTF-8.");
        if (entry.labelled_by && *entry.labelled_by >= snapshot.node_count)
            check.error("invalid_reference", at + "/labelled_by", "Reference an authored node within node_count.");
        if (entry.name_source == NameSource::relationship && !entry.labelled_by)
            check.error("invalid_reference", at + "/labelled_by", "A relationship-derived name requires a reference.");
        if ((entry.name.empty()) != (entry.name_source == NameSource::none))
            check.error("invalid_name", at + "/name_source", "An empty name has source none; a nonempty name has a source.");
        if ((!entry.enabled && entry.focusable) || (entry.focused && (!entry.focusable || focused)))
            check.error("invalid_state", at + "/focused", "Focus requires enabled/focusable state and at most one focused entry.");
        focused = focused || entry.focused;
        if (entry.actions.size() > 1 || (!entry.actions.empty() && (!entry.enabled || entry.role != SemanticRole::button)))
            check.error("invalid_action", at + "/actions", "Only an enabled button can expose one activate action.");
        for (std::size_t a = 0; a < entry.actions.size(); ++a) {
            const auto path = at + "/actions/" + std::to_string(a);
            if (entry.actions[a].binding != "activate") check.error("unsupported_action", path + "/binding", "Use activate.");
            if (!identifier(entry.actions[a].action))
                check.error("invalid_identifier", path + "/action", "Use a nonempty UTF-8 action name without ASCII controls.");
        }
    }
    return errors;
}

Result<SemanticSnapshot> load_semantics(std::string_view source) {
    auto json = detail::parse_json(source, max_serialized_semantic_bytes, 300000);
    if (!json) return {std::nullopt, std::move(json.diagnostics)};
    try {
        auto snapshot = read(json.value->root);
        auto errors = validate(snapshot);
        if (!errors.empty()) {
            detail::annotate_json(errors, *json.value);
            return {std::nullopt, std::move(errors)};
        }
        return {std::move(snapshot), {}};
    } catch (Failure& failure) {
        detail::annotate_json(failure.diagnostics, *json.value);
        return {std::nullopt, std::move(failure.diagnostics)};
    }
}
Result<std::string> save_semantics(const SemanticSnapshot& snapshot) {
    auto errors = validate(snapshot);
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    auto result = detail::write_json_value(write(snapshot)) + '\n';
    if (result.size() > max_serialized_semantic_bytes)
        return {std::nullopt, {{"size_limit", Severity::error, "", "Canonical semantics exceed 16 MiB.", {}}}};
    return {std::move(result), {}};
}
} // namespace tessera
