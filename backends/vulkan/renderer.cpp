#include <tessera/vulkan/renderer.hpp>
#include "placeholder_glyph.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace tessera {
namespace {
void require(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed: VkResult " + std::to_string(result));
}
Diagnostic error(std::string code, std::string path, std::string message) {
    return {std::move(code), Severity::error, std::move(path), std::move(message), {}};
}
float linear(float v) {
    return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
}
struct Primitive {
    std::array<float, 4> transform0, transform1, rect, color, widths, source, params;
};
static_assert(sizeof(Primitive) == 112);
struct State {
    Affine2D transform;
    // Physical, continuously valued clip edges; rounding happens after intersection.
    double left = 0, top = 0, right = 0, bottom = 0;
};
struct Packet {
    Primitive primitive{};
    VkRect2D scissor{};
    std::uint64_t image = 0;
};
bool compatible(const Packet& a, const Packet& b) {
    return a.image == b.image && a.scissor.offset.x == b.scissor.offset.x &&
        a.scissor.offset.y == b.scissor.offset.y && a.scissor.extent.width == b.scissor.extent.width &&
        a.scissor.extent.height == b.scissor.extent.height;
}
bool representable(double value) {
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}
bool compose(Affine2D& a, const Affine2D& b) {
    const std::array<double, 6> values{
        double(a.a)*b.a + double(a.c)*b.b, double(a.b)*b.a + double(a.d)*b.b,
        double(a.a)*b.c + double(a.c)*b.d, double(a.b)*b.c + double(a.d)*b.d,
        double(a.a)*b.tx + double(a.c)*b.ty + a.tx, double(a.b)*b.tx + double(a.d)*b.ty + a.ty};
    for (double value : values) if (!representable(value)) return false;
    a = {float(values[0]), float(values[1]), float(values[2]), float(values[3]), float(values[4]), float(values[5])};
    const double det = double(a.a)*a.d - double(a.b)*a.c;
    return std::isfinite(det) && det != 0;
}
bool bounds(const Rect& rect, const Affine2D& t, float scale, std::array<double, 4>& out) {
    out = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
           -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (int i = 0; i < 4; ++i) {
        const double x = double(rect.origin.x) + (i & 1 ? rect.size.width : 0);
        const double y = double(rect.origin.y) + (i & 2 ? rect.size.height : 0);
        const double px = (t.a*x + t.c*y + t.tx)*scale;
        const double py = (t.b*x + t.d*y + t.ty)*scale;
        // The shader uses float arithmetic, including intermediate local coordinates.
        if (!representable(x) || !representable(y) || !representable(px) || !representable(py)) return false;
        // Also bound intermediates: opposite large terms can cancel in double yet
        // overflow in the shader before cancellation. Reserve room for NDC * 2.
        const double sx = std::abs(t.a*x) + std::abs(t.c*y) + std::abs(t.tx);
        const double sy = std::abs(t.b*x) + std::abs(t.d*y) + std::abs(t.ty);
        if (!representable(sx) || !representable(sy) ||
            sx*scale > std::numeric_limits<float>::max()/4.0 ||
            sy*scale > std::numeric_limits<float>::max()/4.0) return false;
        out[0] = std::min(out[0], px); out[1] = std::min(out[1], py);
        out[2] = std::max(out[2], px); out[3] = std::max(out[3], py);
    }
    return true;
}
}

struct VulkanRenderer::Impl {
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout image_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkPipeline solid = VK_NULL_HANDLE, textured = VK_NULL_HANDLE;
    VkPipeline batch_solid = VK_NULL_HANDLE, batch_textured = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceLimits limits{};
    VulkanTarget target;
    std::uint64_t last_frame = 0, completed = 0;
    std::uint32_t max_images = 0;
    bool placeholder_text = false;
    struct Image { VkDescriptorSet set; std::uint64_t last_use = 0; };
    std::map<std::uint64_t, Image> images;
    struct Upload {
        VkDevice device;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        explicit Upload(VkDevice value) : device(value) {}
        ~Upload() {
            if (buffer) vkDestroyBuffer(device, buffer, nullptr);
            if (memory) vkFreeMemory(device, memory, nullptr);
        }
    };
    std::map<std::uint64_t, std::unique_ptr<Upload>> uploads;
    VulkanSubmissionStats stats;

