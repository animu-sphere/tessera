// Comparison of two loaded snapshot bundles.
#include <tessera/snapshot/snapshot_diff.hpp>
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace tessera {
namespace {

using snapshot_detail::quote;
using snapshot_detail::write_json;

const JsonValue& null_value() {
    static const JsonValue value;
    return value;
}
const JsonValue* at(const JsonValue& value, std::string_view key) {
    const auto* object = std::get_if<JsonValue::Object>(&value.value);
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}
const JsonValue& field(const JsonValue& value, std::string_view key) {
    const auto* found = at(value, key);
    return found ? *found : null_value();
}
const JsonValue::Array& list(const JsonValue& value) {
    static const JsonValue::Array empty;
    const auto* array = std::get_if<JsonValue::Array>(&value.value);
    return array ? *array : empty;
}
std::optional<std::uint32_t> index(const JsonValue& value) {
    const auto* number = std::get_if<double>(&value.value);
    if (!number) return std::nullopt;
    return std::uint32_t(*number);
}
JsonValue text(std::string value) { return JsonValue{std::move(value)}; }

// Numbers within `tolerance` are equal; everything else compares exactly.
bool equal(const JsonValue& a, const JsonValue& b, double tolerance) {
    if (a.value.index() != b.value.index()) return false;
    if (const auto* x = std::get_if<double>(&a.value)) return std::abs(*x - std::get<double>(b.value)) <= tolerance;
    if (const auto* x = std::get_if<JsonValue::Array>(&a.value)) {
        const auto& y = std::get<JsonValue::Array>(b.value);
        if (x->size() != y.size()) return false;
        for (std::size_t i = 0; i < x->size(); ++i)
            if (!equal((*x)[i], y[i], tolerance)) return false;
        return true;
    }
    if (const auto* x = std::get_if<JsonValue::Object>(&a.value)) {
        const auto& y = std::get<JsonValue::Object>(b.value);
        if (x->size() != y.size()) return false;
        for (auto i = x->begin(), j = y.begin(); i != x->end(); ++i, ++j)
            if (i->first != j->first || !equal(i->second, j->second, tolerance)) return false;
        return true;
    }
    return a == b;
}

// Changed fields of two flat objects; a field missing on one side compares as null.
std::vector<SnapshotFieldChange> changes(const JsonValue::Object& before, const JsonValue::Object& after,
                                         double tolerance) {
    std::vector<SnapshotFieldChange> result;
    std::vector<std::string> keys;
    for (const auto& [key, value] : before) keys.push_back(key);
    for (const auto& [key, value] : after)
        if (!before.contains(key)) keys.push_back(key);
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) {
        const auto b = before.find(key), a = after.find(key);
        const auto& bv = b == before.end() ? null_value() : b->second;
        const auto& av = a == after.end() ? null_value() : a->second;
        if (!equal(bv, av, tolerance)) result.push_back({key, bv, av});
    }
    return result;
}

JsonValue key_json(const SnapshotNodeKey& key) {
    JsonValue::Object object;
    if (key.id) {
        object["id"] = text(*key.id);
    } else {
        object["within"] = key.within ? text(*key.within) : JsonValue{};
        object["ordinal"] = JsonValue{double(key.ordinal)};
    }
    return {std::move(object)};
}

// Node identities of one generation's document, from its preorder layout.
struct Nodes {
    std::vector<SnapshotNodeKey> keys;
    std::map<SnapshotNodeKey, std::uint32_t> by_key;

    explicit Nodes(const JsonValue& layout) {
        const auto& nodes = list(field(layout, "nodes"));
        std::vector<std::optional<std::string>> scope; // The identified scope each node passes to its children.
        std::map<std::optional<std::string>, std::uint32_t> ordinals;
        for (const auto& node : nodes) {
            const auto* id = std::get_if<std::string>(&field(node, "id").value);
            const auto parent = index(field(node, "parent"));
            SnapshotNodeKey key;
            if (id) {
                key.id = *id;
                scope.push_back(*id);
            } else {
                key.within = parent && *parent < scope.size() ? scope[*parent] : std::nullopt;
                key.ordinal = ordinals[key.within]++;
                scope.push_back(key.within);
            }
            by_key.emplace(key, std::uint32_t(keys.size()));
            keys.push_back(std::move(key));
        }
    }
    JsonValue reference(const JsonValue& node) const {
        const auto i = index(node);
        return i && *i < keys.size() ? key_json(keys[*i]) : JsonValue{};
    }
};

using Entries = std::map<std::uint32_t, JsonValue::Object>; // Normalized entries by preorder index.

Entries layout_entries(const JsonValue& layout, const Nodes& nodes) {
    Entries result;
    const auto& list_nodes = list(field(layout, "nodes"));
    for (std::uint32_t i = 0; i < list_nodes.size(); ++i) {
        auto object = std::get<JsonValue::Object>(list_nodes[i].value);
        object.erase("node");
        object.erase("id");
        object["parent"] = nodes.reference(object["parent"]);
        result.emplace(i, std::move(object));
    }
    return result;
}

Entries semantic_entries(const std::optional<JsonValue>& semantics, const Nodes& nodes) {
    Entries result;
    if (!semantics) return result;
    const auto& records = list(field(*semantics, "nodes"));
    for (const auto& record : records) {
        auto object = std::get<JsonValue::Object>(record.value);
        const auto node = index(object["node"]);
        if (!node) continue;
        // parent is an entry index; labelled_by is a node index.
        const auto parent = index(object["parent"]);
        object["parent"] =
            parent && *parent < records.size() ? nodes.reference(field(records[*parent], "node")) : JsonValue{};
        object["labelled_by"] = nodes.reference(object["labelled_by"]);
        object.erase("node");
        object.erase("id");
        result.emplace(*node, std::move(object));
    }
    return result;
}

void diff_entries(std::size_t pair, const Nodes& before_nodes, const Entries& before, const Nodes& after_nodes,
                  const Entries& after, double tolerance, std::vector<SnapshotNodeChange>& out) {
    const auto find =
        [](const Nodes& nodes, const Entries& entries,
           const SnapshotNodeKey& key) -> std::pair<std::optional<std::uint32_t>, const JsonValue::Object*> {
        const auto node = nodes.by_key.find(key);
        if (node == nodes.by_key.end()) return {std::nullopt, nullptr};
        const auto entry = entries.find(node->second);
        if (entry == entries.end()) return {std::nullopt, nullptr};
        return {node->second, &entry->second};
    };
    static const JsonValue::Object empty;
    for (const auto& [i, entry] : after) {
        const auto& key = after_nodes.keys[i];
        const auto [j, previous] = find(before_nodes, before, key);
        if (!previous) {
            out.push_back({pair, key, std::nullopt, i, SnapshotChange::added, changes(empty, entry, 0)});
        } else if (auto fields = changes(*previous, entry, tolerance); !fields.empty()) {
            out.push_back({pair, key, j, i, SnapshotChange::changed, std::move(fields)});
        }
    }
    for (const auto& [j, entry] : before) {
        const auto& key = before_nodes.keys[j];
        if (!find(after_nodes, after, key).second)
            out.push_back({pair, key, j, std::nullopt, SnapshotChange::removed, changes(entry, empty, 0)});
    }
}

JsonValue::Object paint_fields(const JsonValue& paint) {
    JsonValue::Object result;
    for (const auto key : {"commands", "glyphs", "time_dependent"}) result[key] = field(paint, key);
    if (const auto* kinds = std::get_if<JsonValue::Object>(&field(paint, "kinds").value))
        for (const auto& [kind, count] : *kinds) result["kinds." + kind] = count;
    return result;
}

SnapshotPixelComparison pixels(std::size_t pair, const std::optional<CapturedImage>& before,
                               const std::optional<CapturedImage>& after, const SnapshotDiffOptions& options) {
    SnapshotPixelComparison result{pair};
    if (before) result.before = before->extent;
    if (after) result.after = after->extent;
    if (!before || !after) {
        result.different = before.has_value() != after.has_value();
        return result;
    }
    if (before->extent != after->extent) {
        result.different = true;
        return result;
    }
    const auto width = before->extent.width, height = before->extent.height;
    std::uint32_t left = width, top = height, right = 0, bottom = 0;
    for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::size_t at = (std::size_t(y) * width + x) * 4;
            int delta = 0;
            for (std::size_t c = 0; c < 4; ++c)
                delta = std::max(delta, std::abs(int(before->pixels[at + c]) - int(after->pixels[at + c])));
            result.max_delta = std::max(result.max_delta, std::uint8_t(delta));
            if (delta <= options.pixel_threshold) continue;
            ++result.differing;
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x);
            bottom = std::max(bottom, y);
        }
    result.compared = std::uint64_t(width) * height;
    if (result.differing) result.bounds = std::array<std::uint32_t, 4>{left, top, right - left + 1, bottom - top + 1};
    result.different = result.differing > options.pixel_count;
    return result;
}

