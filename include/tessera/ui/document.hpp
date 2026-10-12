#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace tessera {

// Backend-neutral JSON values are used only for preserved extension metadata.
struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value = nullptr;
    bool operator==(const JsonValue&) const = default;
};

// Half-open UTF-8 byte range and an opaque caller-declared file identity.
struct SourceSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    bool operator==(const SourceSpan&) const = default;
};
struct SourceLocation {
    std::string file;
    SourceSpan span;
    bool operator==(const SourceLocation&) const = default;
};
enum class DiagnosticOrigin { value, point, node, argument, invocation };
struct DiagnosticSource {
    SourceLocation location;
    DiagnosticOrigin origin = DiagnosticOrigin::node;
    bool operator==(const DiagnosticSource&) const = default;
};

enum class Severity { warning, error };
struct Diagnostic {
    std::string code;
    Severity severity = Severity::error;
    std::string path; // JSON-pointer location; empty means the document root.
    std::string message;
    std::optional<std::size_t> byte_offset; // Zero-based UTF-8 offset, when parsed.
    std::optional<DiagnosticSource> source; // Owned tooling origin; absent when unavailable.
    bool operator==(const Diagnostic&) const = default;
};

template<class T>
struct Result {
    std::optional<T> value;
    std::vector<Diagnostic> diagnostics;
    explicit operator bool() const noexcept { return value.has_value(); }
};

enum class NodeKind { box, text };
struct NodeReference {
    std::string id;
    bool operator==(const NodeReference&) const = default;
};
using Property = std::variant<bool, double, std::string, NodeReference>;

struct UiNode {
    NodeKind kind = NodeKind::box;
    std::optional<std::string> id;
    std::vector<std::string> classes;
    std::map<std::string, Property, std::less<>> properties;
    std::map<std::string, std::string, std::less<>> events;
    JsonValue::Object extensions;
    std::vector<UiNode> children;
    bool operator==(const UiNode&) const = default;
};

struct UiDocument {
    std::uint32_t version = 1;
    UiNode root;
    JsonValue::Object extensions;
    bool operator==(const UiDocument&) const = default;
};

// The caller supplies action names. No callbacks or registries live in a document.
struct ValidationContext {
    std::set<std::string, std::less<>> actions;
    bool operator==(const ValidationContext&) const = default;
};

inline constexpr std::size_t max_document_depth = 64;
inline constexpr std::size_t max_document_nodes = 10000;
inline constexpr std::size_t max_json_depth = 256;
inline constexpr std::size_t max_serialized_bytes = 4 * 1024 * 1024;

std::vector<Diagnostic> validate(const UiDocument&, const ValidationContext& = {});

} // namespace tessera
