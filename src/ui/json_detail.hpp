#pragma once

#include <tessera/ui/document.hpp>
#include <string_view>

namespace tessera::detail {
bool valid_utf8(std::string_view);
std::string pointer_token(std::string_view);

// Shared private JSON machinery. Each format supplies its own byte bound and schema.
struct ParsedJson {
    JsonValue root;
    std::map<std::string, std::size_t, std::less<>> offsets;
};
Result<ParsedJson> parse_json(std::string_view, std::size_t byte_limit, std::size_t value_limit);
void annotate_json(std::vector<Diagnostic>&, const ParsedJson&);
std::string write_json_value(const JsonValue&); // Canonical keys, no trailing newline.
}
