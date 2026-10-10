#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/render/draw_list.hpp>
#include <tessera/ui/document.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tessera {

// Prototype completed-frame capture boundary; not a stable API, image encoding, or backend selection policy.

// Pixel convention of a captured image: top-down rows of tightly packed 8-bit RGBA, sRGB-encoded RGB and the
// target's alpha channel as read back.
enum class CaptureFormat : std::uint8_t { rgba8_srgb };

// Host-declared configuration of the backend that produces captured frames. Identities are host fixture names
// (1-256 UTF-8 bytes without ASCII controls), not discovered device properties; the host states what it built.
struct CaptureBackend {
    std::string backend; // e.g. "vulkan-reference".
    std::string device;  // Device/driver identity the host declares for comparison conditions.
    CaptureFormat format = CaptureFormat::rgba8_srgb;
    bool operator==(const CaptureBackend&) const = default;
};

std::vector<Diagnostic> validate(const CaptureBackend&);

// Physical extent of a logical size at a device scale: ceil(logical * scale) per axis, computed in double.
struct CaptureExtent {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool operator==(const CaptureExtent&) const = default;
};
// Fails as out_of_range at /logical_size or /device_scale unless both are finite and positive and the
// extent fits 32 bits.
Result<CaptureExtent> capture_extent(Size logical_size, float device_scale);

// One completed-frame request; borrowed for the call only.
struct CaptureRequest {
    std::size_t generation = 0; // Update generation the paint belongs to.
    Size logical_size;
    float device_scale = 1;
    CaptureExtent extent; // capture_extent(logical_size, device_scale).
    const UiDrawList* paint = nullptr;
};

struct CapturedImage {
    CaptureExtent extent;
    CaptureFormat format = CaptureFormat::rgba8_srgb;
    std::vector<std::uint8_t> pixels; // extent.width * extent.height * 4 bytes.
    bool operator==(const CapturedImage&) const = default;
};

// Implemented by an offscreen host that owns its device, target, submission, completion, retirement, and
// readback. capture() returns only after the frame completed and its resources may be retired; it prepares
// any image resources (for example glyph atlas pages) itself. Calls are sequential and not reentrant.
class FrameCapture {
public:
    virtual ~FrameCapture() = default;
    FrameCapture(const FrameCapture&) = delete;
    FrameCapture& operator=(const FrameCapture&) = delete;

    virtual CaptureBackend backend() const = 0;
    virtual Result<CapturedImage> capture(const CaptureRequest&) = 0;

protected:
    FrameCapture() = default;
};

} // namespace tessera
