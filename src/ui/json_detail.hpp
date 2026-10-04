#pragma once

#include <tessera/ui/document.hpp>
#include <string_view>

namespace tessera::detail {
bool valid_utf8(std::string_view);
std::string pointer_token(std::string_view);
}
