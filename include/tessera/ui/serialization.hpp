#pragma once

#include <tessera/ui/document.hpp>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace tessera {

// Load and save validate fully; failure never returns a partially usable value.
Result<UiDocument> load_document(std::string_view json, const ValidationContext& = {});
Result<std::string> save_document(const UiDocument&, const ValidationContext& = {});

// Half-open UTF-8 byte range in the loaded source.
struct SourceSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool operator==(const SourceSpan&) const = default;
};

// Tooling metadata for one resulting node; never encoded as JSON v1 fields.
struct NodeSource {
    std::string pointer; // JSON pointer of the node object, e.g. "/root/children/1".
    SourceSpan span;     // Authored node range; JSON loads include the braces.
    std::map<std::string, SourceSpan, std::less<>> properties; // Property values by encoded name.
    // Absent means DocumentSourceMap::file; a present empty identity stays empty.
    // Lowering resolves child file identities
    // before composition; pointer always addresses the resulting document.
    std::optional<std::string> file;
    bool mapped = true; // False for generated/raw nodes without an authored span.
    // Present properties without an authored value span (e.g. reactive mappings).
    std::set<std::string, std::less<>> unmapped_properties;
    bool operator==(const NodeSource&) const = default;
};

struct DocumentSourceMap {
    std::string file;              // Caller-supplied opaque file identity.
    std::vector<NodeSource> nodes; // Resulting document preorder: index i matches NodeHandle::index.
    bool operator==(const DocumentSourceMap&) const = default;
};

inline constexpr std::size_t max_source_file_bytes = 4096;
// Bounded shape, pointer, file and span validation; does not validate document
// semantics or authenticate source contents. No copying or external reads.
std::vector<Diagnostic> validate_sources(const UiDocument&, const DocumentSourceMap&,
                                         std::string_view path = "/sources");

struct SourcedDocument {
    UiDocument document;
    DocumentSourceMap sources;
};

// load_document plus node/property spans. Document diagnostics are identical;
// the caller's opaque file identity must also satisfy the source-map bound.
Result<SourcedDocument> load_document_with_sources(std::string_view json, std::string file,
                                                   const ValidationContext& = {});

} // namespace tessera
