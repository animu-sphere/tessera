#include <tessera/snapshot/snapshot_bundle.hpp>
#include <tessera/replay/replay_serialization.hpp>
#include "json.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <system_error>

namespace tessera {
namespace {

using snapshot_detail::quote;
using snapshot_detail::identity;

constexpr std::array<std::string_view, 27> role_names{
    "root",      "button",    "text",       "separator",    "dialog",       "list",        "tree",
    "list_item", "tree_item", "menu",       "menu_item",    "scrollbar",    "radio_group", "radio",
    "checkbox",  "switch",    "text_input", "number_input", "tab_list",     "tab",         "toolbar",
    "combobox",  "listbox",   "option",     "group",        "color_picker", "text_editor"};
static_assert(role_names.size() == std::size_t(SemanticRole::text_editor) + 1, "Every role needs a bundle name");

constexpr std::array<std::string_view, 5> member_files{"semantics.json", "layout.json", "paint.json", "tree.txt",
                                                       "frame.png"};
constexpr std::array<std::string_view, 4> bundle_files{"manifest.json", "recording.json", "diagnostics.json",
                                                       "actions.json"};

Diagnostic error(std::string code, std::string path, std::string message) {
    return {std::move(code), Severity::error, std::move(path), std::move(message)};
}

// Floats use the shortest round-trip form.
std::string number(float value) {
    if (value == 0) value = 0; // Spell negative zero as 0.
    std::array<char, 32> buffer{};
    const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return ec == std::errc{} ? std::string(buffer.data(), end) : "null";
}
std::string number(std::uint64_t value) { return std::to_string(value); }

std::string pair(float a, float b) { return "[" + number(a) + ", " + number(b) + "]"; }
std::string rect(const Rect& r) {
    return "[" + number(r.origin.x) + ", " + number(r.origin.y) + ", " + number(r.size.width) + ", " +
           number(r.size.height) + "]";
}
std::string edges(const Edges& e) {
    return "[" + number(e.top) + ", " + number(e.right) + ", " + number(e.bottom) + ", " + number(e.left) + "]";
}
std::string optional_string(const std::optional<std::string>& text) { return text ? quote(*text) : "null"; }

std::string diagnostic_json(const Diagnostic& d) {
    std::string out = "{\"code\": " + quote(d.code) +
                      ", \"severity\": " + quote(d.severity == Severity::error ? "error" : "warning") +
                      ", \"path\": " + quote(d.path) + ", \"message\": " + quote(d.message);
    if (d.byte_offset) out += ", \"byte_offset\": " + number(std::uint64_t(*d.byte_offset));
    if (d.source)
        out += ", \"source\": {\"file\": " + quote(d.source->location.file) + ", \"span\": [" +
               number(std::uint64_t(d.source->location.span.begin)) + ", " +
               number(std::uint64_t(d.source->location.span.end)) + "]}";
    return out + "}";
}

// Joins a list of already spelled JSON values, one per line at the given indent.
std::string lines(const std::vector<std::string>& values, std::string_view indent) {
    if (values.empty()) return "[]";
    std::string out = "[\n";
    for (std::size_t i = 0; i < values.size(); ++i) {
        out += indent;
        out += "  ";
        out += values[i];
        out += i + 1 < values.size() ? ",\n" : "\n";
    }
    return out + std::string(indent) + "]";
}

struct FlatNode {
    const UiNode* node = nullptr;
    std::uint32_t parent = no_layout_parent; // Document preorder index.
    std::uint32_t depth = 0;
};

std::vector<FlatNode> flatten(const UiDocument& document) {
    std::vector<FlatNode> result;
    struct Pending {
        const UiNode* node;
        std::uint32_t parent;
        std::uint32_t depth;
    };
    std::vector<Pending> stack{{&document.root, no_layout_parent, 0}};
    while (!stack.empty()) {
        const auto current = stack.back();
        stack.pop_back();
        const auto index = std::uint32_t(result.size());
        result.push_back({current.node, current.parent, current.depth});
        for (auto child = current.node->children.rbegin(); child != current.node->children.rend(); ++child)
            stack.push_back({&*child, index, current.depth + 1});
    }
    return result;
}

// The document each replay generation observed: the latest reload or host view at or before its step.
const UiDocument& generation_document(const ReplayRecording& recording, const ReplayGeneration& generation) {
    const UiDocument* document = &recording.document;
    if (!generation.step) return *document;
    for (std::size_t i = 0; i <= *generation.step && i < recording.steps.size(); ++i) {
        if (const auto* reload = std::get_if<ReplayReload>(&recording.steps[i]))
            document = &reload->document;
        else if (const auto* host = std::get_if<ReplayHostUpdate>(&recording.steps[i]))
            document = &host->view.document;
    }
    return *document;
}

std::string text_property(const UiNode& node) {
    const auto found = node.properties.find("text");
    if (found == node.properties.end()) return {};
    const auto* text = std::get_if<std::string>(&found->second);
    return text ? *text : std::string{};
}

// One-line display text: control characters escaped, bounded on a UTF-8 boundary.
std::string display(std::string_view text) {
    constexpr std::size_t limit = 96;
    std::string out;
    std::size_t i = 0;
    for (; i < text.size() && out.size() < limit; ++i) {
        const char c = text[i];
        if (c == '\n')
            out += "\\n";
        else if (c == '"')
            out += "\\\"";
        else if (static_cast<unsigned char>(c) < 0x20)
            out += ' ';
        else
            out += c;
    }
    while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) out += text[i++];
    if (i < text.size()) out += "...";
    return "\"" + out + "\"";
}

struct Generation {
    std::size_t index = 0;
    const ReplayGeneration* replay = nullptr;
    const ReplaySemantics* semantics = nullptr;
    const OffscreenFrame* frame = nullptr;
    std::vector<FlatNode> nodes;
    std::vector<const ReplayBox*> boxes;        // By document preorder index; null when not displayed.
    std::vector<const SemanticRecord*> records; // By document preorder index; null without an entry.
    std::string directory() const { return "g" + std::to_string(index); }
};

std::string layout_json(const Generation& g) {
    std::vector<std::string> nodes;
    for (std::uint32_t i = 0; i < g.nodes.size(); ++i) {
        const auto& flat = g.nodes[i];
        std::string out = "{\"node\": " + number(std::uint64_t(i)) + ", \"parent\": " +
                          (flat.parent == no_layout_parent ? std::string("null") : number(std::uint64_t(flat.parent))) +
                          ", \"kind\": " + quote(flat.node->kind == NodeKind::text ? "text" : "box") +
                          ", \"id\": " + optional_string(flat.node->id);
        const auto* box = g.boxes[i];
        if (!box) {
            out += ", \"displayed\": false}";
        } else {
            out += ", \"displayed\": true, \"visible\": " + std::string(box->visible ? "true" : "false") +
                   ", \"border_box\": " + rect(box->border_box) + ", \"border\": " + edges(box->border) +
                   ", \"padding\": " + edges(box->padding);
            if (box->clip) out += ", \"clip\": " + rect(*box->clip);
            if (box->scroll)
                out += ", \"scroll\": {\"extent\": " + pair(box->scroll->extent.width, box->scroll->extent.height) +
                       ", \"offset\": " + pair(box->scroll->offset.x, box->scroll->offset.y) + "}";
            out += "}";
        }
        nodes.push_back(std::move(out));
    }
    const auto& r = *g.replay;
    return "{\n  \"format\": \"tessera-layout\",\n  \"version\": 1,\n  \"generation\": " +
           number(std::uint64_t(g.index)) + ",\n  \"viewport\": " + pair(r.viewport.width, r.viewport.height) +
           ",\n  \"device_scale\": " + number(r.device_scale) + ",\n  \"nodes\": " + lines(nodes, "  ") + "\n}\n";
}

std::string paint_json(const Generation& g) {
    constexpr std::array<std::string_view, 10> kinds{"rect",      "border",   "image",          "glyph_run",
                                                     "push_clip", "pop_clip", "push_transform", "pop_transform",
                                                     "shadow",    "stroke"};
    static_assert(kinds.size() == std::variant_size_v<DrawCommand>, "Every paint command needs a summary name");
    std::array<std::uint64_t, kinds.size()> counts{};
    std::uint64_t glyphs = 0;
    for (const auto& command : g.replay->paint.commands) {
        ++counts[command.index()];
        if (const auto* run = std::get_if<DrawGlyphRun>(&command)) glyphs += run->run.glyphs.size();
    }
    std::string out = "{\n  \"format\": \"tessera-paint-summary\",\n  \"version\": 1,\n  \"generation\": " +
                      number(std::uint64_t(g.index)) +
                      ",\n  \"commands\": " + number(std::uint64_t(g.replay->paint.commands.size())) +
                      ",\n  \"glyphs\": " + number(glyphs) +
                      ",\n  \"time_dependent\": " + (time_dependent(g.replay->paint) ? "true" : "false") +
                      ",\n  \"kinds\": {";
    for (std::size_t i = 0; i < kinds.size(); ++i) out += (i ? ", " : "") + quote(kinds[i]) + ": " + number(counts[i]);
    return out + "}\n}\n";
}

std::string tree_text(const Generation& g, const SnapshotOptions& options) {
    const auto& r = *g.replay;
    std::string out = "# tessera snapshot g" + std::to_string(g.index) + " | viewport " + number(r.viewport.width) +
                      "x" + number(r.viewport.height) + " @" + number(r.device_scale) + " | focus ";
    std::optional<std::uint32_t> focused = g.semantics ? g.semantics->focused : std::nullopt;
    if (!focused)
        out += "none";
    else if (const auto& id = g.nodes[*focused].node->id)
        out += "#" + *id;
    else
        out += "node " + std::to_string(*focused);
    if (options.scenario) out += " | scenario " + *options.scenario;
    out += "\n# <role|kind> #id \"name\" [x,y wxh] state; derived from semantics.json and layout.json\n";
    for (std::uint32_t i = 0; i < g.nodes.size(); ++i) {
        const auto& flat = g.nodes[i];
        const auto* record = g.records[i];
        const auto* box = g.boxes[i];
        std::string line(std::size_t(flat.depth) * 2, ' ');
        line += record                              ? std::string(role_names[std::size_t(record->role)])
                : flat.node->kind == NodeKind::text ? "text"
                                                    : "box";
        if (flat.node->id) line += " #" + *flat.node->id;
        const auto name = record ? record->name : flat.node->kind == NodeKind::text ? text_property(*flat.node) : "";
        if (!name.empty()) line += " " + display(name);
        if (box)
            line += " [" + number(box->border_box.origin.x) + "," + number(box->border_box.origin.y) + " " +
                    number(box->border_box.size.width) + "x" + number(box->border_box.size.height) + "]";
        else
            line += " display:none";
        if (box && !box->visible) line += " hidden";
        if (focused && *focused == i) line += " focused";
        if (record) {
            if (!record->enabled) line += " disabled";
            if (record->selected) line += *record->selected ? " selected" : " unselected";
            if (record->expanded) line += *record->expanded ? " expanded" : " collapsed";
            if (record->value) line += " value=" + display(*record->value);
            if (record->item_position && record->item_count)
                line += " item=" + std::to_string(*record->item_position) + "/" + std::to_string(*record->item_count);
        }
        if (box && box->scroll)
            line += " scroll=" + number(box->scroll->offset.x) + "," + number(box->scroll->offset.y) + "/" +
                    number(box->scroll->extent.width) + "x" + number(box->scroll->extent.height);
        out += line + "\n";
    }
    return out;
}

std::string member(std::string path, std::string_view extra = {}) {
    return "{\"path\": " + quote(path) + (extra.empty() ? "" : ", ") + std::string(extra) + "}";
}

} // namespace

