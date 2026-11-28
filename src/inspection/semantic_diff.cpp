#include <tessera/inspection/semantic_diff.hpp>
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <type_traits>
#include <utility>

namespace tessera {
namespace {
using Object = JsonValue::Object;
using Array = JsonValue::Array;

constexpr std::array<std::string_view, semantic_change_kind_count> kind_names{
    "focus_changed",     "node_added",         "node_removed",          "node_moved",
    "role_changed",      "name_changed",       "name_source_changed",   "enabled_changed",
    "focusable_changed", "selection_changed",  "checked_changed",       "expanded_changed",
    "value_changed",     "item_count_changed", "item_position_changed", "multiselectable_changed",
    "action_added",      "action_removed"};
// Semantic JSON v1 spellings, in SemanticRole and NameSource order.
constexpr std::array<std::string_view, 27> role_names{
    "root",      "button",    "text",       "separator",    "dialog",       "list",        "tree",
    "list_item", "tree_item", "menu",       "menu_item",    "scrollbar",    "radio_group", "radio",
    "checkbox",  "switch",    "text_input", "number_input", "tab_list",     "tab",         "toolbar",
    "combobox",  "listbox",   "option",     "group",        "color_picker", "text_editor"};
constexpr std::array<std::string_view, 3> source_names{"none", "relationship", "content"};

void error(std::vector<Diagnostic>& out, std::string code, std::string path, std::string message) {
    out.push_back({std::move(code), Severity::error, std::move(path), std::move(message), {}});
}
void relocate(std::vector<Diagnostic>& out, std::vector<Diagnostic> diagnostics, std::string_view prefix) {
    for (auto& diagnostic : diagnostics) {
        diagnostic.path = std::string(prefix) + diagnostic.path;
        out.push_back(std::move(diagnostic));
    }
}

// One snapshot's identities, hierarchy and scope, indexed by entry.
struct View {
    const SemanticSnapshot& snapshot;
    std::vector<SemanticNodeKey> keys;
    std::map<SemanticNodeKey, std::uint32_t> by_key;
    std::vector<std::uint32_t> position; // Index among the entry's semantic siblings.
    std::vector<bool> in_scope;
    std::optional<std::uint32_t> focused;

