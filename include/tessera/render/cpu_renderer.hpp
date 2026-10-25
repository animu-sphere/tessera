#pragma once

#include <tessera/render/capture.hpp>
#include <tessera/render/glyph_atlas.hpp>
#include <tessera/render/renderer.hpp>
#include <map>
#include <span>

namespace tessera {
inline constexpr std::size_t max_cpu_pixel_bytes = std::size_t(256) << 20;
inline constexpr std::size_t max_cpu_images = 64;
enum class CpuImageEncoding : std::uint8_t { rgba8_srgb, rgba8_unorm };
struct CpuImage {
    CaptureExtent extent;
    std::span<const std::uint8_t> pixels;
    CpuImageEncoding encoding = CpuImageEncoding::rgba8_srgb;
    ImageAlpha alpha = ImageAlpha::straight;
};
struct CpuTarget {
    CaptureExtent extent;
    std::span<std::uint8_t> pixels; // Borrowed top-down, tightly packed RGBA8 sRGB.
};

// Scalar, synchronous reference implementation. No SDK, window, or external IO.
// Successful submit composites over existing target bytes; the host clears them.
class CpuRenderer final : public UiRenderer {
public:
    void set_target(CpuTarget target) { target_ = target; }
    // Copies image bytes. Replacement/removal requires retirement, even if clipped.
    std::vector<Diagnostic> bind_image(ImageHandle, CpuImage);
    std::vector<Diagnostic> unbind_image(ImageHandle);
    std::vector<Diagnostic> submit(const FrameInfo&, const UiDrawList&) override;
    void retire(std::uint64_t completed_frame) override;
private:
    friend class CpuFrameCapture;
    struct Image {
        CaptureExtent extent;
        std::vector<std::uint8_t> pixels;
        CpuImageEncoding encoding;
        ImageAlpha alpha;
        std::uint64_t last_frame = 0;
    };
    CpuTarget target_;
    std::map<std::uint64_t, Image> images_;
    std::uint64_t submitted_ = 0, retired_ = 0;
};

// Owns a transparent target per call and returns completed, owned bytes. Optional
// cache is borrowed for this adapter's lifetime; glyphs use ordinary atlas lowering.
class CpuFrameCapture final : public FrameCapture {
public:
    explicit CpuFrameCapture(GlyphCache* glyphs = nullptr) : glyphs_(glyphs) {}
    CaptureBackend backend() const override { return {"cpu-scalar", "scalar-rgba8"}; }
    std::vector<Diagnostic> bind_image(ImageHandle, CpuImage);
    std::vector<Diagnostic> unbind_image(ImageHandle);
    Result<CapturedImage> capture(const CaptureRequest&) override;
private:
    CpuRenderer renderer_;
    GlyphCache* glyphs_;
    bool capturing_ = false;
};
} // namespace tessera