JsonValue environment(const LoadedSnapshotBundle& bundle) {
    const auto& env = field(bundle.manifest, "environment");
    return {JsonValue::Object{
        {"locale", field(env, "locale")}, {"scale", field(env, "scale")}, {"text", field(env, "text")}}};
}
JsonValue capture_backend(const LoadedSnapshotBundle& bundle) {
    auto capture = field(bundle.manifest, "capture");
    if (auto* object = std::get_if<JsonValue::Object>(&capture.value)) object->erase("glyphs");
    return capture;
}

std::string change_name(SnapshotChange change) {
    return change == SnapshotChange::added ? "added" : change == SnapshotChange::removed ? "removed" : "changed";
}
std::string count(std::uint64_t value) { return std::to_string(value); }
std::string optional_index(std::optional<std::uint32_t> value) { return value ? count(*value) : "null"; }
std::string fields_json(const std::vector<SnapshotFieldChange>& fields) {
    JsonValue::Object object;
    for (const auto& f : fields) object[f.field] = {JsonValue::Object{{"after", f.after}, {"before", f.before}}};
    return write_json({std::move(object)});
}
std::string lines(const std::vector<std::string>& entries) {
    if (entries.empty()) return "[]";
    std::string out = "[\n";
    for (std::size_t i = 0; i < entries.size(); ++i)
        out += "    " + entries[i] + (i + 1 < entries.size() ? ",\n" : "\n");
    return out + "  ]";
}
std::string extent(const std::optional<CaptureExtent>& value) {
    return value ? "[" + count(value->width) + ", " + count(value->height) + "]" : "null";
}

} // namespace