    View(const SemanticSnapshot& s, const std::optional<std::string>& scope) : snapshot(s) {
        const auto& nodes = s.nodes;
        std::vector<std::optional<std::string>> identified(nodes.size()); // Nearest identified entry, inclusive.
        std::map<std::pair<std::optional<std::string>, SemanticRole>, std::uint32_t> ordinals;
        std::map<std::uint32_t, std::uint32_t> children; // Child count by parent entry; roots use the sentinel.
        for (std::uint32_t i = 0; i < nodes.size(); ++i) {
            const auto& entry = nodes[i];
            const bool child = entry.parent != no_semantic_parent;
            const auto inherited = child ? identified[entry.parent] : std::nullopt;
            SemanticNodeKey key;
            if (entry.id) {
                key.id = entry.id;
                identified[i] = entry.id;
            } else {
                key.within = inherited;
                key.role = entry.role;
                key.ordinal = ordinals[{inherited, entry.role}]++;
                identified[i] = inherited;
            }
            by_key.emplace(key, i);
            keys.push_back(std::move(key));
            position.push_back(children[entry.parent]++);
            in_scope.push_back(!scope || entry.id == scope || (child && in_scope[entry.parent]));
            if (entry.focused) focused = i;
        }
    }
    std::optional<SemanticNodeKey> parent(std::uint32_t i) const {
        const auto p = snapshot.nodes[i].parent;
        return p == no_semantic_parent ? std::nullopt : std::optional(keys[p]);
    }
    SemanticPlacement placement(std::uint32_t i) const { return {parent(i), position[i]}; }
    std::optional<std::uint32_t> find(const SemanticNodeKey& key) const {
        const auto found = by_key.find(key);
        return found == by_key.end() ? std::nullopt : std::optional(found->second);
    }
};

// Longest increasing subsequence of `values` (distinct), as a membership mask.
std::vector<bool> increasing(const std::vector<std::uint32_t>& values) {
    std::vector<std::size_t> tails, previous(values.size(), values.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto at = std::lower_bound(tails.begin(), tails.end(), values[i],
                                         [&](std::size_t t, std::uint32_t v) { return values[t] < v; });
        if (at != tails.begin()) previous[i] = *(at - 1);
        if (at == tails.end())
            tails.push_back(i);
        else
            *at = i;
    }
    std::vector<bool> result(values.size());
    for (auto i = tails.empty() ? values.size() : tails.back(); i < values.size(); i = previous[i]) result[i] = true;
    return result;
}

// After entries that keep their parent but change order among siblings that also kept it. Siblings on the
// longest run that keeps its relative order stay; the others moved.
std::vector<bool> reordered(const View& before, const View& after,
                            const std::vector<std::optional<std::uint32_t>>& match) {
    std::map<std::uint32_t, std::vector<std::uint32_t>> groups; // After entries by after parent entry.
    for (std::uint32_t a = 0; a < after.keys.size(); ++a)
        if (match[a] && before.parent(*match[a]) == after.parent(a))
            groups[after.snapshot.nodes[a].parent].push_back(a);
    std::vector<bool> moved(after.keys.size());
    for (const auto& [parent, members] : groups) {
        std::vector<std::uint32_t> order;
        for (const auto a : members) order.push_back(before.position[*match[a]]);
        const auto kept = increasing(order);
        for (std::size_t i = 0; i < members.size(); ++i) moved[members[i]] = !kept[i];
    }
    return moved;
}

template<class T> SemanticChangeValue optional_value(const std::optional<T>& value) {
    if (!value) return {};
    return SemanticChangeValue{*value};
}
bool toggle(SemanticRole role) { return role == SemanticRole::checkbox || role == SemanticRole::switch_; }

class Builder {
public:
    Builder(const SemanticDiffOptions& options, std::vector<SemanticChange>& out) : out_(out) {
        for (const auto kind : options.kinds) wanted_[static_cast<std::size_t>(kind)] = true;
        if (options.kinds.empty()) wanted_.fill(true);
    }
    void add(SemanticChangeKind kind, const SemanticChange& base, SemanticChangeValue before,
             SemanticChangeValue after) {
        // A reorder can leave a moved entry at its old index when siblings are also added and removed.
        if (!wanted_[static_cast<std::size_t>(kind)] || (kind != SemanticChangeKind::node_moved && before == after))
            return;
        auto change = base;
        change.kind = kind;
        change.before = std::move(before);
        change.after = std::move(after);
        out_.push_back(std::move(change));
    }
    void fields(const SemanticChange& base, const SemanticRecord& b, const SemanticRecord& a) {
        using K = SemanticChangeKind;
        add(K::role_changed, base, b.role, a.role);
        add(K::name_changed, base, b.name, a.name);
        add(K::name_source_changed, base, b.name_source, a.name_source);
        add(K::enabled_changed, base, b.enabled, a.enabled);
        add(K::focusable_changed, base, b.focusable, a.focusable);
        add(toggle(a.role) ? K::checked_changed : K::selection_changed, base, optional_value(b.selected),
            optional_value(a.selected));
        add(K::expanded_changed, base, optional_value(b.expanded), optional_value(a.expanded));
        add(K::value_changed, base, optional_value(b.value), optional_value(a.value));
        add(K::item_count_changed, base, optional_value(b.item_count), optional_value(a.item_count));
        add(K::item_position_changed, base, optional_value(b.item_position), optional_value(a.item_position));
        add(K::multiselectable_changed, base, optional_value(b.multiselectable), optional_value(a.multiselectable));
        for (const auto& action : a.actions)
            if (std::find(b.actions.begin(), b.actions.end(), action) == b.actions.end())
                add(K::action_added, base, {}, action);
        for (const auto& action : b.actions)
            if (std::find(a.actions.begin(), a.actions.end(), action) == a.actions.end())
                add(K::action_removed, base, action, {});
    }

private:
    std::vector<SemanticChange>& out_;
    std::array<bool, semantic_change_kind_count> wanted_{};
};

JsonValue text(std::string_view value) { return JsonValue{std::string(value)}; }
JsonValue number(std::uint64_t value) { return JsonValue{static_cast<double>(value)}; }
JsonValue optional_number(std::optional<std::uint32_t> value) { return value ? number(*value) : JsonValue{}; }
JsonValue key_json(const SemanticNodeKey& key) {
    if (key.id) return JsonValue{Object{{"id", text(*key.id)}}};
    return JsonValue{Object{{"within", key.within ? text(*key.within) : JsonValue{}},
                            {"role", text(role_names[static_cast<std::size_t>(key.role)])},
                            {"ordinal", number(key.ordinal)}}};
}
JsonValue placement_json(const SemanticPlacement& placement) {
    return JsonValue{Object{{"parent", placement.parent ? key_json(*placement.parent) : JsonValue{}},
                            {"index", number(placement.index)}}};
}
JsonValue action_json(const SemanticAction& action) {
    return JsonValue{Object{{"binding", text(action.binding)}, {"action", text(action.action)}}};
}
// The entry's Semantic JSON v1 fields without its snapshot-specific indices, which the change and
// placement carry instead.
JsonValue entry_json(const SemanticEntry& entry) {
    const auto& r = entry.record;
    Array actions;
    for (const auto& action : r.actions) actions.push_back(action_json(action));
    Object o{{"placement", placement_json(entry.placement)},
             {"id", r.id ? text(*r.id) : JsonValue{}},
             {"role", text(role_names[static_cast<std::size_t>(r.role)])},
             {"name", text(r.name)},
             {"name_source", text(source_names[static_cast<std::size_t>(r.name_source)])},
             {"enabled", JsonValue{r.enabled}},
             {"focusable", JsonValue{r.focusable}},
             {"focused", JsonValue{r.focused}},
             {"actions", JsonValue{std::move(actions)}}};
    if (r.selected) o["selected"] = JsonValue{*r.selected};
    if (r.expanded) o["expanded"] = JsonValue{*r.expanded};
    if (r.value) o["value"] = text(*r.value);
    if (r.item_count) o["item_count"] = number(*r.item_count);
    if (r.item_position) o["item_position"] = number(*r.item_position);
    if (r.multiselectable) o["multiselectable"] = JsonValue{*r.multiselectable};
    return JsonValue{std::move(o)};
}
JsonValue value_json(const SemanticChangeValue& value) {
    return std::visit(
        [](const auto& v) -> JsonValue {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return {};
            else if constexpr (std::is_same_v<T, bool>)
                return JsonValue{v};
            else if constexpr (std::is_same_v<T, std::uint64_t>)
                return number(v);
            else if constexpr (std::is_same_v<T, std::string>)
                return text(v);
            else if constexpr (std::is_same_v<T, SemanticRole>)
                return text(role_names[static_cast<std::size_t>(v)]);
            else if constexpr (std::is_same_v<T, NameSource>)
                return text(source_names[static_cast<std::size_t>(v)]);
            else if constexpr (std::is_same_v<T, SemanticNodeKey>)
                return key_json(v);
            else if constexpr (std::is_same_v<T, SemanticPlacement>)
                return placement_json(v);
            else if constexpr (std::is_same_v<T, SemanticAction>)
                return action_json(v);
            else
                return entry_json(v);
        },
        value);
}
} // namespace

std::string_view semantic_change_name(SemanticChangeKind kind) noexcept {
    const auto i = static_cast<std::size_t>(kind);
    return i < kind_names.size() ? kind_names[i] : std::string_view{};
}

const SemanticChange* SemanticDiff::find(std::string_view id, SemanticChangeKind kind) const noexcept {
    for (const auto& change : changes_)
        if (change.kind == kind && change.target && change.target->id == id) return &change;
    return nullptr;
}

Result<SemanticDiff> diff_semantics(const SemanticSnapshot& before, const SemanticSnapshot& after,
                                    const SemanticDiffOptions& options) {
    std::vector<Diagnostic> errors;
    relocate(errors, validate(before), "/before");
    relocate(errors, validate(after), "/after");
    for (std::size_t i = 0; i < options.kinds.size(); ++i)
        if (static_cast<std::size_t>(options.kinds[i]) >= semantic_change_kind_count)
            error(errors, "unknown_value", "/options/kinds/" + std::to_string(i),
                  "Use a declared semantic change kind.");
    if (options.scope && (options.scope->empty() || !detail::valid_utf8(*options.scope) ||
                          std::any_of(options.scope->begin(), options.scope->end(),
                                      [](unsigned char c) { return c < 0x20 || c == 0x7f; })))
        error(errors, "invalid_identifier", "/options/scope", "Use a nonempty UTF-8 author ID without ASCII controls.");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto has_scope = [&](const SemanticSnapshot& snapshot) {
        return std::any_of(snapshot.nodes.begin(), snapshot.nodes.end(),
                           [&](const SemanticRecord& entry) { return entry.id == options.scope; });
    };
    if (options.scope && !has_scope(before) && !has_scope(after))
        return {std::nullopt,
                {{"target_not_found",
                  Severity::error,
                  "/options/scope",
                  "Scope the diff to an author ID that has a semantic entry in either snapshot.",
                  {}}}};

    SemanticDiff diff;
    diff.from_generation_ = before.generation;
    diff.to_generation_ = after.generation;
    diff.scope_ = options.scope;
    diff.kinds_ = options.kinds;
    std::sort(diff.kinds_.begin(), diff.kinds_.end());
    diff.kinds_.erase(std::unique(diff.kinds_.begin(), diff.kinds_.end()), diff.kinds_.end());
    if (diff.kinds_.size() == semantic_change_kind_count) diff.kinds_.clear();

    const View b(before, options.scope), a(after, options.scope);
    std::vector<std::optional<std::uint32_t>> match(a.keys.size());
    std::vector<bool> matched(b.keys.size());
    for (std::uint32_t i = 0; i < a.keys.size(); ++i)
        if ((match[i] = b.find(a.keys[i]))) matched[*match[i]] = true;
    const auto moved = reordered(b, a, match);
    Builder builder(options, diff.changes_);
    using K = SemanticChangeKind;

    const auto focus_key = [](const View& view) {
        return view.focused ? SemanticChangeValue{view.keys[*view.focused]} : SemanticChangeValue{};
    };
    if ((b.focused && b.in_scope[*b.focused]) || (a.focused && a.in_scope[*a.focused])) {
        SemanticChange base;
        if (b.focused) base.before_node = before.nodes[*b.focused].node;
        if (a.focused) base.after_node = after.nodes[*a.focused].node;
        builder.add(K::focus_changed, base, focus_key(b), focus_key(a));
    }
    for (std::uint32_t i = 0; i < a.keys.size(); ++i) {
        const auto j = match[i];
        if (!a.in_scope[i] && !(j && b.in_scope[*j])) continue;
        SemanticChange base{.target = a.keys[i], .after_node = after.nodes[i].node};
        if (!j) {
            builder.add(K::node_added, base, {}, SemanticEntry{a.placement(i), after.nodes[i]});
            continue;
        }
        base.before_node = before.nodes[*j].node;
        if (moved[i] || b.parent(*j) != a.parent(i)) builder.add(K::node_moved, base, b.placement(*j), a.placement(i));
        builder.fields(base, before.nodes[*j], after.nodes[i]);
    }
    for (std::uint32_t j = 0; j < b.keys.size(); ++j)
        if (!matched[j] && b.in_scope[j])
            builder.add(K::node_removed, {.target = b.keys[j], .before_node = before.nodes[j].node},
                        SemanticEntry{b.placement(j), before.nodes[j]}, {});
    return {std::move(diff), {}};
}

Result<std::string> save_semantic_diff(const SemanticDiff& diff) {
    Array kinds;
    for (const auto kind : diff.kinds()) kinds.push_back(text(semantic_change_name(kind)));
    Array changes;
    for (const auto& change : diff.changes())
        changes.push_back(JsonValue{Object{{"kind", text(semantic_change_name(change.kind))},
                                           {"target", change.target ? key_json(*change.target) : JsonValue{}},
                                           {"before_node", optional_number(change.before_node)},
                                           {"after_node", optional_number(change.after_node)},
                                           {"before", value_json(change.before)},
                                           {"after", value_json(change.after)}}});
    const JsonValue root{Object{{"format", text("tessera-semantic-diff")},
                                {"version", number(diff.version())},
                                {"from_generation", text(std::to_string(diff.from_generation()))},
                                {"to_generation", text(std::to_string(diff.to_generation()))},
                                {"scope", diff.scope() ? text(*diff.scope()) : JsonValue{}},
                                {"kinds", diff.kinds().empty() ? JsonValue{} : JsonValue{std::move(kinds)}},
                                {"changes", JsonValue{std::move(changes)}}}};
    auto result = detail::write_json_value(root) + '\n';
    if (result.size() > max_serialized_semantic_diff_bytes)
        return {std::nullopt, {{"size_limit", Severity::error, "", "The canonical semantic diff exceeds 32 MiB.", {}}}};
    return {std::move(result), {}};
}
} // namespace tessera
