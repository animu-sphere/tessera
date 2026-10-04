#pragma once

#include <tessera/ui/document.hpp>
#include <string_view>

namespace tessera {

// Load and save validate fully; failure never returns a partially usable value.
Result<UiDocument> load_document(std::string_view json, const ValidationContext& = {});
Result<std::string> save_document(const UiDocument&, const ValidationContext& = {});

} // namespace tessera