    ~Impl() {
        if (batch_textured) vkDestroyPipeline(device, batch_textured, nullptr);
        if (batch_solid) vkDestroyPipeline(device, batch_solid, nullptr);
        if (textured) vkDestroyPipeline(device, textured, nullptr);
        if (solid) vkDestroyPipeline(device, solid, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
        if (image_layout) vkDestroyDescriptorSetLayout(device, image_layout, nullptr);
    }
    VkShaderModule shader(std::span<const std::uint32_t> code) {
        if (code.size() < 5 || code[0] != 0x07230203)
            throw std::invalid_argument("Provide compiled SPIR-V shader words.");
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size_bytes(); info.pCode = code.data();
        VkShaderModule module;
        require(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
        return module;
    }
    std::unique_ptr<Upload> upload(const std::vector<Packet>& packets) {
        auto result = std::make_unique<Upload>(device);
        VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = VkDeviceSize(packets.size()) * sizeof(Primitive);
        info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        require(vkCreateBuffer(device, &info, nullptr, &result->buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, result->buffer, &requirements);
        std::uint32_t type = UINT32_MAX;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if (!(requirements.memoryTypeBits & (1u << i)) ||
                !(memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
            type = i;
            if (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
        }
        if (type == UINT32_MAX) throw std::runtime_error("Batch uploads require host-visible vertex buffer memory.");
        VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
        require(vkAllocateMemory(device, &allocation, nullptr, &result->memory), "vkAllocateMemory");
        require(vkBindBufferMemory(device, result->buffer, result->memory, 0), "vkBindBufferMemory");
        void* mapped;
        require(vkMapMemory(device, result->memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
        auto* destination = static_cast<unsigned char*>(mapped);
        for (const auto& packet : packets) {
            std::memcpy(destination, &packet.primitive, sizeof(Primitive)); destination += sizeof(Primitive);
        }
        VkResult flushed = VK_SUCCESS;
        if (!(memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VkMappedMemoryRange range{}; range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = result->memory; range.size = VK_WHOLE_SIZE;
            flushed = vkFlushMappedMemoryRanges(device, 1, &range);
        }
        vkUnmapMemory(device, result->memory);
        require(flushed, "vkFlushMappedMemoryRanges");
        return result;
    }
    VkPipeline pipeline(VkRenderPass pass, VkShaderModule vertex, VkShaderModule fragment, const char* entry, bool batch = false) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto& stage : stages) stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vertex; stages[0].pName = batch ? "batchMain" : "vertexMain";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fragment; stages[1].pName = entry;
        VkPipelineVertexInputStateCreateInfo vertices{};
        vertices.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        const VkVertexInputBindingDescription binding{0, sizeof(Primitive), VK_VERTEX_INPUT_RATE_INSTANCE};
        std::array<VkVertexInputAttributeDescription, 7> attributes{};
        for (std::uint32_t i = 0; i < attributes.size(); ++i)
            attributes[i] = {i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16};
        if (batch) {
            vertices.vertexBindingDescriptionCount = 1; vertices.pVertexBindingDescriptions = &binding;
            vertices.vertexAttributeDescriptionCount = std::uint32_t(attributes.size());
            vertices.pVertexAttributeDescriptions = attributes.data();
        }
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1; blend.pAttachments = &attachment;
        const VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamic_states;
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2; info.pStages = stages; info.pVertexInputState = &vertices;
        info.pInputAssemblyState = &assembly; info.pViewportState = &viewport;
        info.pRasterizationState = &raster; info.pMultisampleState = &samples;
        info.pColorBlendState = &blend; info.pDynamicState = &dynamic;
        info.layout = layout; info.renderPass = pass;
        VkPipeline result = VK_NULL_HANDLE;
        const auto status = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &result);
        if (status != VK_SUCCESS && result) vkDestroyPipeline(device, result, nullptr);
        require(status, "vkCreateGraphicsPipelines");
        return result;
    }
};

VulkanRenderer::VulkanRenderer(const VulkanContext& context) : impl_(std::make_unique<Impl>()) {
    if (!context.physical_device || !context.device || !context.render_pass || !context.max_images ||
        context.max_images > std::numeric_limits<std::uint32_t>::max()/2)
        throw std::invalid_argument("Provide host Vulkan physical device/device/render pass and bounded image capacity.");
    if (context.format != VK_FORMAT_R8G8B8A8_SRGB && context.format != VK_FORMAT_B8G8R8A8_SRGB)
        throw std::invalid_argument("The prototype requires an RGBA8/BGRA8 sRGB color attachment.");
    auto& p = *impl_;
    p.device = context.device; p.max_images = context.max_images;
    p.placeholder_text = context.placeholder_text;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(context.physical_device, &properties);
    vkGetPhysicalDeviceMemoryProperties(context.physical_device, &p.memory);
    p.limits = properties.limits;
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    bindings[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo descriptors{};
    descriptors.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptors.bindingCount = 2; descriptors.pBindings = bindings;
    require(vkCreateDescriptorSetLayout(p.device, &descriptors, nullptr, &p.image_layout), "vkCreateDescriptorSetLayout");
    const VkDescriptorPoolSize sizes[]{ {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, context.max_images},
                                      {VK_DESCRIPTOR_TYPE_SAMPLER, context.max_images} };
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = context.max_images; pool.poolSizeCount = 2; pool.pPoolSizes = sizes;
    require(vkCreateDescriptorPool(p.device, &pool, nullptr, &p.pool), "vkCreateDescriptorPool");
    VkPushConstantRange constants{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Primitive)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.setLayoutCount = 1; layout.pSetLayouts = &p.image_layout;
    layout.pushConstantRangeCount = 1; layout.pPushConstantRanges = &constants;
    require(vkCreatePipelineLayout(p.device, &layout, nullptr, &p.layout), "vkCreatePipelineLayout");
    // Clean up shader modules even when a later pipeline creation fails.
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE, image = VK_NULL_HANDLE, batch = VK_NULL_HANDLE;
    try {
        vertex = p.shader(context.vertex_spirv); fragment = p.shader(context.fragment_spirv); image = p.shader(context.image_spirv);
        p.solid = p.pipeline(context.render_pass, vertex, fragment, "fragmentMain");
        p.textured = p.pipeline(context.render_pass, vertex, image, "imageMain");
        if (!context.batch_vertex_spirv.empty()) {
            batch = p.shader(context.batch_vertex_spirv);
            p.batch_solid = p.pipeline(context.render_pass, batch, fragment, "fragmentMain", true);
            p.batch_textured = p.pipeline(context.render_pass, batch, image, "imageMain", true);
        }
    } catch (...) {
        if (vertex) vkDestroyShaderModule(p.device, vertex, nullptr);
        if (fragment) vkDestroyShaderModule(p.device, fragment, nullptr);
        if (image) vkDestroyShaderModule(p.device, image, nullptr);
        if (batch) vkDestroyShaderModule(p.device, batch, nullptr);
        throw;
    }
    vkDestroyShaderModule(p.device, vertex, nullptr); vkDestroyShaderModule(p.device, fragment, nullptr);
    vkDestroyShaderModule(p.device, image, nullptr);
    if (batch) vkDestroyShaderModule(p.device, batch, nullptr);
}
VulkanRenderer::~VulkanRenderer() = default;
void VulkanRenderer::set_target(VulkanTarget target) { impl_->target = target; }

std::vector<Diagnostic> VulkanRenderer::bind_image(ImageHandle handle, VkImageView view, VkSampler sampler) {
    auto& p = *impl_;
    if (!handle.value || !view || !sampler)
        return {error("invalid_handle", "/image", "Provide a nonzero image handle and host view/sampler.")};
    auto found = p.images.find(handle.value);
    if (found != p.images.end() && found->second.last_use > p.completed)
        return {error("resource_in_use", "/image", "Retire the last referencing frame before replacing this image.")};
    if (found == p.images.end() && p.images.size() == p.max_images)
        return {error("image_limit", "/image", "Unbind a retired image or create a renderer with greater capacity.")};
    VkDescriptorSet set;
    if (found != p.images.end()) set = found->second.set;
    else {
        // Reserve map storage before allocating the Vulkan object.
        found = p.images.emplace(handle.value, Impl::Image{VK_NULL_HANDLE}).first;
        VkDescriptorSetAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptorPool = p.pool; allocation.descriptorSetCount = 1; allocation.pSetLayouts = &p.image_layout;
        const auto result = vkAllocateDescriptorSets(p.device, &allocation, &set);
        if (result != VK_SUCCESS) { p.images.erase(found); require(result, "vkAllocateDescriptorSets"); }
        found->second.set = set;
    }
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo sampling{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[2]{};
    for (auto& write : writes) { write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; write.dstSet = set; write.descriptorCount = 1; }
    writes[0].dstBinding = 0; writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; writes[0].pImageInfo = &image;
    writes[1].dstBinding = 1; writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; writes[1].pImageInfo = &sampling;
    vkUpdateDescriptorSets(p.device, 2, writes, 0, nullptr);
    return {};
}
std::vector<Diagnostic> VulkanRenderer::unbind_image(ImageHandle handle) {
    auto& p = *impl_;
    const auto found = p.images.find(handle.value);
    if (found == p.images.end()) return {error("invalid_handle", "/image", "Image is not bound to this renderer.")};
    if (found->second.last_use > p.completed)
        return {error("resource_in_use", "/image", "Retire the last referencing frame before unbinding this image.")};
    require(vkFreeDescriptorSets(p.device, p.pool, 1, &found->second.set), "vkFreeDescriptorSets");
    p.images.erase(found);
    return {};
}
void VulkanRenderer::retire(std::uint64_t frame) {
    impl_->completed = std::max(impl_->completed, std::min(frame, impl_->last_frame));
    impl_->uploads.erase(impl_->uploads.begin(), impl_->uploads.upper_bound(impl_->completed));
}
VulkanSubmissionStats VulkanRenderer::submission_stats() const { return impl_->stats; }
std::size_t VulkanRenderer::pending_uploads() const { return impl_->uploads.size(); }

std::vector<Diagnostic> VulkanRenderer::submit(const FrameInfo& frame, const UiDrawList& list) {
    auto& p = *impl_;
    auto errors = validate(frame);
    auto list_errors = validate(list);
    errors.insert(errors.end(), list_errors.begin(), list_errors.end());
    if (frame.frame <= p.last_frame)
        errors.push_back(error("stale_frame", "/frame", "Use a frame number greater than the last successful submission."));
    const auto extent = p.target.extent;
    if (!p.target.commands || !extent.width || !extent.height ||
        extent.width > p.limits.maxViewportDimensions[0] || extent.height > p.limits.maxViewportDimensions[1] ||
        extent.width > p.limits.maxFramebufferWidth || extent.height > p.limits.maxFramebufferHeight ||
        extent.width > std::uint32_t(std::numeric_limits<std::int32_t>::max()) ||
        extent.height > std::uint32_t(std::numeric_limits<std::int32_t>::max()))
        errors.push_back(error("invalid_target", "/target", "Set a recording command buffer and device-supported framebuffer extent."));
    if (std::ceil(double(frame.logical_size.width)*frame.device_scale) != extent.width ||
        std::ceil(double(frame.logical_size.height)*frame.device_scale) != extent.height)
        errors.push_back(error("extent_mismatch", "/target/extent", "Framebuffer extent must equal ceil(logical_size * device_scale)."));
    if (!errors.empty()) return errors;
    State state{{}, 0, 0, double(extent.width), double(extent.height)};
    std::vector<State> stack;
    std::vector<Packet> packets;
    std::vector<std::uint64_t> referenced_images;
    for (std::size_t i = 0; i < list.commands.size(); ++i) {
        const auto path = "/commands/" + std::to_string(i);
        std::visit([&](const auto& command) {
            using T = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<T, PushTransform>) {
                stack.push_back(state);
                if (!compose(state.transform, command.transform))
                    errors.push_back(error("geometry_overflow", path, "Composed transform must remain finite, representable and invertible."));
            } else if constexpr (std::is_same_v<T, PushClip>) {
                stack.push_back(state);
                std::array<double, 4> clip;
                if (!bounds(command.rect, state.transform, frame.device_scale, clip))
                    errors.push_back(error("geometry_overflow", path, "Transformed clip exceeds representable coordinates."));
                else {
                    state.left = std::max(state.left, clip[0]); state.top = std::max(state.top, clip[1]);
                    state.right = std::min(state.right, clip[2]); state.bottom = std::min(state.bottom, clip[3]);
                }
            } else if constexpr (std::is_same_v<T, PopClip> || std::is_same_v<T, PopTransform>) {
                state = stack.back(); stack.pop_back();
            } else if constexpr (std::is_same_v<T, DrawGlyphRun>) {
                if (!p.placeholder_text) {
                    errors.push_back(error("unsupported_command", path, "Enable placeholder_text only for default-font PlaceholderTextShaper runs."));
                    return;
                }
                if (command.run.font.value != 0) {
                    errors.push_back(error("unsupported_font", path + "/run/font", "Placeholder rendering requires FontId 0; real font glyph IDs are unsupported."));
                    return;
                }
                for (std::size_t g = 0; g < command.run.glyphs.size(); ++g) {
                    const auto& glyph = command.run.glyphs[g];
                    const auto glyph_path = path + "/run/glyphs/" + std::to_string(g);
                    if (glyph.id > 0x10ffff || (glyph.id >= 0xd800 && glyph.id <= 0xdfff)) {
                        errors.push_back(error("invalid_glyph", glyph_path + "/id", "Placeholder glyph IDs must be Unicode scalar values."));
                        continue;
                    }
                    // Position from the baseline supplied by the shaper, not run metrics.
                    const double size = command.run.size;
                    const double x = double(command.origin.x) + glyph.position.x + size * 0.05;
                    const double y = double(command.origin.y) + glyph.position.y - size * PlaceholderTextShaper::ascent_em;
                    const double width = size * 0.4, height = size * 0.7;
                    if (!representable(x) || !representable(y)) {
                        errors.push_back(error("geometry_overflow", glyph_path + "/position", "Placeholder origin plus baseline exceeds representable coordinates."));
                        continue;
                    }
                    const Rect rect{{float(x),float(y)},{float(width),float(height)}};
                    std::array<double,4> transformed;
                    if (!bounds(rect, state.transform, frame.device_scale, transformed)) {
                        errors.push_back(error("geometry_overflow", glyph_path + "/position", "Transformed placeholder exceeds representable coordinates."));
                        continue;
                    }
                    // Validate even whitespace and fully clipped glyphs before skipping them.
                    if (glyph.id == ' ' || state.right <= state.left || state.bottom <= state.top) continue;
                    Packet packet;
                    const auto left = std::floor(std::clamp(state.left, 0.0, double(extent.width)));
                    const auto top = std::floor(std::clamp(state.top, 0.0, double(extent.height)));
                    const auto right = std::ceil(std::clamp(state.right, 0.0, double(extent.width)));
                    const auto bottom = std::ceil(std::clamp(state.bottom, 0.0, double(extent.height)));
                    if (right <= left || bottom <= top || rect.size.width == 0 || rect.size.height == 0) continue;
                    packet.scissor = {{std::int32_t(left),std::int32_t(top)}, {std::uint32_t(right-left),std::uint32_t(bottom-top)}};
                    auto& data = packet.primitive;
                    const auto& t = state.transform;
                    data.transform0 = {t.a,t.c,t.tx,frame.device_scale}; data.transform1 = {t.b,t.d,t.ty,0};
                    data.rect = {rect.origin.x,rect.origin.y,rect.size.width,rect.size.height};
                    data.color = {linear(command.color.r),linear(command.color.g),linear(command.color.b),command.color.a};
                    data.params = {0,3,float(extent.width),float(extent.height)};
                    const auto rows = detail::placeholder_glyph(glyph.id);
                    for (std::size_t row = 0; row < 4; ++row) data.widths[row] = rows[row];
                    for (std::size_t row = 4; row < 7; ++row) data.source[row-4] = rows[row];
                    packets.push_back(packet);
                }
            } else {
                std::array<double, 4> transformed;
                if (!bounds(command.rect, state.transform, frame.device_scale, transformed)) {
                    errors.push_back(error("geometry_overflow", path, "Transformed primitive exceeds representable coordinates.")); return;
                }
                Packet packet;
                if constexpr (std::is_same_v<T, DrawImage>) {
                    if (!p.images.contains(command.image.value)) {
                        errors.push_back(error("invalid_handle", path + "/image", "Bind this image to the renderer before submission.")); return;
                    }
                    if (command.source.origin.x < 0 || command.source.origin.y < 0 ||
                        double(command.source.origin.x) + command.source.size.width > 1 ||
                        double(command.source.origin.y) + command.source.size.height > 1) {
                        errors.push_back(error("out_of_range", path + "/source", "Source rectangle must lie inside normalized [0,1] image coordinates.")); return;
                    }
                    packet.image = command.image.value;
                    referenced_images.push_back(packet.image);
                }
                if constexpr (std::is_same_v<T, DrawBorder>) {
                    if (!representable(double(command.widths.left) + command.widths.right) ||
                        !representable(double(command.widths.top) + command.widths.bottom)) {
                        errors.push_back(error("geometry_overflow", path + "/widths", "Combined border widths exceed representable coordinates.")); return;
                    }
                }
                if (command.rect.size.width == 0 || command.rect.size.height == 0 ||
                    state.right <= state.left || state.bottom <= state.top) return;
                const auto left = std::floor(std::clamp(state.left, 0.0, double(extent.width)));
                const auto top = std::floor(std::clamp(state.top, 0.0, double(extent.height)));
                const auto right = std::ceil(std::clamp(state.right, 0.0, double(extent.width)));
                const auto bottom = std::ceil(std::clamp(state.bottom, 0.0, double(extent.height)));
                if (right <= left || bottom <= top) return;
                packet.scissor = {{std::int32_t(left), std::int32_t(top)}, {std::uint32_t(right-left), std::uint32_t(bottom-top)}};
                auto& data = packet.primitive;
                const auto& t = state.transform;
                data.transform0 = {t.a, t.c, t.tx, frame.device_scale}; data.transform1 = {t.b, t.d, t.ty, 0};
                data.rect = {command.rect.origin.x, command.rect.origin.y, command.rect.size.width, command.rect.size.height};
                Color color;
                if constexpr (std::is_same_v<T, DrawImage>) {
                    color = command.tint;
                    data.source = {command.source.origin.x, command.source.origin.y, command.source.size.width, command.source.size.height};
                    data.params = {0, 2, float(extent.width), float(extent.height)};
                } else {
                    color = command.color;
                    data.params = {command.corner_radius, std::is_same_v<T, DrawBorder> ? 1.0f : 0.0f, float(extent.width), float(extent.height)};
                    if constexpr (std::is_same_v<T, DrawBorder>)
                        data.widths = {command.widths.top, command.widths.right, command.widths.bottom, command.widths.left};
                }
                data.color = {linear(color.r), linear(color.g), linear(color.b), color.a};
                packets.push_back(packet);
            }
        }, list.commands[i]);
    }
    if (!errors.empty()) return errors;
    if (packets.size() > std::numeric_limits<std::uint32_t>::max())
        return {error("primitive_limit", "/commands", "Use at most UINT32_MAX emitted primitives per submission.")};
    const bool batch = p.batch_solid != VK_NULL_HANDLE;
    Impl::Upload* upload = nullptr;
    if (batch && !packets.empty()) {
        auto owned = p.upload(packets);
        upload = owned.get();
        p.uploads.emplace(frame.frame, std::move(owned));
    }
    VulkanSubmissionStats stats{packets.size(), 0, upload ? packets.size() * sizeof(Primitive) : 0};
    // All diagnostics and allocations precede command recording and resource-use changes.
    const VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
    vkCmdSetViewport(p.target.commands, 0, 1, &viewport);
    VkPipeline bound = VK_NULL_HANDLE;
    if (upload) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(p.target.commands, 0, 1, &upload->buffer, &offset);
    }
    for (std::size_t first = 0; first < packets.size();) {
        const auto& packet = packets[first];
        std::size_t end = first + 1;
        if (batch) while (end < packets.size() && compatible(packet, packets[end])) ++end;
        const auto pipeline = batch ? (packet.image ? p.batch_textured : p.batch_solid) : (packet.image ? p.textured : p.solid);
        if (pipeline != bound) { vkCmdBindPipeline(p.target.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline); bound = pipeline; }
        if (packet.image) {
            auto& image = p.images.at(packet.image);
            vkCmdBindDescriptorSets(p.target.commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &image.set, 0, nullptr);
        }
        vkCmdSetScissor(p.target.commands, 0, 1, &packet.scissor);
        if (!batch) vkCmdPushConstants(p.target.commands, p.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                      0, sizeof(Primitive), &packet.primitive);
        vkCmdDraw(p.target.commands, 6, std::uint32_t(end - first), 0, batch ? std::uint32_t(first) : 0);
        ++stats.draw_calls;
        first = end;
    }
    for (auto image : referenced_images) p.images.at(image).last_use = frame.frame;
    p.last_frame = frame.frame;
    p.stats = stats;
    return {};
}
} // namespace tessera
