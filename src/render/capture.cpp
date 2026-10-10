#include <tessera/render/capture.hpp>
#include "../detail/checks.hpp"
#include "../ui/json_detail.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace tessera {
namespace {

void identity(const std::string& value, const std::string& path, detail::Checker& check) {
    if (!detail::valid_utf8(value)) check.error("invalid_utf8", path, "Use valid UTF-8.");
    else if (value.empty() || value.size() > 256 ||
             std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
        check.error("invalid_identifier", path, "Use 1-256 UTF-8 bytes without ASCII controls.");
}

std::optional<std::uint32_t> physical(float logical, float scale) {
    const auto pixels = std::ceil(static_cast<double>(logical) * scale);
    if (!(pixels >= 1) || pixels > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(pixels);
}

} // namespace

std::vector<Diagnostic> validate(const CaptureBackend& backend) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    identity(backend.backend, "/backend", check);
    identity(backend.device, "/device", check);
    check.enumeration(backend.format, CaptureFormat::rgba8_srgb, "/format");
    return errors;
}

Result<CaptureExtent> capture_extent(Size logical_size, float device_scale) {
    std::vector<Diagnostic> errors;
    detail::Checker check(errors);
    check.positive(device_scale, "/device_scale");
    check.positive(logical_size.width, "/logical_size/width");
    check.positive(logical_size.height, "/logical_size/height");
    if (!errors.empty()) return {std::nullopt, std::move(errors)};
    const auto width = physical(logical_size.width, device_scale);
    const auto height = physical(logical_size.height, device_scale);
    if (!width || !height) {
        check.error("out_of_range", "/logical_size",
                    "The physical extent ceil(logical size * scale) must fit 32 bits per axis.");
        return {std::nullopt, std::move(errors)};
    }
    return {CaptureExtent{*width, *height}, {}};
}

} // namespace tessera