const SnapshotFile* SnapshotBundle::find(std::string_view path) const {
    const auto found =
        std::find_if(files.begin(), files.end(), [&](const SnapshotFile& file) { return file.path == path; });
    return found == files.end() ? nullptr : &*found;
}

Result<SnapshotBundle> build_snapshot_bundle(const ReplayRecording& recording, const OffscreenRun& run,
                                             const SnapshotOptions& options) {
    std::vector<Diagnostic> errors;
    if (!identity(options.producer))
        errors.push_back(
            error("invalid_bundle_option", "/options/producer", "Use a 1-256 byte UTF-8 identity without controls."));
    if (options.scenario && !identity(*options.scenario))
        errors.push_back(
            error("invalid_bundle_option", "/options/scenario", "Use a 1-256 byte UTF-8 identity without controls."));
    if (options.text_service && !identity(*options.text_service))
        errors.push_back(error("invalid_bundle_option", "/options/text_service",
                               "Use a 1-256 byte UTF-8 identity without controls."));
    if (options.glyphs && !identity(*options.glyphs))
        errors.push_back(
            error("invalid_bundle_option", "/options/glyphs", "Use a 1-256 byte UTF-8 identity without controls."));
    const auto& generations = run.output.generations;
    if (generations.empty() || run.frames.size() != generations.size())
        errors.push_back(error("invalid_bundle_option", "/run", "Supply a complete run_offscreen result."));
    const bool captured = run.manifest.capture.has_value();
    if (captured && !options.glyphs)
        errors.push_back(
            error("invalid_bundle_option", "/options/glyphs", "Declare the glyph coverage behind captured frames."));
    std::vector<std::size_t> selected = options.generations;
    if (selected.empty() && !generations.empty()) selected.push_back(generations.size() - 1);
    if (selected.size() > max_snapshot_generations)
        errors.push_back(error("out_of_range", "/options/generations", "Select at most 256 generations."));
    for (std::size_t i = 0; i < selected.size() && i < max_snapshot_generations; ++i)
        if (selected[i] >= generations.size() || (i && selected[i] <= selected[i - 1]))
            errors.push_back(error("out_of_range", "/options/generations/" + std::to_string(i),
                                   "Select existing generations in strictly increasing order."));
    if (!errors.empty()) return {std::nullopt, std::move(errors)};

    auto saved = save_replay(recording);
    if (!saved) {
        for (auto& d : saved.diagnostics) d.path = "/recording" + d.path;
        return {std::nullopt, std::move(saved.diagnostics)};
    }

    SnapshotBundle bundle;
    const auto add = [&](std::string path, std::string bytes) {
        bundle.files.push_back({std::move(path), std::move(bytes)});
    };
    add("recording.json", std::move(*saved.value));

    std::vector<std::string> generation_entries;
    for (const auto index : selected) {
        Generation g;
        g.index = index;
        g.replay = &generations[index];
        g.frame = &run.frames[index];
        for (const auto& observation : run.output.semantics)
            if (observation.generation == index) g.semantics = &observation; // The last settled observation.
        g.nodes = flatten(generation_document(recording, *g.replay));
        g.boxes.assign(g.nodes.size(), nullptr);
        g.records.assign(g.nodes.size(), nullptr);
        for (const auto& box : g.replay->boxes)
            if (box.node < g.boxes.size()) g.boxes[box.node] = &box;
        if (g.semantics)
            for (const auto& record : g.semantics->nodes)
                if (record.node < g.records.size()) g.records[record.node] = &record;
        const auto dir = g.directory();
        std::vector<std::string> members;

        if (g.semantics) {
            SemanticSnapshot snapshot{semantic_version, index, std::uint32_t(g.nodes.size()), g.semantics->nodes};
            auto semantics = save_semantics(snapshot);
            if (!semantics) {
                for (auto& d : semantics.diagnostics)
                    d.path = "/generations/" + std::to_string(index) + "/semantics" + d.path;
                return {std::nullopt, std::move(semantics.diagnostics)};
            }
            add(dir + "/semantics.json", std::move(*semantics.value));
            members.push_back("\"semantics\": " +
                              member(dir + "/semantics.json", "\"format\": \"tessera-semantics\", \"version\": " +
                                                                  number(std::uint64_t(semantic_version))));
        } else {
            members.push_back("\"semantics\": {\"unavailable\": \"no_semantic_observation\"}");
        }
        add(dir + "/layout.json", layout_json(g));
        members.push_back("\"layout\": " +
                          member(dir + "/layout.json", "\"format\": \"tessera-layout\", \"version\": 1"));
        add(dir + "/paint.json", paint_json(g));
        members.push_back("\"paint\": " +
                          member(dir + "/paint.json", "\"format\": \"tessera-paint-summary\", \"version\": 1"));
        add(dir + "/tree.txt", tree_text(g, options));
        members.push_back("\"tree\": " + member(dir + "/tree.txt", "\"derived\": true"));
        if (g.frame->status == FrameStatus::captured && g.frame->image) {
            auto png = encode_png(*g.frame->image);
            if (!png) {
                for (auto& d : png.diagnostics) d.path = "/generations/" + std::to_string(index) + "/frame" + d.path;
                return {std::nullopt, std::move(png.diagnostics)};
            }
            add(dir + "/frame.png", std::move(*png.value));
            members.push_back("\"frame\": " + member(dir + "/frame.png", "\"format\": \"png\", \"extent\": " +
                                                                             pair(float(g.frame->extent.width),
                                                                                  float(g.frame->extent.height))));
        } else {
            members.push_back("\"frame\": {\"unavailable\": \"not_declared\"}");
        }

        const auto& r = *g.replay;
        std::string entry = "{\n      \"generation\": " + number(std::uint64_t(index)) +
                            ",\n      \"step\": " + (r.step ? number(std::uint64_t(*r.step)) : std::string("null")) +
                            ",\n      \"viewport\": " + pair(r.viewport.width, r.viewport.height) +
                            ",\n      \"device_scale\": " + number(r.device_scale) +
                            ",\n      \"animation_time_us\": " + std::to_string(r.animation_time.count()) +
                            ",\n      \"node_count\": " + number(std::uint64_t(g.nodes.size())) +
                            ",\n      \"focused\": ";
        if (g.semantics && g.semantics->focused) {
            const auto& id = g.nodes[*g.semantics->focused].node->id;
            entry +=
                "{\"node\": " + number(std::uint64_t(*g.semantics->focused)) + ", \"id\": " + optional_string(id) + "}";
        } else {
            entry += "null";
        }
        entry += ",\n      \"members\": {\n";
        for (std::size_t i = 0; i < members.size(); ++i)
            entry += "        " + members[i] + (i + 1 < members.size() ? ",\n" : "\n");
        entry += "      }\n    }";
        generation_entries.push_back(std::move(entry));
    }

    std::vector<std::string> actions;
    for (const auto& action : run.output.actions)
        actions.push_back("{\"step\": " + number(std::uint64_t(action.step)) +
                          ", \"generation\": " + number(std::uint64_t(action.generation)) +
                          ", \"binding\": " + quote(action.binding) + ", \"action\": " + quote(action.action) +
                          ", \"node\": " + number(std::uint64_t(action.node)) +
                          ", \"id\": " + optional_string(action.id) + "}");
    add("actions.json", "{\n  \"actions\": " + lines(actions, "  ") + "\n}\n");
    std::vector<std::string> diagnostics;
    for (const auto& d : options.diagnostics) diagnostics.push_back(diagnostic_json(d));
    add("diagnostics.json", "{\n  \"diagnostics\": " + lines(diagnostics, "  ") + "\n}\n");

    const auto& environment = run.manifest.environment;
    const auto declared_text = environment.text.kind == ReplayText::Kind::placeholder
                                   ? std::string("placeholder")
                                   : "font-profile:" + environment.text.id;
    std::string manifest =
        "{\n  \"format\": \"tessera-snapshot-bundle\",\n  \"version\": " +
        number(std::uint64_t(snapshot_bundle_version)) + ",\n  \"producer\": " + quote(options.producer) +
        ",\n  \"scenario\": " + optional_string(options.scenario) +
        ",\n  \"replay_version\": " + number(std::uint64_t(recording.version)) +
        ",\n  \"environment\": {\"scale\": " + number(environment.scale) +
        ", \"locale\": " + quote(environment.locale) + ", \"text\": " + quote(declared_text) +
        ", \"text_service\": " + quote(options.text_service.value_or(declared_text)) + "}" + ",\n  \"capture\": ";
    if (captured) {
        const auto& capture = *run.manifest.capture;
        manifest += "{\"backend\": " + quote(capture.backend) + ", \"device\": " + quote(capture.device) +
                    ", \"format\": \"rgba8_srgb\", \"glyphs\": " + quote(*options.glyphs) + "}";
    } else {
        manifest += "null";
    }
    manifest += ",\n  \"generation_count\": " + number(std::uint64_t(generations.size())) +
                ",\n  \"members\": {\n    \"recording\": " +
                member("recording.json",
                       "\"format\": \"tessera-replay\", \"version\": " + number(std::uint64_t(recording.version))) +
                ",\n    \"actions\": " + member("actions.json", "\"count\": " + number(std::uint64_t(actions.size()))) +
                ",\n    \"diagnostics\": " +
                member("diagnostics.json", "\"count\": " + number(std::uint64_t(diagnostics.size()))) +
                "\n  },\n  \"generations\": ";
    if (generation_entries.empty()) {
        manifest += "[]";
    } else {
        manifest += "[\n";
        for (std::size_t i = 0; i < generation_entries.size(); ++i)
            manifest += "    " + generation_entries[i] + (i + 1 < generation_entries.size() ? ",\n" : "\n");
        manifest += "  ]";
    }
    add("manifest.json", manifest + "\n}\n");
    std::sort(bundle.files.begin(), bundle.files.end(),
              [](const SnapshotFile& a, const SnapshotFile& b) { return a.path < b.path; });
    return {std::move(bundle), {}};
}