Result<SnapshotDiff> diff_snapshot_bundles(const LoadedSnapshotBundle& before, const LoadedSnapshotBundle& after,
                                           const SnapshotDiffOptions& options) {
    if (!std::isfinite(options.geometry_tolerance) || options.geometry_tolerance < 0)
        return {std::nullopt,
                {{"invalid_diff_option", Severity::error, "/options/geometry_tolerance",
                  "Use a finite, nonnegative tolerance in logical units."}}};
    SnapshotDiff diff;
    diff.options = options;
    const auto& bg = before.generations;
    const auto& ag = after.generations;
    const bool same =
        bg.size() == ag.size() && std::equal(bg.begin(), bg.end(), ag.begin(),
                                             [](const auto& a, const auto& b) { return a.generation == b.generation; });
    std::vector<std::pair<std::size_t, std::size_t>> positions;
    if (same)
        for (std::size_t i = 0; i < bg.size(); ++i) positions.emplace_back(i, i);
    else if (!bg.empty() && !ag.empty())
        positions.emplace_back(bg.size() - 1, ag.size() - 1);
    for (const auto& [b, a] : positions) diff.pairs.push_back({bg[b].generation, ag[a].generation});

    const auto condition = [&](std::string name, std::optional<std::size_t> pair, JsonValue b, JsonValue a) {
        if (!(b == a)) diff.conditions.push_back({std::move(name), pair, std::move(b), std::move(a)});
    };
    const auto& benv = field(before.manifest, "environment");
    const auto& aenv = field(after.manifest, "environment");
    condition("bundle_version", std::nullopt, field(before.manifest, "version"), field(after.manifest, "version"));
    condition("environment", std::nullopt, environment(before), environment(after));
    condition("text_service", std::nullopt, field(benv, "text_service"), field(aenv, "text_service"));
    condition("capture_backend", std::nullopt, capture_backend(before), capture_backend(after));
    condition("glyphs", std::nullopt, field(field(before.manifest, "capture"), "glyphs"),
              field(field(after.manifest, "capture"), "glyphs"));
    for (std::size_t p = 0; p < positions.size(); ++p) {
        const auto& b = bg[positions[p].first].entry;
        const auto& a = ag[positions[p].second].entry;
        condition("viewport", p, field(b, "viewport"), field(a, "viewport"));
        condition("scale", p, field(b, "device_scale"), field(a, "device_scale"));
    }
    if (!diff.conditions.empty()) {
        diff.status = SnapshotDiffStatus::incompatible;
        return {std::move(diff), {}};
    }

    for (std::size_t p = 0; p < positions.size(); ++p) {
        const auto& b = bg[positions[p].first];
        const auto& a = ag[positions[p].second];
        const Nodes bn(b.layout), an(a.layout);
        diff_entries(p, bn, semantic_entries(b.semantics, bn), an, semantic_entries(a.semantics, an), 0, diff.semantic);
        diff_entries(p, bn, layout_entries(b.layout, bn), an, layout_entries(a.layout, an), options.geometry_tolerance,
                     diff.layout);
        if (auto fields = changes(paint_fields(b.paint), paint_fields(a.paint), 0); !fields.empty())
            diff.paint.push_back({p, std::move(fields)});
        diff.pixels.push_back(pixels(p, b.frame, a.frame, options));
    }

    // Actions compare by position. A node index is noise when both entries name the node by author ID.
    const auto& ba = list(field(before.actions, "actions"));
    const auto& aa = list(field(after.actions, "actions"));
    const auto comparable = [](const JsonValue& action) {
        auto object = std::get<JsonValue::Object>(action.value);
        if (std::holds_alternative<std::string>(object["id"].value)) object.erase("node");
        return object;
    };
    for (std::size_t i = 0; i < std::max(ba.size(), aa.size()); ++i) {
        if (i >= ba.size())
            diff.actions.push_back({i, SnapshotChange::added, {}, aa[i]});
        else if (i >= aa.size())
            diff.actions.push_back({i, SnapshotChange::removed, ba[i], {}});
        else if (comparable(ba[i]) != comparable(aa[i]))
            diff.actions.push_back({i, SnapshotChange::changed, ba[i], aa[i]});
    }

    const bool pixel_difference =
        std::any_of(diff.pixels.begin(), diff.pixels.end(), [](const auto& p) { return p.different; });
    if (!diff.semantic.empty())
        diff.first_difference = "semantic";
    else if (!diff.layout.empty())
        diff.first_difference = "layout";
    else if (!diff.actions.empty())
        diff.first_difference = "actions";
    else if (!diff.paint.empty())
        diff.first_difference = "paint";
    else if (pixel_difference)
        diff.first_difference = "pixels";
    diff.status = diff.first_difference ? SnapshotDiffStatus::different : SnapshotDiffStatus::equal;
    return {std::move(diff), {}};
}

