#pragma once

#include <tessera/render/renderer.hpp>
#include <vulkan/vulkan.h>
#include <memory>
#include <span>

namespace tessera {

// All Vulkan types are confined to this optional module. Host objects are borrowed.
struct VulkanContext {
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkRenderPass render_pass = VK_NULL_HANDLE; // Subpass 0, one color attachment, 1 sample.
    VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
    std::span<const std::uint32_t> vertex_spirv;
    std::span<const std::uint32_t> fragment_spirv;
    std::span<const std::uint32_t> image_spirv;
    std::uint32_t max_images = 64;
    // Opt in only for PlaceholderTextShaper runs with default FontId (0).
    // Built-in bitmap marks are test/menu placeholders, not real font rendering.
    bool placeholder_text = false;
    // Optional instanced vertex shader. An empty span keeps the ordered reference path.
    std::span<const std::uint32_t> batch_vertex_spirv;
};
struct VulkanSubmissionStats {
    std::uint64_t primitives = 0;
    std::uint64_t draw_calls = 0;
    std::uint64_t upload_bytes = 0;
};
struct VulkanTarget {
    VkCommandBuffer commands = VK_NULL_HANDLE; // Recording inside the compatible render pass.
    VkExtent2D extent{};
};

class VulkanRenderer final : public UiRenderer {
public:
    explicit VulkanRenderer(const VulkanContext&);
    // Host must complete/retire every submitted frame before destruction.
    ~VulkanRenderer() override;

    void set_target(VulkanTarget); // Call at an update point, including after resize.
    // Host keeps view/sampler/image alive and shader-readable through retirement.
    // Handles cannot be rebound while their last referencing frame is in flight.
    std::vector<Diagnostic> bind_image(ImageHandle, VkImageView, VkSampler);
    std::vector<Diagnostic> unbind_image(ImageHandle);
    std::vector<Diagnostic> submit(const FrameInfo&, const UiDrawList&) override;
    void retire(std::uint64_t completed_frame) override;
    // Last successful submission; rejection leaves these counters unchanged.
    VulkanSubmissionStats submission_stats() const;
    std::size_t pending_uploads() const; // Retained until host completion/retire.

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tessera
