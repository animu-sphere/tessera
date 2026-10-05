#pragma once

#include <tessera/ui/document.hpp>
#include <cstddef>
#include <map>
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

// Prototype tooling metadata for one authored node; never encoded as JSON v1 fields.
struct NodeSource {
    std::string pointer; // JSON pointer of the node object, e.g. "/root/children/1".
    SourceSpan span;     // The node object, braces included.
    std::map<std::string, SourceSpan, std::less<>> properties; // Property values by encoded name.
    bool operator==(const NodeSource&) const = default;
};

struct DocumentSourceMap {
    std::string file;              // Caller-supplied opaque file identity.
    std::vector<NodeSource> nodes; // Tree preorder: index i matches NodeHandle::index of a tree created
                                   // from the loaded document.
    bool operator==(const DocumentSourceMap&) const = default;
};

struct SourcedDocument {
    UiDocument document;
    DocumentSourceMap sources;
};

// load_document plus node/property spans. Diagnostics and rejection are identical to load_document.
Result<SourcedDocument> load_document_with_sources(std::string_view json, std::string file,
                                                   const ValidationContext& = {});

} // namespace tessera
