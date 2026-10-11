#pragma once

#include <tessera/commands/execution.hpp>

namespace tessera::detail {
std::vector<Diagnostic> validate_command_outcome(const CommandOutcome&);
CommandOutcome command_host_exception(std::string_view message);
} // namespace tessera::detail
