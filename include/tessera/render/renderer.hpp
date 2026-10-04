#pragma once

#include <tessera/layout/geometry.hpp>
#include <tessera/render/draw_list.hpp>
#include <tessera/ui/document.hpp>
#include <cstdint>
#include <vector>

namespace tessera {

struct FrameInfo {
    std::uint64_t frame = 0; // Host-assigned; nonzero and strictly increasing per renderer.
    Size logical_size;       // Finite and positive.
    float device_scale = 1;  // Physical pixels per logical unit; finite and positive.
    bool operator==(const FrameInfo&) const = default;
};

std::vector<Diagnostic> validate(const FrameInfo&);

// Backend contract. The host owns the device, queues, render target, and presentation; a
// concrete backend receives those through its own construction/host-context API.
class UiRenderer {
public:
    virtual ~UiRenderer() = default;
    UiRenderer(const UiRenderer&) = delete;
    UiRenderer& operator=(const UiRenderer&) = delete;

    // Arguments are borrowed for this call only. Invalid input records nothing and returns errors.
    // Images referenced by the frame must stay valid until retire() covers that frame.
    virtual std::vector<Diagnostic> submit(const FrameInfo&, const UiDrawList&) = 0;
    // The host observed GPU completion of every frame <= completed_frame.
    virtual void retire(std::uint64_t completed_frame) = 0;

protected:
    UiRenderer() = default;
};

} // namespace tessera
