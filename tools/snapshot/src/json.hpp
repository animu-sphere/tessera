#pragma once

// Private JSON helpers of the snapshot tooling. Core's JSON machinery stays private to core; bundle members are
// small fixed schemas, so the tooling carries its own bounded strict parser and spelling.
#include <tessera/ui/document.hpp>
#include <cstddef>
#include <string>
#include <string_view>

namespace tessera::snapshot_detail {

inline constexpr std::size_t max_json_depth = 64;

bool valid_utf8(std::string_view);
// A bundle fixture identity: 1-256 UTF-8 bytes without ASCII controls.
bool identity(std::string_view);
// Strict RFC 8259 text: UTF-8 without BOM, no duplicate keys, finite numbers, at most `value_limit` values and
// max_json_depth nesting. Failures are invalid_member at "" with the byte offset.
Result<JsonValue> parse_json(std::string_view text, std::size_t value_limit);

std::string quote(std::string_view);
std::string number(double);
// Compact canonical spelling: object keys in map order, ", " and ": " separators, no trailing newline.
std::string write_json(const JsonValue&);

} // namespace tessera::snapshot_detail
