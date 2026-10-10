#pragma once

#include <string>
#include <string_view>

namespace tessera::detail {

// FIPS 180-4 SHA-256 of the bytes, as 64 lowercase hexadecimal digits. Used for declared resource identity.
std::string sha256_hex(std::string_view bytes);

} // namespace tessera::detail
