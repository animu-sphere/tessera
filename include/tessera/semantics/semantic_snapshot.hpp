#pragma once

#include <tessera/semantics/semantics.hpp>
#include <string_view>

namespace tessera {

inline constexpr std::uint32_t semantic_version = 1;
inline constexpr std::size_t max_serialized_semantic_bytes = std::size_t(16) << 20;

// Owned semantic schema v1 entry. Node and relationship identities are document preorder indices;
// parent is an entry index. No process-specific tree identity, handle, or layout pointer is serialized.
struct SemanticRecord {
    std::uint32_t node = 0;
    std::optional<std::string> id;
    std::uint32_t parent = no_semantic_parent;
    SemanticRole role = SemanticRole::root;
    std::string name;
    NameSource name_source = NameSource::none;
    std::optional<std::uint32_t> labelled_by;
    bool enabled = true;
    bool focusable = false;
    bool focused = false;
    std::vector<SemanticAction> actions;
    bool operator==(const SemanticRecord&) const = default;
};

struct SemanticSnapshot {
    std::uint32_t version = semantic_version;
    // Host-owned logical update sequence, distinct from NodeHandle::tree. Adapters must retain the
    // sequence-to-live-tree mapping and reject requests after that generation is replaced.
    std::uint64_t generation = 0;
    std::uint32_t node_count = 0; // Includes flattened, hidden, and display-none authored nodes.
    std::vector<SemanticRecord> nodes;
    bool operator==(const SemanticSnapshot&) const = default;
};

struct SemanticIdentity {
    std::uint64_t generation = 0;
    std::uint32_t node = 0;
    bool operator==(const SemanticIdentity&) const = default;
};
// The host supplies its current update sequence, advancing it after every settled change. Rejects a
// different generation before ordinary semantic eligibility. Loading a snapshot never invokes anything.
Result<ActionRequest> request_semantic_action(const SemanticInput&, std::uint64_t current_generation,
                                              SemanticIdentity, std::string_view binding = "activate");

std::vector<SemanticRecord> own_semantic_records(const SemanticTree&);
Result<SemanticSnapshot> capture_semantics(const SemanticInput&, std::uint64_t generation);
std::vector<Diagnostic> validate(const SemanticSnapshot&);
// Bounded, canonical Semantic JSON v1. Loading only observes data and confers no invocation authority.
Result<SemanticSnapshot> load_semantics(std::string_view);
Result<std::string> save_semantics(const SemanticSnapshot&);

} // namespace tessera