namespace {

bool generation_directory(std::string_view name) {
    return name.size() > 1 && name.front() == 'g' &&
           std::all_of(name.begin() + 1, name.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Lists the entries of an earlier bundle, or fails without touching anything when another entry exists.
std::vector<Diagnostic> previous_members(const std::filesystem::path& directory,
                                         std::vector<std::filesystem::path>& remove) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(directory, ec)) return {};
    if (!fs::is_directory(directory, ec))
        return {error("bundle_directory_not_empty", "", "The output path exists and is not a directory.")};
    if (fs::is_empty(directory, ec)) return {};
    if (!fs::is_regular_file(directory / "manifest.json", ec))
        return {error("bundle_directory_not_empty", "",
                      "Choose an absent or empty directory, or one that holds an earlier snapshot bundle.")};
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() &&
            std::find(bundle_files.begin(), bundle_files.end(), name) != bundle_files.end()) {
            remove.push_back(entry.path());
            continue;
        }
        if (!entry.is_directory() || !generation_directory(name))
            return {error("bundle_directory_not_empty", "/" + name,
                          "The directory holds an entry that is not a bundle member.")};
        for (const auto& inner : fs::directory_iterator(entry.path(), ec)) {
            const auto inner_name = inner.path().filename().string();
            if (!inner.is_regular_file() ||
                std::find(member_files.begin(), member_files.end(), inner_name) == member_files.end())
                return {error("bundle_directory_not_empty", "/" + name + "/" + inner_name,
                              "The directory holds an entry that is not a bundle member.")};
        }
        remove.push_back(entry.path());
    }
    if (ec) return {error("bundle_io", "", ec.message())};
    return {};
}

} // namespace

std::vector<Diagnostic> write_snapshot_bundle(const SnapshotBundle& bundle, const std::filesystem::path& directory) {
    namespace fs = std::filesystem;
    std::vector<fs::path> remove;
    if (auto diagnostics = previous_members(directory, remove); !diagnostics.empty()) return diagnostics;
    std::error_code ec;
    for (const auto& path : remove) {
        fs::remove_all(path, ec);
        if (ec) return {error("bundle_io", "", "Cannot replace " + path.string() + ": " + ec.message())};
    }
    for (const auto& file : bundle.files) {
        const auto target = directory / fs::path(file.path);
        fs::create_directories(target.parent_path(), ec);
        if (ec) return {error("bundle_io", "/" + file.path, ec.message())};
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(file.bytes.data(), std::streamsize(file.bytes.size()));
        out.close();
        if (!out) return {error("bundle_io", "/" + file.path, "Cannot write the member.")};
    }
    return {};
}

} // namespace tessera
