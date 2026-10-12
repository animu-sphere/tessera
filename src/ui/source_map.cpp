#include <tessera/ui/serialization.hpp>
#include "json_detail.hpp"

namespace tessera {
namespace {
void error(std::vector<Diagnostic>& errors, std::string code, std::string path, std::string message) {
    errors.push_back({std::move(code), Severity::error, std::move(path), std::move(message), {}});
}
bool file_valid(std::string_view file) {
    return file.size() <= max_source_file_bytes && detail::valid_utf8(file);
}
bool count_nodes(const UiNode& node, std::size_t depth, std::size_t& count) {
    if (depth > max_document_depth || ++count > max_document_nodes) return false;
    for (const auto& child : node.children)
        if (!count_nodes(child, depth + 1, count)) return false;
    return true;
}
bool check_node(const UiNode& node, const std::string& pointer, std::size_t depth, const DocumentSourceMap& map,
                const std::string& path, std::size_t& index, std::vector<Diagnostic>& errors) {
    if (depth > max_document_depth || index >= max_document_nodes || index >= map.nodes.size()) {
        error(errors, "source_map_mismatch", path + "/nodes", "Supply one source record per bounded document node in preorder.");
        return false;
    }
    const auto& source = map.nodes[index];
    const auto location = path + "/nodes/" + std::to_string(index++);
    bool shape = source.pointer == pointer && source.properties.size() <= node.properties.size() &&
        source.unmapped_properties.size() == node.properties.size() - source.properties.size();
    for (const auto& [name, value] : node.properties) {
        (void)value;
        if (source.properties.contains(name) == source.unmapped_properties.contains(name)) shape = false;
    }
    if (!shape) {
        error(errors, "source_map_mismatch", location,
              "Match the node pointer and every present property exactly once, with a span or an explicit unmapped declaration.");
        return false;
    }
    // Bound containers before examining caller-controlled names/locations.
    if (source.property_arguments.size() > node.properties.size() || source.event_arguments.size() > node.events.size()) {
        error(errors, "source_map_mismatch", location, "Argument origins must name present properties or events.");
        return false;
    }
    auto origin = [&](const SourceLocation& value, const std::string& field) {
        auto diagnostics = validate_source_location(value, field);
        errors.insert(errors.end(), diagnostics.begin(), diagnostics.end());
    };
    if (source.invocation) origin(*source.invocation, location + "/invocation");
    if (source.style_argument) origin(*source.style_argument, location + "/style_argument");
    for (const auto& [key, value] : source.property_arguments) {
        const auto field = location + "/property_arguments/" + detail::pointer_token(key);
        if (!source.unmapped_properties.contains(key))
            error(errors, "source_map_mismatch", field, "Argument origins name instance properties without template value spans.");
        origin(value, field);
    }
    for (const auto& [key, value] : source.event_arguments) {
        const auto field = location + "/event_arguments/" + detail::pointer_token(key);
        if (!node.events.contains(key))
            error(errors, "source_map_mismatch", field, "Argument origins name present event bindings.");
        origin(value, field);
    }
    if (!errors.empty()) return false;
    if (source.file && !file_valid(*source.file)) {
        error(errors, "source_map_file", location + "/file", "Use an opaque file identity of at most 4096 valid UTF-8 bytes.");
        return false;
    }
    if (!source.mapped) {
        if (source.span != SourceSpan{} || source.file || !source.properties.empty()) {
            error(errors, "source_map_span", location, "An unmapped node has no span, file override or property spans.");
            return false;
        }
    } else {
        if (source.span.begin > source.span.end) {
            error(errors, "source_map_span", location + "/span", "Use an ordered half-open UTF-8 byte range.");
            return false;
        }
        for (const auto& [name, span] : source.properties) {
            if (span.begin > span.end || span.begin < source.span.begin || span.end > source.span.end) {
                error(errors, "source_map_span", location + "/properties/" + detail::pointer_token(name),
                      "Keep the ordered property byte range within its authored node span.");
                return false;
            }
        }
    }
    for (std::size_t i = 0; i < node.children.size(); ++i)
        if (!check_node(node.children[i], pointer + "/children/" + std::to_string(i), depth + 1, map, path, index, errors)) return false;
    return true;
}
} // namespace

std::vector<Diagnostic> validate_source_location(const SourceLocation& source, std::string_view path) {
    std::vector<Diagnostic> errors;
    const std::string base(path);
    if (!file_valid(source.file))
        error(errors, "source_map_file", base + "/file", "Use an opaque file identity of at most 4096 valid UTF-8 bytes.");
    if (source.span.begin > source.span.end)
        error(errors, "source_map_span", base + "/span", "Use an ordered half-open UTF-8 byte range.");
    return errors;
}

std::vector<Diagnostic> validate_sources(const UiDocument& document, const DocumentSourceMap& map, std::string_view path) {
    std::vector<Diagnostic> errors;
    const std::string base(path);
    if (map.nodes.size() > max_document_nodes || map.nodes.empty()) {
        error(errors, "source_map_mismatch", base + "/nodes", "Supply one source record per bounded document node in preorder.");
        return errors;
    }
    std::size_t count = 0;
    if (!count_nodes(document.root, 1, count) || count != map.nodes.size()) {
        error(errors, "source_map_mismatch", base + "/nodes", "Supply one source record per bounded document node in preorder.");
        return errors;
    }
    if (!file_valid(map.file)) {
        error(errors, "source_map_file", base + "/file", "Use an opaque file identity of at most 4096 valid UTF-8 bytes.");
        return errors;
    }
    std::size_t index = 0;
    if (check_node(document.root, "/root", 1, map, base, index, errors) && index != map.nodes.size())
        error(errors, "source_map_mismatch", base + "/nodes", "Supply one source record per bounded document node in preorder.");
    return errors;
}

std::vector<Diagnostic> locate_diagnostics(const UiDocument& document, const DocumentSourceMap& map,
                                          std::span<Diagnostic> diagnostics) {
    std::vector<Diagnostic> errors;
    std::size_t bytes = 0;
    if (diagnostics.size() > max_source_diagnostics) {
        error(errors, "diagnostic_limit", "/diagnostics", "Locate at most 4096 diagnostics per call.");
        return errors;
    }
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.path.size() > max_serialized_bytes - bytes) {
            error(errors, "diagnostic_limit", "/diagnostics", "Keep diagnostic pointers within a shared 4 MiB byte budget.");
            return errors;
        }
        bytes += diagnostic.path.size();
    }
    errors = validate_sources(document, map);
    if (!errors.empty()) return errors;
    std::map<std::string_view, const NodeSource*, std::less<>> nodes;
    for (const auto& node : map.nodes) nodes.emplace(node.pointer, &node);
    std::vector<std::optional<DiagnosticSource>> locations;
    locations.reserve(diagnostics.size());
    for (const auto& diagnostic : diagnostics) {
        const std::string_view path = diagnostic.path;
        std::optional<DiagnosticSource> location;
        if (path == "/root" || path.starts_with("/root/")) {
            std::size_t end = 5;
            // Select the addressed node, never an existing ancestor of a
            // missing child. Token spelling must match the canonical pointer.
            while (path.substr(end).starts_with("/children/")) {
                end += 10;
                const auto next = path.find('/', end);
                end = next == std::string_view::npos ? path.size() : next;
            }
            const auto found = nodes.find(path.substr(0, end));
            if (found != nodes.end()) {
                const auto& node = *found->second;
                const auto field = path.substr(end);
                auto argument = [&](const auto& origins, std::string_view prefix) {
                    if (!field.starts_with(prefix)) return;
                    const auto token = field.substr(prefix.size(), field.find('/', prefix.size()) - prefix.size());
                    for (const auto& [name, source] : origins)
                        if (detail::pointer_token(name) == token) {
                            location = DiagnosticSource{source, DiagnosticOrigin::argument};
                            break;
                        }
                };
                argument(node.property_arguments, "/properties/");
                argument(node.event_arguments, "/events/");
                if ((field == "/style" || field.starts_with("/style/")) && node.style_argument)
                    location = DiagnosticSource{*node.style_argument, DiagnosticOrigin::argument};
                if (!location && field.starts_with("/properties/")) {
                    const auto token = field.substr(12, field.find('/', 12) - 12);
                    for (const auto& [name, span] : node.properties)
                        if (detail::pointer_token(name) == token) {
                            location = DiagnosticSource{{node.file.value_or(map.file), span}, DiagnosticOrigin::value};
                            break;
                        }
                }
                const bool instance_field = field.starts_with("/properties/") || field.starts_with("/events/") ||
                    field == "/style" || field.starts_with("/style/");
                if (!location && instance_field && node.invocation)
                    location = DiagnosticSource{*node.invocation, DiagnosticOrigin::invocation};
                if (!location && node.mapped)
                    location = DiagnosticSource{{node.file.value_or(map.file), node.span}, DiagnosticOrigin::node};
                if (!location && node.invocation)
                    location = DiagnosticSource{*node.invocation, DiagnosticOrigin::invocation};
            }
        }
        locations.push_back(std::move(location));
    }
    for (std::size_t i = 0; i < diagnostics.size(); ++i) diagnostics[i].source = std::move(locations[i]);
    return {};
}
} // namespace tessera
