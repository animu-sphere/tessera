#include <tessera/fonts/replay_fonts.hpp>
#include <tessera/fonts/font_profile_serialization.hpp>
#include "../../src/detail/sha256.hpp"
#include <utility>

namespace tessera {

Result<std::string> font_profile_sha256(const FontProfile& profile) {
    auto saved = save_font_profile(profile);
    if (!saved) return {std::nullopt, std::move(saved.diagnostics)};
    return {detail::sha256_hex(*saved.value), {}};
}

std::vector<Diagnostic> verify_replay_fonts(const ReplayRecording& recording, const FontProfile& profile) {
    const auto& text = recording.environment.text;
    if (text.kind != ReplayText::Kind::font_profile)
        return {{"font_profile_mismatch", Severity::error, "/environment/text/kind",
                 "The recording declares no font profile; record it with this profile's identity or replay with "
                 "the placeholder shaper.", {}}};
    auto digest = font_profile_sha256(profile);
    if (!digest) return std::move(digest.diagnostics);
    if (*digest.value != text.sha256)
        return {{"font_profile_mismatch", Severity::error, "/environment/text/sha256",
                 "The supplied profile's SHA-256 is " + *digest.value + "; supply the recorded profile '" + text.id +
                     "' or re-record.", {}}};
    return {};
}

} // namespace tessera
