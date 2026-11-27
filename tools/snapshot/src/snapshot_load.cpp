// Reading and validating snapshot bundle v1 directories.
#include <tessera/snapshot/snapshot_bundle.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include <tessera/semantics/semantic_snapshot.hpp>
#include "json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <set>
#include <system_error>

namespace tessera {
namespace {

using snapshot_detail::identity;
using snapshot_detail::parse_json;

constexpr std::size_t manifest_bytes = std::size_t(4) << 20;
constexpr std::size_t member_bytes = std::size_t(16) << 20; // layout, actions, diagnostics, tree.
constexpr std::size_t paint_bytes = std::size_t(64) << 10;
constexpr double max_integer = 9007199254740992.0; // 2^53: every smaller integer is exact in a double.

constexpr std::array<std::string_view, 10> paint_kinds{"rect",      "border",   "image",          "glyph_run",
                                                       "push_clip", "pop_clip", "push_transform", "pop_transform",
                                                       "shadow",    "stroke"};

std::string token(std::string_view key) {
    std::string out;
    for (const char c : key) {
        if (c == '~')
            out += "~0";
        else if (c == '/')
            out += "~1";
        else
            out += c;
    }
    return out;
}

void relocate(std::vector<Diagnostic>& diagnostics, const std::string& prefix) {
    for (auto& d : diagnostics) d.path = prefix + d.path;
}

const JsonValue* at(const JsonValue::Object* object, std::string_view key) {
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool null(const JsonValue* value) { return value && std::holds_alternative<std::nullptr_t>(value->value); }

// Schema checks over parsed members. A missing field is reported once by object(); getters then skip it.
struct Checker {
    std::vector<Diagnostic>& errors;

    void fail(std::string path, std::string message, std::string code = "invalid_member") {
        errors.push_back({std::move(code), Severity::error, std::move(path), std::move(message)});
    }

    const JsonValue::Object* object(const JsonValue* value, const std::string& path,
                                    std::initializer_list<std::string_view> required,
                                    std::initializer_list<std::string_view> optional = {}) {
        if (!value) return nullptr;
        const auto* object = std::get_if<JsonValue::Object>(&value->value);
        if (!object) {
            fail(path, "Expected an object.");
            return nullptr;
        }
        for (const auto key : required)
            if (!object->contains(key)) fail(path + "/" + token(key), "Missing required field.");
        for (const auto& [key, item] : *object)
            if (std::find(required.begin(), required.end(), key) == required.end() &&
                std::find(optional.begin(), optional.end(), key) == optional.end())
                fail(path + "/" + token(key), "Unknown field.");
        return object;
    }
    const JsonValue::Array* array(const JsonValue* value, const std::string& path, std::size_t max) {
        if (!value) return nullptr;
        const auto* array = std::get_if<JsonValue::Array>(&value->value);
        if (!array) {
            fail(path, "Expected an array.");
            return nullptr;
        }
        if (array->size() > max) {
            fail(path, "Array holds more than " + std::to_string(max) + " entries.");
            return nullptr;
        }
        return array;
    }
    std::optional<std::string> text(const JsonValue* value, const std::string& path) {
        if (!value) return std::nullopt;
        const auto* text = std::get_if<std::string>(&value->value);
        if (!text) {
            fail(path, "Expected a string.");
            return std::nullopt;
        }
        return *text;
    }
    std::optional<std::string> name(const JsonValue* value, const std::string& path) {
        auto result = text(value, path);
        if (result && !identity(*result)) {
            fail(path, "Use a 1-256 byte UTF-8 identity without controls.");
            return std::nullopt;
        }
        return result;
    }
    // Null or an identity; the outer optional is empty when the value is invalid or missing.
    std::optional<std::optional<std::string>> optional_name(const JsonValue* value, const std::string& path) {
        if (null(value)) return std::optional<std::string>{};
        auto result = name(value, path);
        if (!result) return std::nullopt;
        return std::optional<std::string>(std::move(*result));
    }
    void expect(const JsonValue* value, const std::string& path, std::string_view expected) {
        if (const auto actual = text(value, path); actual && *actual != expected)
            fail(path, "Expected \"" + std::string(expected) + "\".");
    }
    std::optional<bool> boolean(const JsonValue* value, const std::string& path) {
        if (!value) return std::nullopt;
        const auto* flag = std::get_if<bool>(&value->value);
        if (!flag) {
            fail(path, "Expected a boolean.");
            return std::nullopt;
        }
        return *flag;
    }
    std::optional<double> real(const JsonValue* value, const std::string& path) {
        if (!value) return std::nullopt;
        const auto* number = std::get_if<double>(&value->value);
        if (!number) {
            fail(path, "Expected a number.");
            return std::nullopt;
        }
        return *number;
    }
    std::optional<double> positive(const JsonValue* value, const std::string& path) {
        const auto number = real(value, path);
        if (number && !(*number > 0)) {
            fail(path, "Expected a positive number.");
            return std::nullopt;
        }
        return number;
    }
    std::optional<std::uint64_t> integer(const JsonValue* value, const std::string& path, double max = max_integer) {
        const auto number = real(value, path);
        if (!number) return std::nullopt;
        if (*number < 0 || *number > max || std::floor(*number) != *number) {
            fail(path, "Expected an integer from 0 to " + snapshot_detail::number(max) + ".");
            return std::nullopt;
        }
        return std::uint64_t(*number);
    }
    std::optional<std::vector<double>> numbers(const JsonValue* value, const std::string& path, std::size_t count) {
        const auto* list = array(value, path, count);
        if (!list) return std::nullopt;
        if (list->size() != count) {
            fail(path, "Expected " + std::to_string(count) + " numbers.");
            return std::nullopt;
        }
        std::vector<double> result;
        for (std::size_t i = 0; i < count; ++i) {
            const auto number = real(&(*list)[i], path + "/" + std::to_string(i));
            if (!number) return std::nullopt;
            result.push_back(*number);
        }
        return result;
    }
    // Member versions: fails as unsupported_version for any integer other than the supported ones.
    std::optional<std::uint64_t> version(const JsonValue* value, const std::string& path,
                                         std::initializer_list<std::uint64_t> supported = {1}) {
        const auto number = integer(value, path);
        if (number && std::find(supported.begin(), supported.end(), *number) == supported.end()) {
            fail(path, "Version " + std::to_string(*number) + " is not supported.", "unsupported_version");
            return std::nullopt;
        }
        return number;
    }
};

struct Loader {
    const SnapshotBundle& bundle;
    std::vector<Diagnostic> errors;
    Checker c{errors};
    std::set<std::string, std::less<>> listed;

    // Registers a listed member and returns its bytes when present within its bound.
    const std::string* bytes(const std::string& path, std::size_t bound) {
        listed.insert(path);
        const auto* file = bundle.find(path);
        if (!file) {
            c.fail("/" + path, "The manifest lists this member, but the bundle does not hold it.", "missing_member");
            return nullptr;
        }
        if (file->bytes.size() > bound) {
            c.fail("/" + path, "The member exceeds its " + std::to_string(bound) + "-byte bound.", "bundle_limit");
            return nullptr;
        }
        return &file->bytes;
    }
    std::optional<JsonValue> json(const std::string& path, std::size_t bound) {
        const auto* text = bytes(path, bound);
        if (!text) return std::nullopt;
        auto parsed = parse_json(*text, text->size() + 1);
        if (!parsed) {
            relocate(parsed.diagnostics, "/" + path);
            errors.insert(errors.end(), parsed.diagnostics.begin(), parsed.diagnostics.end());
            return std::nullopt;
        }
        return std::move(*parsed.value);
    }
    void take(std::vector<Diagnostic> diagnostics, const std::string& prefix) {
        relocate(diagnostics, prefix);
        for (auto& d : diagnostics)
            if (d.severity == Severity::error) errors.push_back(std::move(d));
    }

    // A listed member reference: {"path": <canonical>, ...} with the given fixed fields.
    void reference(const JsonValue::Object* object, const std::string& path, const std::string& canonical) {
        if (const auto stated = c.text(at(object, "path"), path + "/path"); stated && *stated != canonical)
            c.fail(path + "/path", "Expected the canonical member path \"" + canonical + "\".");
    }

    void layout(LoadedSnapshotGeneration& g, const std::string& file, std::uint64_t node_count,
                const std::vector<double>* viewport, std::optional<double> scale) {
        auto value = json(file, member_bytes);
        if (!value) return;
        const std::string p = "/" + file;
        const auto* o = c.object(&*value, p, {"format", "version", "generation", "viewport", "device_scale", "nodes"});
        c.expect(at(o, "format"), p + "/format", "tessera-layout");
        c.version(at(o, "version"), p + "/version");
        if (const auto generation = c.integer(at(o, "generation"), p + "/generation");
            generation && *generation != g.generation)
            c.fail(p + "/generation", "The layout belongs to another generation than its manifest entry.");
        if (const auto vp = c.numbers(at(o, "viewport"), p + "/viewport", 2); vp && viewport && *vp != *viewport)
            c.fail(p + "/viewport", "The viewport disagrees with the manifest entry.");
        if (const auto s = c.positive(at(o, "device_scale"), p + "/device_scale"); s && scale && *s != *scale)
            c.fail(p + "/device_scale", "The device scale disagrees with the manifest entry.");
        const auto* nodes = c.array(at(o, "nodes"), p + "/nodes", max_document_nodes);
        if (nodes && nodes->size() != node_count)
            c.fail(p + "/nodes", "The layout lists " + std::to_string(nodes->size()) +
                                     " nodes; the manifest entry has " + std::to_string(node_count) + ".");
        std::set<std::string, std::less<>> ids;
        for (std::size_t i = 0; nodes && i < nodes->size(); ++i) {
            const auto np = p + "/nodes/" + std::to_string(i);
            const auto* n = c.object(&(*nodes)[i], np, {"node", "parent", "kind", "id", "displayed"},
                                     {"visible", "border_box", "border", "padding", "clip", "scroll"});
            if (!n) continue;
            if (const auto index = c.integer(at(n, "node"), np + "/node"); index && *index != i)
                c.fail(np + "/node", "Nodes must be listed in preorder index order.");
            const auto* parent = at(n, "parent");
            if (i == 0 && parent && !null(parent)) c.fail(np + "/parent", "The root has no parent.");
            if (i > 0 && parent) {
                if (null(parent))
                    c.fail(np + "/parent", "Only the root has no parent.");
                else if (const auto index = c.integer(parent, np + "/parent"); index && *index >= i)
                    c.fail(np + "/parent", "A parent precedes its children in preorder.");
            }
            if (const auto kind = c.text(at(n, "kind"), np + "/kind"); kind && *kind != "box" && *kind != "text")
                c.fail(np + "/kind", "Expected \"box\" or \"text\".");
            if (const auto* id = at(n, "id"); id && !null(id))
                if (const auto text = c.text(id, np + "/id"); text && !ids.insert(*text).second)
                    c.fail(np + "/id", "Author IDs are unique within a document.");
            const auto displayed = c.boolean(at(n, "displayed"), np + "/displayed");
            if (!displayed) continue;
            if (!*displayed) {
                for (const auto key : {"visible", "border_box", "border", "padding", "clip", "scroll"})
                    if (n->contains(key)) c.fail(np + "/" + key, "A display-none node has no geometry.");
                continue;
            }
            for (const auto key : {"visible", "border_box", "border", "padding"})
                if (!n->contains(key)) c.fail(np + "/" + key, "Missing required field of a displayed node.");
            c.boolean(at(n, "visible"), np + "/visible");
            if (const auto box = c.numbers(at(n, "border_box"), np + "/border_box", 4);
                box && ((*box)[2] < 0 || (*box)[3] < 0))
                c.fail(np + "/border_box", "Box sizes are nonnegative.");
            c.numbers(at(n, "border"), np + "/border", 4);
            c.numbers(at(n, "padding"), np + "/padding", 4);
            if (n->contains("clip")) c.numbers(at(n, "clip"), np + "/clip", 4);
            if (const auto* scroll = c.object(at(n, "scroll"), np + "/scroll", {"extent", "offset"})) {
                c.numbers(at(scroll, "extent"), np + "/scroll/extent", 2);
                c.numbers(at(scroll, "offset"), np + "/scroll/offset", 2);
            }
        }
        g.layout = std::move(*value);
    }

    void paint(LoadedSnapshotGeneration& g, const std::string& file) {
        auto value = json(file, paint_bytes);
        if (!value) return;
        const std::string p = "/" + file;
        const auto* o =
            c.object(&*value, p, {"format", "version", "generation", "commands", "glyphs", "time_dependent", "kinds"});
        c.expect(at(o, "format"), p + "/format", "tessera-paint-summary");
        c.version(at(o, "version"), p + "/version");
        if (const auto generation = c.integer(at(o, "generation"), p + "/generation");
            generation && *generation != g.generation)
            c.fail(p + "/generation", "The paint summary belongs to another generation than its manifest entry.");
        const auto commands = c.integer(at(o, "commands"), p + "/commands");
        c.integer(at(o, "glyphs"), p + "/glyphs");
        c.boolean(at(o, "time_dependent"), p + "/time_dependent");
        const auto* kinds = at(o, "kinds");
        if (const auto* k = c.object(kinds, p + "/kinds",
                                     {"rect", "border", "image", "glyph_run", "push_clip", "pop_clip", "push_transform",
                                      "pop_transform", "shadow", "stroke"})) {
            std::uint64_t sum = 0;
            bool complete = true;
            for (const auto kind : paint_kinds) {
                const auto count = c.integer(at(k, kind), p + "/kinds/" + std::string(kind));
                complete = complete && count;
                sum += count.value_or(0);
            }
            if (complete && commands && sum != *commands)
                c.fail(p + "/kinds", "Command kinds do not add up to the command count.");
        }
        g.paint = std::move(*value);
    }

    void semantics(LoadedSnapshotGeneration& g, const std::string& file, std::uint64_t node_count) {
        const auto* text = bytes(file, max_serialized_semantic_bytes);
        if (!text) return;
        const std::string p = "/" + file;
        auto loaded = load_semantics(*text);
        if (!loaded) {
            take(std::move(loaded.diagnostics), p);
            return;
        }
        if (loaded.value->generation != g.generation)
            c.fail(p + "/generation", "The semantic snapshot belongs to another generation than its manifest entry.");
        if (loaded.value->node_count != node_count)
            c.fail(p + "/node_count", "The node count disagrees with the manifest entry.");
        auto parsed = parse_json(*text, text->size() + 1);
        if (parsed) g.semantics = std::move(*parsed.value);
    }

    void frame(LoadedSnapshotGeneration& g, const std::string& file, const std::vector<double>* extent) {
        const auto* png = bytes(file, max_snapshot_file_bytes);
        if (!png) return;
        auto image = decode_png(*png);
        if (!image) {
            take(std::move(image.diagnostics), "/" + file);
            return;
        }
        if (extent &&
            (double(image.value->extent.width) != (*extent)[0] || double(image.value->extent.height) != (*extent)[1]))
            c.fail("/" + file, "The PNG extent disagrees with the manifest entry.");
        g.frame = std::move(*image.value);
    }

    void generation(const JsonValue& entry, const std::string& p, std::optional<std::uint64_t> index, bool captured,
                    std::vector<LoadedSnapshotGeneration>& out) {
        const auto* o = c.object(&entry, p,
                                 {"generation", "step", "viewport", "device_scale", "animation_time_us", "node_count",
                                  "focused", "members"});
        if (!o || !index) return;
        LoadedSnapshotGeneration g;
        g.generation = *index;
        g.entry = entry;
        if (const auto* step = at(o, "step"); step && !null(step)) c.integer(step, p + "/step");
        const auto viewport = c.numbers(at(o, "viewport"), p + "/viewport", 2);
        if (viewport && !((*viewport)[0] > 0 && (*viewport)[1] > 0))
            c.fail(p + "/viewport", "Viewport dimensions are positive.");
        const auto scale = c.positive(at(o, "device_scale"), p + "/device_scale");
        if (const auto time = c.real(at(o, "animation_time_us"), p + "/animation_time_us");
            time && (std::floor(*time) != *time || std::abs(*time) > max_integer))
            c.fail(p + "/animation_time_us", "Expected an integral number of microseconds.");
        const auto node_count = c.integer(at(o, "node_count"), p + "/node_count", double(max_document_nodes));
        if (node_count && *node_count == 0) c.fail(p + "/node_count", "A document has at least its root.");
        std::optional<std::uint64_t> focused;
        std::optional<std::string> focused_id;
        if (const auto* f = at(o, "focused"); f && !null(f))
            if (const auto* focus = c.object(f, p + "/focused", {"node", "id"})) {
                focused = c.integer(at(focus, "node"), p + "/focused/node");
                if (focused && node_count && *focused >= *node_count)
                    c.fail(p + "/focused/node", "The focused node is outside the document.");
                if (const auto* id = at(focus, "id"); id && !null(id)) focused_id = c.text(id, p + "/focused/id");
            }

        const auto dir = "g" + std::to_string(*index) + "/";
        const auto* members =
            c.object(at(o, "members"), p + "/members", {"semantics", "layout", "paint", "tree", "frame"});
        if (!members || !node_count) return;
        const auto mp = p + "/members";

        // Semantics: a Semantic JSON v1 reference or an unavailable record.
        if (const auto* s = at(members, "semantics")) {
            const auto* so = std::get_if<JsonValue::Object>(&s->value);
            if (so && so->contains("unavailable")) {
                c.object(s, mp + "/semantics", {"unavailable"});
                c.expect(at(so, "unavailable"), mp + "/semantics/unavailable", "no_semantic_observation");
            } else if (const auto* ref = c.object(s, mp + "/semantics", {"path", "format", "version"})) {
                reference(ref, mp + "/semantics", dir + "semantics.json");
                c.expect(at(ref, "format"), mp + "/semantics/format", "tessera-semantics");
                c.version(at(ref, "version"), mp + "/semantics/version");
                semantics(g, dir + "semantics.json", *node_count);
            }
        }
        if (const auto* ref = c.object(at(members, "layout"), mp + "/layout", {"path", "format", "version"})) {
            reference(ref, mp + "/layout", dir + "layout.json");
            c.expect(at(ref, "format"), mp + "/layout/format", "tessera-layout");
            c.version(at(ref, "version"), mp + "/layout/version");
            layout(g, dir + "layout.json", *node_count, viewport ? &*viewport : nullptr, scale);
        }
        if (const auto* ref = c.object(at(members, "paint"), mp + "/paint", {"path", "format", "version"})) {
            reference(ref, mp + "/paint", dir + "paint.json");
            c.expect(at(ref, "format"), mp + "/paint/format", "tessera-paint-summary");
            c.version(at(ref, "version"), mp + "/paint/version");
            paint(g, dir + "paint.json");
        }
        if (const auto* ref = c.object(at(members, "tree"), mp + "/tree", {"path", "derived"})) {
            reference(ref, mp + "/tree", dir + "tree.txt");
            if (const auto derived = c.boolean(at(ref, "derived"), mp + "/tree/derived"); derived && !*derived)
                c.fail(mp + "/tree/derived", "tree.txt is a derived member.");
            if (const auto* tree = bytes(dir + "tree.txt", member_bytes); tree && !snapshot_detail::valid_utf8(*tree))
                c.fail("/" + dir + "tree.txt", "Member text is not valid UTF-8.");
        }
        if (const auto* f = at(members, "frame")) {
            const auto* fo = std::get_if<JsonValue::Object>(&f->value);
            if (fo && fo->contains("unavailable")) {
                c.object(f, mp + "/frame", {"unavailable"});
                c.expect(at(fo, "unavailable"), mp + "/frame/unavailable", "not_declared");
                if (captured) c.fail(mp + "/frame", "A bundle with a capture backend records every frame.");
            } else if (const auto* ref = c.object(f, mp + "/frame", {"path", "format", "extent"})) {
                reference(ref, mp + "/frame", dir + "frame.png");
                c.expect(at(ref, "format"), mp + "/frame/format", "png");
                auto extent = c.numbers(at(ref, "extent"), mp + "/frame/extent", 2);
                if (extent)
                    for (std::size_t i = 0; i < 2; ++i)
                        if ((*extent)[i] < 1 || (*extent)[i] > max_png_dimension ||
                            std::floor((*extent)[i]) != (*extent)[i]) {
                            c.fail(mp + "/frame/extent/" + std::to_string(i), "Expected a positive pixel count.");
                            extent.reset();
                            break;
                        }
                if (!captured) c.fail(mp + "/frame", "A frame needs the manifest's capture declaration.");
                frame(g, dir + "frame.png", extent ? &*extent : nullptr);
            }
        }
        // The manifest's focused ID names the layout node at that index.
        if (focused) {
            const auto* nodes = at(std::get_if<JsonValue::Object>(&g.layout.value), "nodes");
            const auto* list = nodes ? std::get_if<JsonValue::Array>(&nodes->value) : nullptr;
            if (list && *focused < list->size()) {
                const auto* node = std::get_if<JsonValue::Object>(&(*list)[std::size_t(*focused)].value);
                const auto* id = node ? at(node, "id") : nullptr;
                const auto* layout_id = id ? std::get_if<std::string>(&id->value) : nullptr;
                if ((layout_id ? std::optional<std::string>(*layout_id) : std::nullopt) != focused_id)
                    c.fail(p + "/focused/id", "The focused ID disagrees with the layout node.");
            }
        }
        out.push_back(std::move(g));
    }

    void list_member(const JsonValue::Object* members, std::string_view name, const std::string& file,
                     std::optional<std::uint64_t>& count) {
        const auto p = "/manifest.json/members/" + std::string(name);
        if (const auto* ref = c.object(at(members, name), p, {"path", "count"})) {
            reference(ref, p, file);
            count = c.integer(at(ref, "count"), p + "/count");
        }
    }

    void actions(LoadedSnapshotBundle& loaded, std::optional<std::uint64_t> count,
                 std::optional<std::uint64_t> generation_count) {
        auto value = json("actions.json", member_bytes);
        if (!value) return;
        const auto* o = c.object(&*value, "/actions.json", {"actions"});
        const auto* list = c.array(at(o, "actions"), "/actions.json/actions", member_bytes);
        if (list && count && list->size() != *count)
            c.fail("/actions.json/actions", "The action count disagrees with the manifest.");
        for (std::size_t i = 0; list && i < list->size(); ++i) {
            const auto p = "/actions.json/actions/" + std::to_string(i);
            const auto* a = c.object(&(*list)[i], p, {"step", "generation", "binding", "action", "node", "id"});
            c.integer(at(a, "step"), p + "/step");
            if (const auto g = c.integer(at(a, "generation"), p + "/generation");
                g && generation_count && *g >= *generation_count)
                c.fail(p + "/generation", "The generation is outside the run.");
            c.text(at(a, "binding"), p + "/binding");
            c.text(at(a, "action"), p + "/action");
            c.integer(at(a, "node"), p + "/node");
            if (const auto* id = at(a, "id"); id && !null(id)) c.text(id, p + "/id");
        }
        loaded.actions = std::move(*value);
    }

    void diagnostics(LoadedSnapshotBundle& loaded, std::optional<std::uint64_t> count) {
        auto value = json("diagnostics.json", member_bytes);
        if (!value) return;
        const auto* o = c.object(&*value, "/diagnostics.json", {"diagnostics"});
        const auto* list = c.array(at(o, "diagnostics"), "/diagnostics.json/diagnostics", member_bytes);
        if (list && count && list->size() != *count)
            c.fail("/diagnostics.json/diagnostics", "The diagnostic count disagrees with the manifest.");
        for (std::size_t i = 0; list && i < list->size(); ++i) {
            const auto p = "/diagnostics.json/diagnostics/" + std::to_string(i);
            const auto* d =
                c.object(&(*list)[i], p, {"code", "severity", "path", "message"}, {"byte_offset", "source"});
            c.text(at(d, "code"), p + "/code");
            if (const auto severity = c.text(at(d, "severity"), p + "/severity");
                severity && *severity != "error" && *severity != "warning")
                c.fail(p + "/severity", "Expected \"error\" or \"warning\".");
            c.text(at(d, "path"), p + "/path");
            c.text(at(d, "message"), p + "/message");
            c.integer(at(d, "byte_offset"), p + "/byte_offset");
            if (const auto* source = c.object(at(d, "source"), p + "/source", {"file", "span"})) {
                c.text(at(source, "file"), p + "/source/file");
                if (const auto span = c.numbers(at(source, "span"), p + "/source/span", 2);
                    span && ((*span)[0] < 0 || (*span)[1] < (*span)[0] || std::floor((*span)[0]) != (*span)[0] ||
                             std::floor((*span)[1]) != (*span)[1]))
                    c.fail(p + "/source/span", "Expected an ordered pair of byte offsets.");
            }
        }
        loaded.diagnostics = std::move(*value);
    }

    Result<LoadedSnapshotBundle> run() {
        LoadedSnapshotBundle loaded;
        auto manifest = json("manifest.json", manifest_bytes);
        if (!manifest) return {std::nullopt, std::move(errors)};
        const std::string mp = "/manifest.json";
        const auto* m = c.object(&*manifest, mp,
                                 {"format", "version", "producer", "scenario", "replay_version", "environment",
                                  "capture", "generation_count", "members", "generations"});
        if (!m) return {std::nullopt, std::move(errors)};
        const auto format = c.text(at(m, "format"), mp + "/format");
        if (format && *format != "tessera-snapshot-bundle") {
            c.fail(mp + "/format", "Not a tessera snapshot bundle manifest.");
            return {std::nullopt, std::move(errors)};
        }
        if (!c.version(at(m, "version"), mp + "/version") || !format) return {std::nullopt, std::move(errors)};

        c.name(at(m, "producer"), mp + "/producer");
        c.optional_name(at(m, "scenario"), mp + "/scenario");
        const auto declared_replay = c.version(at(m, "replay_version"), mp + "/replay_version", {1, 2});
        const auto* environment =
            c.object(at(m, "environment"), mp + "/environment", {"scale", "locale", "text", "text_service"});
        const auto scale = c.positive(at(environment, "scale"), mp + "/environment/scale");
        const auto locale = c.text(at(environment, "locale"), mp + "/environment/locale");
        const auto text = c.text(at(environment, "text"), mp + "/environment/text");
        c.name(at(environment, "text_service"), mp + "/environment/text_service");
        const auto* capture = at(m, "capture");
        const bool captured = capture && !null(capture);
        if (const auto* co =
                captured ? c.object(capture, mp + "/capture", {"backend", "device", "format", "glyphs"}) : nullptr) {
            c.name(at(co, "backend"), mp + "/capture/backend");
            c.name(at(co, "device"), mp + "/capture/device");
            c.expect(at(co, "format"), mp + "/capture/format", "rgba8_srgb");
            c.name(at(co, "glyphs"), mp + "/capture/glyphs");
        }
        const auto generation_count = c.integer(at(m, "generation_count"), mp + "/generation_count");
        if (generation_count && *generation_count == 0)
            c.fail(mp + "/generation_count", "A run has at least one generation.");

        std::optional<std::uint64_t> action_count, diagnostic_count;
        if (const auto* members =
                c.object(at(m, "members"), mp + "/members", {"recording", "actions", "diagnostics"})) {
            if (const auto* ref =
                    c.object(at(members, "recording"), mp + "/members/recording", {"path", "format", "version"})) {
                reference(ref, mp + "/members/recording", "recording.json");
                c.expect(at(ref, "format"), mp + "/members/recording/format", "tessera-replay");
                if (const auto v = c.version(at(ref, "version"), mp + "/members/recording/version", {1, 2});
                    v && declared_replay && *v != *declared_replay)
                    c.fail(mp + "/members/recording/version", "The recording version disagrees with replay_version.");
            }
            list_member(members, "actions", "actions.json", action_count);
            list_member(members, "diagnostics", "diagnostics.json", diagnostic_count);
        }

        // The recording is always a member, even when its manifest reference is broken.
        if (const auto* recording = bytes("recording.json", max_serialized_replay_bytes)) {
            auto replay = load_replay(*recording);
            if (!replay) {
                take(std::move(replay.diagnostics), "/recording.json");
            } else {
                const auto& env = replay.value->environment;
                if (declared_replay && replay.value->version != *declared_replay)
                    c.fail("/recording.json", "The recording version disagrees with the manifest.");
                if (scale && float(*scale) != env.scale)
                    c.fail(mp + "/environment/scale", "The scale disagrees with the recording environment.");
                if (locale && *locale != env.locale)
                    c.fail(mp + "/environment/locale", "The locale disagrees with the recording environment.");
                const auto declared = env.text.kind == ReplayText::Kind::placeholder ? std::string("placeholder")
                                                                                     : "font-profile:" + env.text.id;
                if (text && *text != declared)
                    c.fail(mp + "/environment/text", "The text declaration disagrees with the recording environment.");
                loaded.recording = std::move(*replay.value);
            }
        }
        actions(loaded, action_count, generation_count);
        diagnostics(loaded, diagnostic_count);

        if (const auto* entries = c.array(at(m, "generations"), mp + "/generations", max_snapshot_generations)) {
            if (entries->empty()) c.fail(mp + "/generations", "A bundle holds at least one generation.");
            std::optional<std::uint64_t> previous;
            for (std::size_t i = 0; i < entries->size(); ++i) {
                const auto p = mp + "/generations/" + std::to_string(i);
                const auto* o = std::get_if<JsonValue::Object>(&(*entries)[i].value);
                auto index = c.integer(at(o, "generation"), p + "/generation");
                if (index && generation_count && *index >= *generation_count) {
                    c.fail(p + "/generation", "The generation is outside the run.");
                    index.reset();
                } else if (index && previous && *index <= *previous) {
                    c.fail(p + "/generation", "Generations are listed in strictly increasing order.");
                    index.reset();
                }
                if (index) previous = index;
                generation((*entries)[i], p, index, captured, loaded.generations);
            }
        }

        for (const auto& file : bundle.files)
            if (!listed.contains(file.path))
                c.fail("/" + file.path, "The manifest does not list this member.", "unlisted_member");
        if (!errors.empty()) return {std::nullopt, std::move(errors)};
        loaded.manifest = std::move(*manifest);
        return {std::move(loaded), {}};
    }
};

std::string utf8(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

} // namespace

Result<LoadedSnapshotBundle> load_snapshot_bundle(const SnapshotBundle& bundle) { return Loader{bundle}.run(); }

Result<SnapshotBundle> read_snapshot_bundle(const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    const auto io = [](std::string path, std::string message) -> Result<SnapshotBundle> {
        return {std::nullopt, {{"bundle_io", Severity::error, std::move(path), std::move(message)}}};
    };
    const auto limit = [](std::string path, std::string message) -> Result<SnapshotBundle> {
        return {std::nullopt, {{"bundle_limit", Severity::error, std::move(path), std::move(message)}}};
    };
    std::error_code ec;
    if (!fs::is_directory(directory, ec)) return io("", "The bundle path is not a readable directory.");

    // Lists a directory's entries in name order without following links.
    const auto list = [&](const fs::path& dir, std::vector<std::pair<std::string, fs::path>>& out) {
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (out.size() >= max_snapshot_entries) return false;
            out.emplace_back(utf8(it->path().filename()), it->path());
        }
        std::sort(out.begin(), out.end());
        return true;
    };
    Result<SnapshotBundle> result{SnapshotBundle{}, {}};
    auto& files = result.value->files;
    std::size_t entries = 0;
    std::uint64_t total = 0;
    std::vector<std::pair<std::string, fs::path>> top;
    if (!list(directory, top))
        return limit("", "The bundle holds more than " + std::to_string(max_snapshot_entries) + " entries.");
    if (ec) return io("", ec.message());
    const auto unlisted = [&](std::string path, std::string message) {
        result.diagnostics.push_back({"unlisted_member", Severity::error, "/" + path, std::move(message)});
    };
    std::vector<std::pair<std::string, fs::path>> regular;
    for (const auto& [name, path] : top) {
        const auto status = fs::symlink_status(path, ec);
        if (ec) return io("/" + name, ec.message());
        if (fs::is_regular_file(status)) {
            regular.emplace_back(name, path);
        } else if (fs::is_directory(status)) {
            std::vector<std::pair<std::string, fs::path>> inner;
            if (!list(path, inner)) return limit("/" + name, "The directory holds too many entries.");
            if (ec) return io("/" + name, ec.message());
            for (const auto& [inner_name, inner_path] : inner) {
                const auto inner_status = fs::symlink_status(inner_path, ec);
                if (ec) return io("/" + name + "/" + inner_name, ec.message());
                if (fs::is_regular_file(inner_status))
                    regular.emplace_back(name + "/" + inner_name, inner_path);
                else
                    unlisted(name + "/" + inner_name, "Bundle members are regular files at most one directory deep.");
            }
        } else {
            unlisted(name, "Bundle members are regular files.");
        }
        entries = regular.size() + result.diagnostics.size();
        if (entries > max_snapshot_entries)
            return limit("", "The bundle holds more than " + std::to_string(max_snapshot_entries) + " entries.");
    }
    for (const auto& [name, path] : regular) {
        const auto size = fs::file_size(path, ec);
        if (ec) return io("/" + name, ec.message());
        if (size > max_snapshot_file_bytes)
            return limit("/" + name,
                         "The file exceeds the " + std::to_string(max_snapshot_file_bytes) + "-byte bound.");
        total += size;
        if (total > max_snapshot_bundle_bytes) return limit("/" + name, "The bundle exceeds its total byte bound.");
        std::ifstream in(path, std::ios::binary);
        std::string bytes(std::size_t(size), '\0');
        in.read(bytes.data(), std::streamsize(size));
        if (!in || in.peek() != std::char_traits<char>::eof())
            return io("/" + name, "Cannot read the file completely.");
        files.push_back({name, std::move(bytes)});
    }
    std::sort(files.begin(), files.end(), [](const SnapshotFile& a, const SnapshotFile& b) { return a.path < b.path; });
    return result;
}

Result<LoadedSnapshotBundle> verify_snapshot_bundle(const std::filesystem::path& directory) {
    auto read = read_snapshot_bundle(directory);
    if (!read) return {std::nullopt, std::move(read.diagnostics)};
    auto loaded = load_snapshot_bundle(*read.value);
    read.diagnostics.insert(read.diagnostics.end(), loaded.diagnostics.begin(), loaded.diagnostics.end());
    if (!read.diagnostics.empty()) return {std::nullopt, std::move(read.diagnostics)};
    return loaded;
}

} // namespace tessera