std::string save_snapshot_diff(const SnapshotDiff& diff) {
    const bool judged = diff.status != SnapshotDiffStatus::incompatible;
    std::string out =
        "{\n  \"format\": \"tessera-snapshot-diff\",\n  \"version\": " + count(snapshot_diff_version) +
        ",\n  \"status\": " +
        quote(diff.status == SnapshotDiffStatus::equal       ? "equal"
              : diff.status == SnapshotDiffStatus::different ? "different"
                                                             : "incompatible") +
        ",\n  \"first_difference\": " + (diff.first_difference ? quote(*diff.first_difference) : "null") +
        ",\n  \"options\": {\"geometry_tolerance\": " + snapshot_detail::number(diff.options.geometry_tolerance) +
        ", \"pixel_threshold\": " + count(diff.options.pixel_threshold) +
        ", \"pixel_count\": " + count(diff.options.pixel_count) + "}";
    std::vector<std::string> entries;
    for (const auto& p : diff.pairs)
        entries.push_back("{\"before\": " + count(p.before) + ", \"after\": " + count(p.after) + "}");
    out += ",\n  \"pairs\": " + lines(entries);
    entries.clear();
    for (const auto& c : diff.conditions)
        entries.push_back("{\"condition\": " + quote(c.condition) +
                          (c.pair ? ", \"pair\": " + count(*c.pair) : std::string()) +
                          ", \"before\": " + write_json(c.before) + ", \"after\": " + write_json(c.after) + "}");
    out += ",\n  \"conditions\": " + lines(entries);

    const auto nodes = [&](const std::vector<SnapshotNodeChange>& changes) {
        std::vector<std::string> result;
        for (const auto& c : changes)
            result.push_back("{\"pair\": " + count(c.pair) + ", \"node\": " + write_json(key_json(c.node)) +
                             ", \"before\": " + optional_index(c.before) + ", \"after\": " + optional_index(c.after) +
                             ", \"change\": " + quote(change_name(c.change)) +
                             ", \"fields\": " + fields_json(c.fields) + "}");
        return lines(result);
    };
    out += ",\n  \"semantic\": " + (judged ? nodes(diff.semantic) : "null");
    out += ",\n  \"layout\": " + (judged ? nodes(diff.layout) : "null");
    entries.clear();
    for (const auto& a : diff.actions)
        entries.push_back("{\"index\": " + count(a.index) + ", \"change\": " + quote(change_name(a.change)) +
                          ", \"before\": " + write_json(a.before) + ", \"after\": " + write_json(a.after) + "}");
    out += ",\n  \"actions\": " + (judged ? lines(entries) : "null");
    entries.clear();
    for (const auto& p : diff.paint)
        entries.push_back("{\"pair\": " + count(p.pair) + ", \"fields\": " + fields_json(p.fields) + "}");
    out += ",\n  \"paint\": " + (judged ? lines(entries) : "null");
    entries.clear();
    for (const auto& p : diff.pixels) {
        std::string bounds = "null";
        if (p.bounds)
            bounds = "[" + count((*p.bounds)[0]) + ", " + count((*p.bounds)[1]) + ", " + count((*p.bounds)[2]) + ", " +
                     count((*p.bounds)[3]) + "]";
        entries.push_back("{\"pair\": " + count(p.pair) + ", \"before\": " + extent(p.before) +
                          ", \"after\": " + extent(p.after) + ", \"compared\": " + count(p.compared) +
                          ", \"differing\": " + count(p.differing) + ", \"max_delta\": " + count(p.max_delta) +
                          ", \"bounds\": " + bounds + ", \"different\": " + (p.different ? "true" : "false") + "}");
    }
    out += ",\n  \"pixels\": " + (judged ? lines(entries) : "null");
    return out + "\n}\n";
}

} // namespace tessera
