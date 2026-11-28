#include <tessera/vulkan/renderer.hpp>
#include "placeholder_glyph.hpp"
#include "../../src/detail/checks.hpp"
#include "../../src/detail/shadow.hpp"
#include "../../src/detail/stroke.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
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
float linear(float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }
struct Primitive {
    std::array<float, 4> transform0, transform1, rect, color, widths, source, params;
};
static_assert(sizeof(Primitive) == 112);
struct GradientStopData {
    std::array<float, 4> color, position;
};
static_assert(sizeof(GradientStopData) == 32);
struct State {
    Affine2D transform;
    // Physical, continuously valued clip edges; rounding happens after intersection.
    double left = 0, top = 0, right = 0, bottom = 0;
};
struct Packet {
    Primitive primitive{};
    VkRect2D scissor{};
    std::uint64_t image = 0;
    std::array<std::uint64_t, max_shader_textures> textures{}; // fill-texture-v1 images in parameter order.
    bool premultiplied = false;                                // Fixed by the image binding, so equal handles agree.
    const void* program = nullptr;     // Registered fill or shape whose pipelines draw this packet.
    std::int32_t layer = -1;           // Closed layer pass whose surface this packet composes.
    std::array<std::int32_t, 4> box{}; // Conservative pixel bounds (x0, y0, x1, y1) inside the scissor.
};
bool compatible(const Packet& a, const Packet& b) {
    return a.image == b.image && a.textures == b.textures && a.program == b.program && a.layer == b.layer &&
           a.scissor.offset.x == b.scissor.offset.x && a.scissor.offset.y == b.scissor.offset.y &&
           a.scissor.extent.width == b.scissor.extent.width && a.scissor.extent.height == b.scissor.extent.height;
}
// A pixel is inside the clip when its center is: edges [low, high) cover pixels ceil(low - 0.5) up
// to ceil(high - 0.5), the same sample rule as primitive rasterization and pixel-center pointer mapping.
std::optional<VkRect2D> scissor(const State& state, VkExtent2D extent) {
    const auto edge = [](double value, std::uint32_t limit) {
        return std::clamp(std::ceil(value - 0.5), 0.0, double(limit));
    };
    const auto left = edge(state.left, extent.width), right = edge(state.right, extent.width);
    const auto top = edge(state.top, extent.height), bottom = edge(state.bottom, extent.height);
    if (right <= left || bottom <= top) return std::nullopt;
    return VkRect2D{{std::int32_t(left), std::int32_t(top)},
                    {std::uint32_t(right - left), std::uint32_t(bottom - top)}};
}
// Physical primitive bounds widened by a pixel for fractional rasterization, inside the scissor.
std::array<std::int32_t, 4> pixel_box(const std::array<double, 4>& bounds, const VkRect2D& scissor) {
    const double x0 = scissor.offset.x, y0 = scissor.offset.y;
    const double x1 = x0 + scissor.extent.width, y1 = y0 + scissor.extent.height;
    return {std::int32_t(std::clamp(std::floor(bounds[0]) - 1, x0, x1)),
            std::int32_t(std::clamp(std::floor(bounds[1]) - 1, y0, y1)),
            std::int32_t(std::clamp(std::ceil(bounds[2]) + 1, x0, x1)),
            std::int32_t(std::clamp(std::ceil(bounds[3]) + 1, y0, y1))};
}
bool representable(double value) {
    return std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
}
bool compose(Affine2D& a, const Affine2D& b) {
    const std::array<double, 6> values{
        double(a.a) * b.a + double(a.c) * b.b,          double(a.b) * b.a + double(a.d) * b.b,
        double(a.a) * b.c + double(a.c) * b.d,          double(a.b) * b.c + double(a.d) * b.d,
        double(a.a) * b.tx + double(a.c) * b.ty + a.tx, double(a.b) * b.tx + double(a.d) * b.ty + a.ty};
    for (double value : values)
        if (!representable(value)) return false;
    a = {float(values[0]), float(values[1]), float(values[2]), float(values[3]), float(values[4]), float(values[5])};
    const double det = double(a.a) * a.d - double(a.b) * a.c;
    return std::isfinite(det) && det != 0;
}
bool bounds(const Rect& rect, const Affine2D& t, float scale, std::array<double, 4>& out) {
    out = {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
           -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (int i = 0; i < 4; ++i) {
        const double x = double(rect.origin.x) + (i & 1 ? rect.size.width : 0);
        const double y = double(rect.origin.y) + (i & 2 ? rect.size.height : 0);
        const double px = (t.a * x + t.c * y + t.tx) * scale;
        const double py = (t.b * x + t.d * y + t.ty) * scale;
        // The shader uses float arithmetic, including intermediate local coordinates.
        if (!representable(x) || !representable(y) || !representable(px) || !representable(py)) return false;
        // Also bound intermediates: opposite large terms can cancel in double yet
        // overflow in the shader before cancellation. Reserve room for NDC * 2.
        const double sx = std::abs(t.a * x) + std::abs(t.c * y) + std::abs(t.tx);
        const double sy = std::abs(t.b * x) + std::abs(t.d * y) + std::abs(t.ty);
        if (!representable(sx) || !representable(sy) || sx * scale > std::numeric_limits<float>::max() / 4.0 ||
            sy * scale > std::numeric_limits<float>::max() / 4.0)
            return false;
        out[0] = std::min(out[0], px);
        out[1] = std::min(out[1], py);
        out[2] = std::max(out[2], px);
        out[3] = std::max(out[3], py);
    }
    return true;
}
// Host shader profiles: which fragment entry point an artifact exposes and how diagnostics name it.
struct Profile {
    ShaderProfile profile; // The parameter manifest profile registered with the artifact.
    bool textures;         // Also accepts fill-texture-v1 manifests, whose artifacts may sample images.
    const char* entry;     // Required fragment entry point.
    const char* name;      // Profile names in messages.
    const char* macro;     // ABI macro that defines the entry point.
    const char *abi_code, *limit_code, *limit_message, *unknown_message;
};
constexpr Profile fill_profile{ShaderProfile::fill_v1,
                               true,
                               "proceduralMain",
                               "fill-v1 or fill-texture-v1",
                               "TESSERA_PROCEDURAL_FILL",
                               "procedural_abi",
                               "procedural_fill_limit",
                               "Unregister a retired procedural fill first.",
                               "Procedural fill is not registered with this renderer."};
constexpr Profile shape_profile{ShaderProfile::sdf_v1,
                                false,
                                "shapeMain",
                                "sdf-v1",
                                "TESSERA_CUSTOM_SHAPE",
                                "shape_abi",
                                "custom_shape_limit",
                                "Unregister a retired custom shape first.",
                                "Custom shape is not registered with this renderer."};
// Host artifacts expose the profile's fragment entry point and bind only the profile's resources: the fill
// data at set 1 binding 0 and, for fill-texture-v1 (`textured`), sampled 2D images and samplers at bindings
// 0 and 1 of sets 0, 2 and 3. Other profiles declare no image or sampler types.
std::optional<std::string> abi_problem(std::span<const std::uint32_t> code, const Profile& profile, bool textured) {
    bool entry = false;
    std::map<std::uint32_t, std::uint32_t> sets, bindings; // Decorations by target id.
    for (std::size_t i = 5; i < code.size();) {
        const std::uint32_t count = code[i] >> 16, opcode = code[i] & 0xffff;
        if (count == 0 || count > code.size() - i) return "Malformed SPIR-V instruction stream.";
        if (opcode == 15 && count >= 4 && code[i + 1] == 4) { // OpEntryPoint Fragment
            std::string name;
            bool end = false;
            for (std::size_t w = i + 3; w < i + count && !end; ++w)
                for (int b = 0; b < 4 && !end; ++b) {
                    const char c = char((code[w] >> (8 * b)) & 0xff);
                    if (c == 0)
                        end = true;
                    else
                        name.push_back(c);
                }
            entry |= name == profile.entry;
        }
        // OpTypeImage, OpTypeSampler, OpTypeSampledImage.
        if ((opcode == 25 || opcode == 26 || opcode == 27) && !textured)
            return std::string("Profile ") + (profile.textures ? "fill-v1" : profile.name) +
                   " does not permit image or sampler access.";
        // OpTypeImage operands: result, sampled type, Dim (1 is 2D), depth, arrayed, MS, Sampled (1 with a sampler).
        if (opcode == 25 && (count < 9 || code[i + 3] != 1 || code[i + 5] != 0 || code[i + 6] != 0 || code[i + 7] != 1))
            return "Profile fill-texture-v1 samples nonarrayed single-sample 2D images only; storage images are not "
                   "permitted.";
        if (opcode == 71 && count >= 4 && (code[i + 2] == 34 || code[i + 2] == 33)) // OpDecorate DescriptorSet/Binding
            (code[i + 2] == 34 ? sets : bindings)[code[i + 1]] = code[i + 3];
        i += count;
    }
    if (!entry) return std::string("Expose the fragment entry point ") + profile.entry + " (" + profile.macro + ").";
    for (const auto& [id, set] : sets) {
        const auto binding = bindings.contains(id) ? bindings.at(id) : 0;
        if (!(set == 1 && binding == 0) && !(textured && set != 1 && set <= 3 && binding <= 1))
            return "Descriptor set " + std::to_string(set) + " binding " + std::to_string(binding) +
                   " is not part of the profile's ABI.";
    }
    return std::nullopt;
}
}

struct VulkanRenderer::Impl {
    VkDevice device = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout image_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout gradient_layout = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkPipeline solid = VK_NULL_HANDLE, textured = VK_NULL_HANDLE;
    VkPipeline batch_solid = VK_NULL_HANDLE, batch_textured = VK_NULL_HANDLE;
    VkPipeline premultiplied = VK_NULL_HANDLE, batch_premultiplied = VK_NULL_HANDLE;
    // The same pipelines for layer passes.
    VkPipeline layer_solid = VK_NULL_HANDLE, layer_textured = VK_NULL_HANDLE, layer_premultiplied = VK_NULL_HANDLE;
    VkPipeline layer_batch_solid = VK_NULL_HANDLE, layer_batch_textured = VK_NULL_HANDLE,
               layer_batch_premultiplied = VK_NULL_HANDLE;
    // Retained so procedural fills registered later share the primitive vertex stages.
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkShaderModule vertex_module = VK_NULL_HANDLE, batch_module = VK_NULL_HANDLE;
    struct Fill {
        VkPipeline pipeline = VK_NULL_HANDLE, batch = VK_NULL_HANDLE;
        VkPipeline layer_pipeline = VK_NULL_HANDLE, layer_batch = VK_NULL_HANDLE; // For layer passes.
        ShaderParameters declaration;
        std::uint64_t last_use = 0;
    };
    using Programs = std::map<std::string, Fill, std::less<>>;
    Programs fills, shapes; // Separate identity spaces for procedural fills and custom shapes.
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceLimits limits{};
    VulkanTarget target;
    std::uint64_t last_frame = 0, completed = 0;
    std::uint32_t max_images = 0;
    bool placeholder_text = false;
    struct Image {
        VkDescriptorSet set;
        std::uint64_t last_use = 0;
        ImageAlpha alpha = ImageAlpha::straight;
    };
    std::map<std::uint64_t, Image> images;
    struct Upload {
        VkDevice device;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkDescriptorSet gradients = VK_NULL_HANDLE;
        VkDeviceSize bytes = 0;
        explicit Upload(VkDevice value) : device(value) {}
        ~Upload() {
            if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
            if (buffer) vkDestroyBuffer(device, buffer, nullptr);
            if (memory) vkFreeMemory(device, memory, nullptr);
        }
    };
    std::map<std::uint64_t, std::unique_ptr<Upload>> uploads;
    std::unique_ptr<Upload> empty_gradients;
    VulkanSubmissionStats stats;
    // Layer surfaces: linear half-float premultiplied color, quantized only when the main pass composes them.
    static constexpr VkFormat layer_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    bool layer_support = false; // The format can be sampled and blended as an optimal-tiling attachment.
    VkRenderPass layer_pass = VK_NULL_HANDLE;
    VkSampler layer_sampler = VK_NULL_HANDLE;
    VkDescriptorPool layer_pool = VK_NULL_HANDLE;
    struct Surface {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
        VkExtent2D extent{};
        std::uint64_t last_use = 0;
    };
    std::vector<Surface> surfaces;

    void destroy(Surface& surface) {
        if (surface.set) vkFreeDescriptorSets(device, layer_pool, 1, &surface.set);
        if (surface.framebuffer) vkDestroyFramebuffer(device, surface.framebuffer, nullptr);
        if (surface.view) vkDestroyImageView(device, surface.view, nullptr);
        if (surface.image) vkDestroyImage(device, surface.image, nullptr);
        if (surface.memory) vkFreeMemory(device, surface.memory, nullptr);
        surface = {};
    }
    // Device-local memory when available, otherwise any compatible type.
    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags preferred) const {
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & preferred) == preferred) return i;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if (bits & (1u << i)) return i;
        throw std::runtime_error("No memory type for a layer surface.");
    }
    // Leaves `surface` empty when any step fails.
    void create(Surface& surface, VkExtent2D size) {
        try {
            VkImageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            info.imageType = VK_IMAGE_TYPE_2D;
            info.format = layer_format;
            info.extent = {size.width, size.height, 1};
            info.mipLevels = info.arrayLayers = 1;
            info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            require(vkCreateImage(device, &info, nullptr, &surface.image), "vkCreateImage");
            VkMemoryRequirements requirements;
            vkGetImageMemoryRequirements(device, surface.image, &requirements);
            VkMemoryAllocateInfo allocation{};
            allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            require(vkAllocateMemory(device, &allocation, nullptr, &surface.memory), "vkAllocateMemory");
            require(vkBindImageMemory(device, surface.image, surface.memory, 0), "vkBindImageMemory");
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = surface.image;
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = layer_format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            require(vkCreateImageView(device, &view, nullptr, &surface.view), "vkCreateImageView");
            VkFramebufferCreateInfo framebuffer{};
            framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer.renderPass = layer_pass;
            framebuffer.attachmentCount = 1;
            framebuffer.pAttachments = &surface.view;
            framebuffer.width = size.width;
            framebuffer.height = size.height;
            framebuffer.layers = 1;
            require(vkCreateFramebuffer(device, &framebuffer, nullptr, &surface.framebuffer), "vkCreateFramebuffer");
            VkDescriptorSetAllocateInfo sets{};
            sets.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            sets.descriptorPool = layer_pool;
            sets.descriptorSetCount = 1;
            sets.pSetLayouts = &image_layout;
            require(vkAllocateDescriptorSets(device, &sets, &surface.set), "vkAllocateDescriptorSets");
            const VkDescriptorImageInfo image{VK_NULL_HANDLE, surface.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            const VkDescriptorImageInfo sampling{layer_sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
            VkWriteDescriptorSet writes[2]{};
            for (auto& write : writes) {
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = surface.set;
                write.descriptorCount = 1;
            }
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writes[0].pImageInfo = &image;
            writes[1].dstBinding = 1;
            writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writes[1].pImageInfo = &sampling;
            vkUpdateDescriptorSets(device, 2, writes, 0, nullptr);
            surface.extent = size;
            surface.last_use = 0;
        } catch (...) {
            destroy(surface);
            throw;
        }
    }

    void destroy(Fill& fill) {
        for (auto* pipeline : {&fill.batch, &fill.pipeline, &fill.layer_batch, &fill.layer_pipeline}) {
            if (*pipeline) vkDestroyPipeline(device, *pipeline, nullptr);
            *pipeline = VK_NULL_HANDLE;
        }
    }
    ~Impl() {
        for (auto& surface : surfaces) destroy(surface);
        if (layer_pool) vkDestroyDescriptorPool(device, layer_pool, nullptr);
        if (layer_sampler) vkDestroySampler(device, layer_sampler, nullptr);
        if (layer_pass) vkDestroyRenderPass(device, layer_pass, nullptr);
        for (auto* programs : {&fills, &shapes})
            for (auto& entry : *programs) destroy(entry.second);
        if (batch_module) vkDestroyShaderModule(device, batch_module, nullptr);
        if (vertex_module) vkDestroyShaderModule(device, vertex_module, nullptr);
        for (auto pipeline : {layer_solid, layer_textured, layer_premultiplied, layer_batch_solid, layer_batch_textured,
                              layer_batch_premultiplied})
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
        if (batch_premultiplied) vkDestroyPipeline(device, batch_premultiplied, nullptr);
        if (premultiplied) vkDestroyPipeline(device, premultiplied, nullptr);
        if (batch_textured) vkDestroyPipeline(device, batch_textured, nullptr);
        if (batch_solid) vkDestroyPipeline(device, batch_solid, nullptr);
        if (textured) vkDestroyPipeline(device, textured, nullptr);
        if (solid) vkDestroyPipeline(device, solid, nullptr);
        if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
        if (pool) vkDestroyDescriptorPool(device, pool, nullptr);
        if (image_layout) vkDestroyDescriptorSetLayout(device, image_layout, nullptr);
        if (gradient_layout) vkDestroyDescriptorSetLayout(device, gradient_layout, nullptr);
    }
    VkShaderModule shader(std::span<const std::uint32_t> code) {
        if (code.size() < 5 || code[0] != 0x07230203)
            throw std::invalid_argument("Provide compiled SPIR-V shader words.");
        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size_bytes();
        info.pCode = code.data();
        VkShaderModule module;
        require(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
        return module;
    }
    std::unique_ptr<Upload> upload(const std::vector<Packet>& packets, const std::vector<GradientStopData>& stops,
                                   bool batch) {
        auto result = std::make_unique<Upload>(device);
        const VkDeviceSize vertices = batch ? VkDeviceSize(packets.size()) * sizeof(Primitive) : 0;
        const auto alignment = limits.minStorageBufferOffsetAlignment;
        const VkDeviceSize stop_offset = stops.empty() ? vertices : (vertices + alignment - 1) / alignment * alignment;
        result->bytes = stop_offset + VkDeviceSize(stops.size()) * sizeof(GradientStopData);
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = result->bytes;
        info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        require(vkCreateBuffer(device, &info, nullptr, &result->buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, result->buffer, &requirements);
        std::uint32_t type = UINT32_MAX;
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
            if (!(requirements.memoryTypeBits & (1u << i)) ||
                !(memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                continue;
            type = i;
            if (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) break;
        }
        if (type == UINT32_MAX) throw std::runtime_error("Frame uploads require host-visible buffer memory.");
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        require(vkAllocateMemory(device, &allocation, nullptr, &result->memory), "vkAllocateMemory");
        require(vkBindBufferMemory(device, result->buffer, result->memory, 0), "vkBindBufferMemory");
        void* mapped;
        require(vkMapMemory(device, result->memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
        auto* destination = static_cast<unsigned char*>(mapped);
        if (batch) {
            for (const auto& packet : packets) {
                std::memcpy(destination, &packet.primitive, sizeof(Primitive));
                destination += sizeof(Primitive);
            }
        }
        if (!stops.empty())
            std::memcpy(static_cast<unsigned char*>(mapped) + stop_offset, stops.data(),
                        stops.size() * sizeof(GradientStopData));
        VkResult flushed = VK_SUCCESS;
        if (!(memory.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = result->memory;
            range.size = VK_WHOLE_SIZE;
            flushed = vkFlushMappedMemoryRanges(device, 1, &range);
        }
        vkUnmapMemory(device, result->memory);
        require(flushed, "vkFlushMappedMemoryRanges");
        if (!stops.empty()) {
            const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.maxSets = pool_info.poolSizeCount = 1;
            pool_info.pPoolSizes = &size;
            require(vkCreateDescriptorPool(device, &pool_info, nullptr, &result->pool), "vkCreateDescriptorPool");
            VkDescriptorSetAllocateInfo sets{};
            sets.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            sets.descriptorPool = result->pool;
            sets.descriptorSetCount = 1;
            sets.pSetLayouts = &gradient_layout;
            require(vkAllocateDescriptorSets(device, &sets, &result->gradients), "vkAllocateDescriptorSets");
            const VkDescriptorBufferInfo buffer{result->buffer, stop_offset,
                                                VkDeviceSize(stops.size()) * sizeof(GradientStopData)};
            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = result->gradients;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write.pBufferInfo = &buffer;
            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
        return result;
    }
    VkPipeline pipeline(VkRenderPass pass, VkShaderModule vertex, VkShaderModule fragment, const char* entry,
                        bool batch = false, bool premultiplied_source = false) {
        VkPipelineShaderStageCreateInfo stages[2]{};
        for (auto& stage : stages) stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = batch ? "batchMain" : "vertexMain";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = entry;
        VkPipelineVertexInputStateCreateInfo vertices{};
        vertices.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        const VkVertexInputBindingDescription binding{0, sizeof(Primitive), VK_VERTEX_INPUT_RATE_INSTANCE};
        std::array<VkVertexInputAttributeDescription, 7> attributes{};
        for (std::uint32_t i = 0; i < attributes.size(); ++i)
            attributes[i] = {i, 0, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16};
        if (batch) {
            vertices.vertexBindingDescriptionCount = 1;
            vertices.pVertexBindingDescriptions = &binding;
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
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = premultiplied_source ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorWriteMask = 15;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        const VkDynamicState dynamic_states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertices;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        info.renderPass = pass;
        VkPipeline result = VK_NULL_HANDLE;
        const auto status = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &result);
        if (status != VK_SUCCESS && result) vkDestroyPipeline(device, result, nullptr);
        require(status, "vkCreateGraphicsPipelines");
        return result;
    }
    // Registration shared by procedural fills and custom shapes; failures register nothing.
    std::vector<Diagnostic> register_program(Programs& programs, std::string_view identity,
                                             std::span<const std::uint32_t> code, const ShaderParameters& declaration,
                                             const Profile& profile, std::size_t limit) {
        std::vector<Diagnostic> errors;
        detail::Checker check(errors);
        if (&profile == &fill_profile)
            check.procedural(ProceduralFill{std::string(identity)}, "");
        else
            check.custom_shape(CustomShape{std::string(identity)}, "");
        auto declared = validate(declaration);
        for (auto& diagnostic : declared) diagnostic.path = "/declaration" + diagnostic.path;
        errors.insert(errors.end(), declared.begin(), declared.end());
        const bool samples = profile.textures && declaration.profile == ShaderProfile::fill_texture_v1;
        if (declared.empty() && declaration.profile != profile.profile && !samples)
            check.error(profile.abi_code, "/declaration/profile",
                        std::string("Register a ") + profile.name + " parameter manifest with this artifact.");
        if (code.size() < 5 || code[0] != 0x07230203)
            check.error("invalid_shader", "/fragment_spirv", "Provide compiled SPIR-V shader words.");
        else if (const auto problem = abi_problem(code, profile, samples))
            check.error(profile.abi_code, "/fragment_spirv", *problem);
        const auto found = programs.find(identity);
        if (found != programs.end() && found->second.last_use > completed)
            check.error("resource_in_use", "/shader",
                        "Retire the last referencing frame before replacing this shader.");
        if (found == programs.end() && programs.size() == limit)
            check.error(profile.limit_code, "/shader", profile.limit_message);
        if (!errors.empty()) return errors;
        Fill created;
        created.declaration = declaration;
        const auto module = shader(code);
        try {
            created.pipeline = pipeline(render_pass, vertex_module, module, profile.entry);
            if (batch_module) created.batch = pipeline(render_pass, batch_module, module, profile.entry, true);
            created.layer_pipeline = pipeline(layer_pass, vertex_module, module, profile.entry);
            if (batch_module) created.layer_batch = pipeline(layer_pass, batch_module, module, profile.entry, true);
        } catch (...) {
            destroy(created);
            vkDestroyShaderModule(device, module, nullptr);
            throw;
        }
        vkDestroyShaderModule(device, module, nullptr);
        if (found != programs.end()) {
            destroy(found->second);
            found->second = std::move(created);
        } else {
            programs.emplace(std::string(identity), std::move(created));
        }
        return {};
    }
    std::vector<Diagnostic> unregister_program(Programs& programs, std::string_view identity, const Profile& profile) {
        const auto found = programs.find(identity);
        if (found == programs.end()) return {error("unknown_shader", "/shader", profile.unknown_message)};
        if (found->second.last_use > completed)
            return {error("resource_in_use", "/shader", "Retire the last referencing frame before unregistering.")};
        destroy(found->second);
        programs.erase(found);
        return {};
    }
};

VulkanRenderer::VulkanRenderer(const VulkanContext& context) : impl_(std::make_unique<Impl>()) {
    if (!context.physical_device || !context.device || !context.render_pass || !context.max_images ||
        context.max_images > std::numeric_limits<std::uint32_t>::max() / 2)
        throw std::invalid_argument(
            "Provide host Vulkan physical device/device/render pass and bounded image capacity.");
    if (context.format != VK_FORMAT_R8G8B8A8_SRGB && context.format != VK_FORMAT_B8G8R8A8_SRGB)
        throw std::invalid_argument("The prototype requires an RGBA8/BGRA8 sRGB color attachment.");
    auto& p = *impl_;
    p.device = context.device;
    p.max_images = context.max_images;
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
    descriptors.bindingCount = 2;
    descriptors.pBindings = bindings;
    require(vkCreateDescriptorSetLayout(p.device, &descriptors, nullptr, &p.image_layout),
            "vkCreateDescriptorSetLayout");
    const VkDescriptorSetLayoutBinding gradient_binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                        VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    descriptors.bindingCount = 1;
    descriptors.pBindings = &gradient_binding;
    require(vkCreateDescriptorSetLayout(p.device, &descriptors, nullptr, &p.gradient_layout),
            "vkCreateDescriptorSetLayout");
    const VkDescriptorPoolSize sizes[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, context.max_images},
                                       {VK_DESCRIPTOR_TYPE_SAMPLER, context.max_images}};
    VkDescriptorPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = context.max_images;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    require(vkCreateDescriptorPool(p.device, &pool, nullptr, &p.pool), "vkCreateDescriptorPool");
    VkPushConstantRange constants{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Primitive)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    // Set 0 holds a DrawImage's or layer's image, or a fill-texture-v1 fill's first texture; sets 2 and 3 hold
    // that fill's other textures. Pipelines that sample nothing leave them unused.
    const VkDescriptorSetLayout layouts[]{p.image_layout, p.gradient_layout, p.image_layout, p.image_layout};
    layout.setLayoutCount = 4;
    layout.pSetLayouts = layouts;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &constants;
    require(vkCreatePipelineLayout(p.device, &layout, nullptr, &p.layout), "vkCreatePipelineLayout");
    // The solid fragment shader statically declares the stop buffer even for nongradient draws.
    p.empty_gradients = p.upload({}, {GradientStopData{}}, false);
    // Layer surfaces have their own render pass, which clears the surface and leaves it shader-readable for the
    // pass that composes it, so layer passes draw with their own copies of the primitive pipelines.
    VkFormatProperties format_properties;
    vkGetPhysicalDeviceFormatProperties(context.physical_device, Impl::layer_format, &format_properties);
    constexpr VkFormatFeatureFlags layer_features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                                    VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                                    VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    p.layer_support = (format_properties.optimalTilingFeatures & layer_features) == layer_features;
    VkAttachmentDescription attachment{};
    attachment.format = Impl::layer_format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    const VkSubpassDependency dependencies[2]{
        // Earlier sampling of a reused surface, and earlier layer passes, finish before this pass writes.
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0},
        // Later passes, including the host's main pass, sample the finished surface.
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0}};
    VkRenderPassCreateInfo pass_info{};
    pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass_info.attachmentCount = 1;
    pass_info.pAttachments = &attachment;
    pass_info.subpassCount = 1;
    pass_info.pSubpasses = &subpass;
    pass_info.dependencyCount = 2;
    pass_info.pDependencies = dependencies;
    require(vkCreateRenderPass(p.device, &pass_info, nullptr, &p.layer_pass), "vkCreateRenderPass");
    // Clean up shader modules even when a later pipeline creation fails.
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE, image = VK_NULL_HANDLE, batch = VK_NULL_HANDLE;
    try {
        vertex = p.shader(context.vertex_spirv);
        fragment = p.shader(context.fragment_spirv);
        image = p.shader(context.image_spirv);
        p.solid = p.pipeline(context.render_pass, vertex, fragment, "fragmentMain");
        p.textured = p.pipeline(context.render_pass, vertex, image, "imageMain");
        p.premultiplied = p.pipeline(context.render_pass, vertex, image, "imageMain", false, true);
        if (!context.batch_vertex_spirv.empty()) {
            batch = p.shader(context.batch_vertex_spirv);
            p.batch_solid = p.pipeline(context.render_pass, batch, fragment, "fragmentMain", true);
            p.batch_textured = p.pipeline(context.render_pass, batch, image, "imageMain", true);
            p.batch_premultiplied = p.pipeline(context.render_pass, batch, image, "imageMain", true, true);
        }
        p.layer_solid = p.pipeline(p.layer_pass, vertex, fragment, "fragmentMain");
        p.layer_textured = p.pipeline(p.layer_pass, vertex, image, "imageMain");
        p.layer_premultiplied = p.pipeline(p.layer_pass, vertex, image, "imageMain", false, true);
        if (batch) {
            p.layer_batch_solid = p.pipeline(p.layer_pass, batch, fragment, "fragmentMain", true);
            p.layer_batch_textured = p.pipeline(p.layer_pass, batch, image, "imageMain", true);
            p.layer_batch_premultiplied = p.pipeline(p.layer_pass, batch, image, "imageMain", true, true);
        }
    } catch (...) {
        if (vertex) vkDestroyShaderModule(p.device, vertex, nullptr);
        if (fragment) vkDestroyShaderModule(p.device, fragment, nullptr);
        if (image) vkDestroyShaderModule(p.device, image, nullptr);
        if (batch) vkDestroyShaderModule(p.device, batch, nullptr);
        throw;
    }
    vkDestroyShaderModule(p.device, fragment, nullptr);
    vkDestroyShaderModule(p.device, image, nullptr);
    p.render_pass = context.render_pass;
    p.vertex_module = vertex;
    p.batch_module = batch;
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    require(vkCreateSampler(p.device, &sampler, nullptr, &p.layer_sampler), "vkCreateSampler");
    const VkDescriptorPoolSize layer_sizes[]{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, std::uint32_t(max_vulkan_layers)},
                                             {VK_DESCRIPTOR_TYPE_SAMPLER, std::uint32_t(max_vulkan_layers)}};
    pool.maxSets = std::uint32_t(max_vulkan_layers);
    pool.pPoolSizes = layer_sizes;
    require(vkCreateDescriptorPool(p.device, &pool, nullptr, &p.layer_pool), "vkCreateDescriptorPool");
}
VulkanRenderer::~VulkanRenderer() = default;
void VulkanRenderer::set_target(VulkanTarget target) { impl_->target = target; }

std::vector<Diagnostic> VulkanRenderer::bind_image(ImageHandle handle, VkImageView view, VkSampler sampler,
                                                   ImageAlpha alpha) {
    auto& p = *impl_;
    if (!handle.value || !view || !sampler)
        return {error("invalid_handle", "/image", "Provide a nonzero image handle and host view/sampler.")};
    if (alpha != ImageAlpha::straight && alpha != ImageAlpha::premultiplied)
        return {error("unknown_value", "/image/alpha", "Use one of the declared enumeration values.")};
    auto found = p.images.find(handle.value);
    if (found != p.images.end() && found->second.last_use > p.completed)
        return {error("resource_in_use", "/image", "Retire the last referencing frame before replacing this image.")};
    if (found == p.images.end() && p.images.size() == p.max_images)
        return {error("image_limit", "/image", "Unbind a retired image or create a renderer with greater capacity.")};
    VkDescriptorSet set;
    if (found != p.images.end())
        set = found->second.set;
    else {
        // Reserve map storage before allocating the Vulkan object.
        found = p.images.emplace(handle.value, Impl::Image{VK_NULL_HANDLE}).first;
        VkDescriptorSetAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocation.descriptorPool = p.pool;
        allocation.descriptorSetCount = 1;
        allocation.pSetLayouts = &p.image_layout;
        const auto result = vkAllocateDescriptorSets(p.device, &allocation, &set);
        if (result != VK_SUCCESS) {
            p.images.erase(found);
            require(result, "vkAllocateDescriptorSets");
        }
        found->second.set = set;
    }
    found->second.alpha = alpha;
    const VkDescriptorImageInfo image{VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    const VkDescriptorImageInfo sampling{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[2]{};
    for (auto& write : writes) {
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = set;
        write.descriptorCount = 1;
    }
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[0].pImageInfo = &image;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[1].pImageInfo = &sampling;
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
std::vector<Diagnostic> VulkanRenderer::register_procedural_fill(const VulkanProceduralFill& fill) {
    return impl_->register_program(impl_->fills, fill.shader, fill.fragment_spirv, fill.declaration, fill_profile,
                                   max_vulkan_procedural_fills);
}
std::vector<Diagnostic> VulkanRenderer::unregister_procedural_fill(std::string_view shader) {
    return impl_->unregister_program(impl_->fills, shader, fill_profile);
}
std::vector<Diagnostic> VulkanRenderer::register_custom_shape(const VulkanCustomShape& shape) {
    return impl_->register_program(impl_->shapes, shape.shader, shape.fragment_spirv, shape.declaration, shape_profile,
                                   max_vulkan_custom_shapes);
}
std::vector<Diagnostic> VulkanRenderer::unregister_custom_shape(std::string_view shader) {
    return impl_->unregister_program(impl_->shapes, shader, shape_profile);
}
std::optional<ShaderParameters> VulkanRenderer::procedural_fill_parameters(std::string_view shader) const {
    const auto found = impl_->fills.find(shader);
    if (found == impl_->fills.end()) return std::nullopt;
    return found->second.declaration;
}
std::optional<ShaderParameters> VulkanRenderer::custom_shape_parameters(std::string_view shader) const {
    const auto found = impl_->shapes.find(shader);
    if (found == impl_->shapes.end()) return std::nullopt;
    return found->second.declaration;
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
        errors.push_back(
            error("stale_frame", "/frame", "Use a frame number greater than the last successful submission."));
    const auto extent = p.target.extent;
    if (!p.target.commands || !extent.width || !extent.height || extent.width > p.limits.maxViewportDimensions[0] ||
        extent.height > p.limits.maxViewportDimensions[1] || extent.width > p.limits.maxFramebufferWidth ||
        extent.height > p.limits.maxFramebufferHeight ||
        extent.width > std::uint32_t(std::numeric_limits<std::int32_t>::max()) ||
        extent.height > std::uint32_t(std::numeric_limits<std::int32_t>::max()))
        errors.push_back(error("invalid_target", "/target",
                               "Set a recording command buffer and device-supported framebuffer extent."));
    if (std::ceil(double(frame.logical_size.width) * frame.device_scale) != extent.width ||
        std::ceil(double(frame.logical_size.height) * frame.device_scale) != extent.height)
        errors.push_back(error("extent_mismatch", "/target/extent",
                               "Framebuffer extent must equal ceil(logical_size * device_scale)."));
    const auto layer_count = std::size_t(std::count_if(list.commands.begin(), list.commands.end(), [](const auto& c) {
        return std::holds_alternative<PushLayer>(c);
    }));
    if (layer_count && !p.target.layers)
        errors.push_back(error("invalid_target", "/target/layers",
                               "Lists with layers need a command buffer recording outside any render pass."));
    if (layer_count && !p.layer_support)
        errors.push_back(error("unsupported_command", "/commands",
                               "The device cannot render, blend and sample half-float layer surfaces."));
    if (layer_count > max_vulkan_layers)
        errors.push_back(error("layer_limit", "/commands", "Use at most 16 layers per submission."));
    if (!errors.empty()) return errors;
    State state{{}, 0, 0, double(extent.width), double(extent.height)};
    std::vector<State> stack;
    // A render pass's packets: the host's main pass, or a layer recorded into VulkanTarget::layers.
    struct Pass {
        std::vector<Packet> packets;
        std::optional<VkRect2D> clip; // Scissor at PushLayer; the surface and its composition stay inside it.
        float opacity = 1;
        VkRect2D area{};                // Pixels the layer's packets can touch.
        std::size_t surface = 0;        // Assigned Impl::surfaces entry.
        std::size_t first = 0, end = 0; // Range in the uploaded packet order.
    };
    std::vector<Pass> open(1), passes; // open.back() receives packets; closed layers in completion order.
    std::vector<GradientStopData> gradient_stops;
    std::vector<std::uint64_t> referenced_images;
    std::vector<Impl::Fill*> referenced_fills; // Registered fills and shapes.
    std::uint64_t procedural_fills = 0, procedural_fallbacks = 0, custom_shapes = 0, custom_shape_fallbacks = 0;
    // Parameter count and kinds must equal the registered declaration; reports at `at`.
    const auto matches = [&](const std::vector<ProceduralValue>& values, const Impl::Fill& program,
                             const std::string& at, const char* code) {
        static constexpr const char* kinds[] = {"number", "point", "color", "texture"};
        const auto& declared = program.declaration.parameters;
        if (values.size() != declared.size()) {
            errors.push_back(error(code, at,
                                   "Supply exactly the " + std::to_string(declared.size()) +
                                       " parameters the registered shader declares."));
            return false;
        }
        for (std::size_t v = 0; v < values.size(); ++v)
            if (kind_of(values[v]) != declared[v].kind) {
                errors.push_back(
                    error(code, at + "/" + std::to_string(v),
                          "Parameter " + declared[v].name + " takes a " + kinds[std::size_t(declared[v].kind)] + "."));
                return false;
            }
        return true;
    };
    for (std::size_t i = 0; i < list.commands.size(); ++i) {
        const auto path = "/commands/" + std::to_string(i);
        std::visit(
            [&](const auto& command) {
                using T = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<T, PushLayer>) {
                    stack.push_back(state);
                    Pass pass;
                    pass.clip = scissor(state, extent);
                    pass.opacity = command.opacity;
                    open.push_back(std::move(pass));
                } else if constexpr (std::is_same_v<T, PopLayer>) {
                    state = stack.back();
                    stack.pop_back();
                    auto pass = std::move(open.back());
                    open.pop_back();
                    if (!pass.clip) return;
                    std::array<std::int32_t, 4> box{
                        std::numeric_limits<std::int32_t>::max(), std::numeric_limits<std::int32_t>::max(),
                        std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::min()};
                    for (const auto& packet : pass.packets) {
                        if (packet.box[2] <= packet.box[0] || packet.box[3] <= packet.box[1]) continue;
                        box = {std::min(box[0], packet.box[0]), std::min(box[1], packet.box[1]),
                               std::max(box[2], packet.box[2]), std::max(box[3], packet.box[3])};
                    }
                    if (box[2] <= box[0] || box[3] <= box[1]) return; // Nothing visible to compose.
                    pass.area = {{box[0], box[1]}, {std::uint32_t(box[2] - box[0]), std::uint32_t(box[3] - box[1])}};
                    // The composition samples surface texel centers at the area's pixel centers, untransformed.
                    Packet composite;
                    composite.scissor = *pass.clip;
                    composite.box = box;
                    composite.layer = std::int32_t(passes.size());
                    composite.premultiplied = true;
                    auto& data = composite.primitive;
                    const double scale = frame.device_scale;
                    data.transform0 = {1, 0, 0, frame.device_scale};
                    data.transform1 = {0, 1, 0, 0};
                    data.rect = {float(box[0] / scale), float(box[1] / scale), float((box[2] - box[0]) / scale),
                                 float((box[3] - box[1]) / scale)};
                    data.color = {1, 1, 1, pass.opacity};
                    data.params = {0, 4, float(extent.width), float(extent.height)};
                    passes.push_back(std::move(pass));
                    open.back().packets.push_back(composite);
                } else if constexpr (std::is_same_v<T, PushTransform>) {
                    stack.push_back(state);
                    if (!compose(state.transform, command.transform))
                        errors.push_back(error("geometry_overflow", path,
                                               "Composed transform must remain finite, representable and invertible."));
                } else if constexpr (std::is_same_v<T, PushClip>) {
                    stack.push_back(state);
                    std::array<double, 4> clip;
                    if (!bounds(command.rect, state.transform, frame.device_scale, clip))
                        errors.push_back(
                            error("geometry_overflow", path, "Transformed clip exceeds representable coordinates."));
                    else {
                        state.left = std::max(state.left, clip[0]);
                        state.top = std::max(state.top, clip[1]);
                        state.right = std::min(state.right, clip[2]);
                        state.bottom = std::min(state.bottom, clip[3]);
                    }
                } else if constexpr (std::is_same_v<T, PopClip> || std::is_same_v<T, PopTransform>) {
                    state = stack.back();
                    stack.pop_back();
                } else if constexpr (std::is_same_v<T, DrawGlyphRun>) {
                    if (!p.placeholder_text) {
                        errors.push_back(
                            error("unsupported_command", path,
                                  "Enable placeholder_text only for default-font PlaceholderTextShaper runs."));
                        return;
                    }
                    if (command.run.font.value != 0) {
                        errors.push_back(
                            error("unsupported_font", path + "/run/font",
                                  "Placeholder rendering requires FontId 0; real font glyph IDs are unsupported."));
                        return;
                    }
                    for (std::size_t g = 0; g < command.run.glyphs.size(); ++g) {
                        const auto& glyph = command.run.glyphs[g];
                        const auto glyph_path = path + "/run/glyphs/" + std::to_string(g);
                        if (glyph.id > 0x10ffff || (glyph.id >= 0xd800 && glyph.id <= 0xdfff)) {
                            errors.push_back(error("invalid_glyph", glyph_path + "/id",
                                                   "Placeholder glyph IDs must be Unicode scalar values."));
                            continue;
                        }
                        // Position from the baseline supplied by the shaper, not run metrics.
                        const double size = command.run.size;
                        const double x = double(command.origin.x) + glyph.position.x + size * 0.05;
                        const double y =
                            double(command.origin.y) + glyph.position.y - size * PlaceholderTextShaper::ascent_em;
                        const double width = size * 0.4, height = size * 0.7;
                        if (!representable(x) || !representable(y)) {
                            errors.push_back(
                                error("geometry_overflow", glyph_path + "/position",
                                      "Placeholder origin plus baseline exceeds representable coordinates."));
                            continue;
                        }
                        const Rect rect{{float(x), float(y)}, {float(width), float(height)}};
                        std::array<double, 4> transformed;
                        if (!bounds(rect, state.transform, frame.device_scale, transformed)) {
                            errors.push_back(error("geometry_overflow", glyph_path + "/position",
                                                   "Transformed placeholder exceeds representable coordinates."));
                            continue;
                        }
                        // Validate even whitespace and fully clipped glyphs before skipping them.
                        if (glyph.id == ' ' || state.right <= state.left || state.bottom <= state.top) continue;
                        const auto clip = scissor(state, extent);
                        if (!clip || rect.size.width == 0 || rect.size.height == 0) continue;
                        Packet packet;
                        packet.scissor = *clip;
                        auto& data = packet.primitive;
                        const auto& t = state.transform;
                        data.transform0 = {t.a, t.c, t.tx, frame.device_scale};
                        data.transform1 = {t.b, t.d, t.ty, 0};
                        data.rect = {rect.origin.x, rect.origin.y, rect.size.width, rect.size.height};
                        data.color = {linear(command.color.r), linear(command.color.g), linear(command.color.b),
                                      command.color.a};
                        data.params = {0, 3, float(extent.width), float(extent.height)};
                        const auto rows = detail::placeholder_glyph(glyph.id);
                        for (std::size_t row = 0; row < 4; ++row) data.widths[row] = rows[row];
                        for (std::size_t row = 4; row < 7; ++row) data.source[row - 4] = rows[row];
                        packet.box = pixel_box(transformed, packet.scissor);
                        open.back().packets.push_back(packet);
                    }
                } else {
                    auto rect = command.rect;
                    std::array<float, 2> stroke_fringe{};
                    bool smooth = std::is_same_v<T, DrawStroke>;
                    bool reconstruct = smooth;
                    [[maybe_unused]] Impl::Fill* shape = nullptr;
                    if constexpr (std::is_same_v<T, DrawRect>) {
                        if (command.custom_shape) {
                            const auto found = p.shapes.find(command.custom_shape->shader);
                            if (found == p.shapes.end()) {
                                ++custom_shape_fallbacks;
                            } else {
                                if (!matches(command.custom_shape->parameters, found->second,
                                             path + "/custom_shape/parameters", "shape_parameter_mismatch"))
                                    return;
                                shape = &found->second;
                                referenced_fills.push_back(shape);
                                ++custom_shapes;
                            }
                        }
                        smooth = command.antialias;
                        // Custom shapes, like hard ellipses, sample reconstructed pixel centers.
                        reconstruct = smooth || command.shape == FillShape::ellipse || shape;
                        if (!shape && command.shape == FillShape::ellipse &&
                            ((command.rect.size.width > 0 &&
                              command.rect.size.width < std::numeric_limits<float>::min()) ||
                             (command.rect.size.height > 0 &&
                              command.rect.size.height < std::numeric_limits<float>::min()))) {
                            errors.push_back(error("geometry_overflow", path + "/shape",
                                                   "Ellipse dimensions must not require subnormal shader arithmetic."));
                            return;
                        }
                    }
                    if constexpr (std::is_same_v<T, DrawRect> || std::is_same_v<T, DrawStroke>) {
                        if (reconstruct) {
                            const auto& t = state.transform;
                            const double det = (double(t.a) * t.d - double(t.b) * t.c) * frame.device_scale;
                            const double fx = (std::abs(double(t.d)) + std::abs(double(t.c))) / std::abs(det) * .5;
                            const double fy = (std::abs(double(t.b)) + std::abs(double(t.a))) / std::abs(det) * .5;
                            const auto expanded = detail::expanded_stroke_bounds(rect, fx, fy);
                            const float shader_det = t.a * t.d - t.b * t.c;
                            const float shader_scaled_det = shader_det * frame.device_scale;
                            bool precise = std::isnormal(shader_det) && std::isnormal(shader_scaled_det) &&
                                           std::isnormal(1 / frame.device_scale);
                            for (float value : {command.corner_radius, t.a, t.b, t.c, t.d})
                                precise &= value == 0 || std::abs(value) >= std::numeric_limits<float>::min();
                            for (float value : {rect.size.width, rect.size.height})
                                precise &= value == 0 || value >= std::numeric_limits<float>::min();
                            if constexpr (std::is_same_v<T, DrawRect>) {
                                if (smooth && rect.size.width > 0 && rect.size.height > 0)
                                    precise &= representable(1 + fx / rect.size.width) &&
                                               representable(1 + fy / rect.size.height);
                            }
                            if constexpr (std::is_same_v<T, DrawStroke>) {
                                const double period = double(command.dash_length) + command.gap_length;
                                const double perimeter = 2 * double(rect.size.width) + 2 * double(rect.size.height);
                                precise &= representable(perimeter + period) &&
                                           (command.gap_length == 0 || perimeter / period <= 1048576);
                                for (float value : {command.width, command.dash_length, command.gap_length})
                                    precise &= value == 0 || value >= std::numeric_limits<float>::min();
                            }
                            if (!expanded || !representable(fx) || !representable(fy) || !precise) {
                                errors.push_back(
                                    error("geometry_overflow", path,
                                          "Smooth bounds, coverage or dash period exceed shader precision."));
                                return;
                            }
                            if (smooth) {
                                rect = *expanded;
                                stroke_fringe = {float(fx), float(fy)};
                            }
                        }
                    }
                    if constexpr (std::is_same_v<T, DrawShadow>) {
                        rect = *detail::shadow_bounds(rect, command.softness, command.inset);
                        const double softness = command.softness;
                        const double radius =
                            std::min(double(command.corner_radius),
                                     std::min(command.rect.size.width, command.rect.size.height) * .5);
                        const double inverse = softness > 0 ? 1 / softness : 0;
                        if ((softness > 0 &&
                             (!representable(inverse) || inverse < std::numeric_limits<float>::min())) ||
                            !representable(std::hypot(radius + softness + std::abs(double(command.offset.x)),
                                                      radius + softness + std::abs(double(command.offset.y)))) ||
                            (command.inset &&
                             (!representable(double(command.rect.size.width) + std::abs(double(command.offset.x))) ||
                              !representable(double(command.rect.size.height) + std::abs(double(command.offset.y)))))) {
                            errors.push_back(error("geometry_overflow", path + "/softness",
                                                   "Shadow distance or softness exceeds shader precision."));
                            return;
                        }
                    }
                    std::array<double, 4> transformed;
                    if (!bounds(rect, state.transform, frame.device_scale, transformed)) {
                        errors.push_back(error("geometry_overflow", path,
                                               "Transformed primitive exceeds representable coordinates."));
                        return;
                    }
                    Packet packet;
                    [[maybe_unused]] Impl::Fill* fill = nullptr;
                    if constexpr (std::is_same_v<T, DrawRect>) {
                        if (command.procedural) {
                            const auto found = p.fills.find(command.procedural->shader);
                            if (found == p.fills.end()) {
                                ++procedural_fallbacks;
                            } else {
                                fill = &found->second;
                                const auto& values = command.procedural->parameters;
                                if (!matches(values, *fill, path + "/procedural/parameters",
                                             "procedural_parameter_mismatch"))
                                    return;
                                // Textures borrow bound images like DrawImage, in parameter order.
                                std::size_t texture = 0;
                                for (std::size_t v = 0; v < values.size(); ++v) {
                                    const auto* handle = std::get_if<ImageHandle>(&values[v]);
                                    if (!handle) continue;
                                    if (!p.images.contains(handle->value)) {
                                        errors.push_back(error("invalid_handle",
                                                               path + "/procedural/parameters/" + std::to_string(v),
                                                               "Bind this texture to the renderer before submission."));
                                        return;
                                    }
                                    packet.textures[texture++] = handle->value;
                                    referenced_images.push_back(handle->value);
                                }
                                referenced_fills.push_back(fill);
                                ++procedural_fills;
                                packet.program = fill;
                            }
                        }
                        if (shape) packet.program = shape;
                    }
                    if constexpr (std::is_same_v<T, DrawImage>) {
                        if (!p.images.contains(command.image.value)) {
                            errors.push_back(error("invalid_handle", path + "/image",
                                                   "Bind this image to the renderer before submission."));
                            return;
                        }
                        if (command.source.origin.x < 0 || command.source.origin.y < 0 ||
                            double(command.source.origin.x) + command.source.size.width > 1 ||
                            double(command.source.origin.y) + command.source.size.height > 1) {
                            errors.push_back(
                                error("out_of_range", path + "/source",
                                      "Source rectangle must lie inside normalized [0,1] image coordinates."));
                            return;
                        }
                        packet.image = command.image.value;
                        packet.premultiplied = p.images.at(packet.image).alpha == ImageAlpha::premultiplied;
                        referenced_images.push_back(packet.image);
                    }
                    if constexpr (std::is_same_v<T, DrawBorder>) {
                        if (!representable(double(command.widths.left) + command.widths.right) ||
                            !representable(double(command.widths.top) + command.widths.bottom)) {
                            errors.push_back(error("geometry_overflow", path + "/widths",
                                                   "Combined border widths exceed representable coordinates."));
                            return;
                        }
                    }
                    if (command.rect.size.width == 0 || command.rect.size.height == 0 || state.right <= state.left ||
                        state.bottom <= state.top)
                        return;
                    const auto clip = scissor(state, extent);
                    if (!clip) return;
                    packet.scissor = *clip;
                    auto& data = packet.primitive;
                    const auto& t = state.transform;
                    data.transform0 = {t.a, t.c, t.tx, frame.device_scale};
                    data.transform1 = {t.b, t.d, t.ty, 0};
                    data.rect = {command.rect.origin.x, command.rect.origin.y, command.rect.size.width,
                                 command.rect.size.height};
                    Color color;
                    if constexpr (std::is_same_v<T, DrawImage>) {
                        color = command.tint;
                        data.source = {command.source.origin.x, command.source.origin.y, command.source.size.width,
                                       command.source.size.height};
                        data.params = {0, packet.premultiplied ? 4.0f : 2.0f, float(extent.width),
                                       float(extent.height)};
                    } else {
                        color = command.color;
                        data.params = {command.corner_radius, std::is_same_v<T, DrawBorder> ? 1.0f : 0.0f,
                                       float(extent.width), float(extent.height)};
                        if constexpr (std::is_same_v<T, DrawBorder>)
                            data.widths = {command.widths.top, command.widths.right, command.widths.bottom,
                                           command.widths.left};
                        if constexpr (std::is_same_v<T, DrawShadow>) {
                            data.params[1] = command.inset ? 10.f : 9.f;
                            data.source = {command.softness, command.softness > 0 ? 1 / command.softness : 0,
                                           command.offset.x, command.offset.y};
                        }
                        if constexpr (std::is_same_v<T, DrawStroke>) {
                            data.params[1] = 11;
                            const double period = double(command.dash_length) + command.gap_length;
                            double phase = period > 0 ? std::fmod(double(command.dash_offset), period) : 0;
                            if (phase < 0) phase += period;
                            data.widths = {command.width, command.dash_length, command.gap_length, float(phase)};
                            data.source = {stroke_fringe[0], stroke_fringe[1], 0, 0};
                        }
                    }
                    data.color = {linear(color.r), linear(color.g), linear(color.b), color.a};
                    if constexpr (std::is_same_v<T, DrawRect>) {
                        data.transform1[3] =
                            float((command.shape == FillShape::ellipse ? 1 : 0) + (command.antialias ? 2 : 0));
                        const auto capacity =
                            std::min<VkDeviceSize>(p.limits.maxStorageBufferRange / sizeof(GradientStopData), 1u << 24);
                        const auto premultiply = [](Color c) {
                            return std::array<float, 4>{linear(c.r) * c.a, linear(c.g) * c.a, linear(c.b) * c.a, c.a};
                        };
                        // Typed values as float4 slots; colors become linear light, straight alpha.
                        const auto slots_of = [](const std::vector<ProceduralValue>& values) {
                            std::vector<std::array<float, 4>> slots;
                            for (const auto& value : values)
                                std::visit(
                                    [&](const auto& v) {
                                        using V = std::decay_t<decltype(v)>;
                                        if constexpr (std::is_same_v<V, float>)
                                            slots.push_back({v, 0, 0, 0});
                                        else if constexpr (std::is_same_v<V, Point>)
                                            slots.push_back({v.x, v.y, 0, 0});
                                        else if constexpr (std::is_same_v<V, Color>)
                                            slots.push_back({linear(v.r), linear(v.g), linear(v.b), v.a});
                                        else // Texture number and premultiplied flag, set below.
                                            slots.push_back({0, 0, 0, 0});
                                    },
                                    value);
                            return slots;
                        };
                        // Appends slots, two per 32-byte record, after checking the frame bounds.
                        const auto append_slots = [&](std::vector<std::array<float, 4>> slots, const std::string& at) {
                            const std::size_t records = (slots.size() + 1) / 2;
                            if (records > capacity - gradient_stops.size() ||
                                2 * gradient_stops.size() + slots.size() > (std::size_t(1) << 24)) {
                                errors.push_back(error("fill_data_limit", at,
                                                       "Frame fill data exceeds the device storage-buffer or "
                                                       "exact-index limit."));
                                return false;
                            }
                            if (slots.size() % 2) slots.push_back({});
                            for (std::size_t r = 0; r < slots.size(); r += 2)
                                gradient_stops.push_back({slots[r], slots[r + 1]});
                            return true;
                        };
                        // Gradient geometry into data.source; false after reporting an error.
                        const auto gradient_source = [&](const auto& g) {
                            if constexpr (std::is_same_v<std::decay_t<decltype(g)>, RadialGradient>) {
                                const double x = 1.0 / g.radius.width, y = 1.0 / g.radius.height;
                                if (!representable(x) || !representable(y) || x < std::numeric_limits<float>::min() ||
                                    y < std::numeric_limits<float>::min()) {
                                    errors.push_back(
                                        error("geometry_overflow", path + "/gradient",
                                              "Radial gradient radii exceed shader coordinate precision."));
                                    return false;
                                }
                                data.source = {g.center.x, g.center.y, float(x), float(y)};
                            } else {
                                const double w = command.rect.size.width, h = command.rect.size.height;
                                const double dx = (double(g.end.x) - g.start.x) * w,
                                             dy = (double(g.end.y) - g.start.y) * h;
                                const double length2 = dx * dx + dy * dy;
                                const double x = w * dx / length2, y = h * dy / length2;
                                const double offset = -(double(g.start.x) * x + double(g.start.y) * y);
                                const double fringe_x = command.antialias ? stroke_fringe[0] / w : 0;
                                const double fringe_y = command.antialias ? stroke_fringe[1] / h : 0;
                                if (!representable(x) || !representable(y) || !representable(offset) ||
                                    !representable(std::abs(x) * (1 + fringe_x) + std::abs(y) * (1 + fringe_y) +
                                                   std::abs(offset))) {
                                    errors.push_back(error("geometry_overflow", path + "/gradient",
                                                           "Gradient projection exceeds representable coordinates."));
                                    return false;
                                }
                                data.source = {float(x), float(y), float(offset), 0};
                            }
                            return true;
                        };
                        // Stop records (the two-color shorthand as stops at 0 and 1); sets widths.xy.
                        const auto gradient_records = [&](const auto& g) {
                            const auto count = g.stop_count();
                            if (count > capacity - gradient_stops.size()) {
                                errors.push_back(
                                    error("gradient_stop_limit", path + "/gradient/stops",
                                          "Frame stop data exceeds the device storage-buffer or exact-index limit."));
                                return false;
                            }
                            data.widths[0] = float(gradient_stops.size());
                            data.widths[1] = float(count);
                            for (std::size_t stop_index = 0; stop_index < count; ++stop_index) {
                                const auto stop = g.stop(stop_index);
                                const double gap = stop_index ? double(stop.offset) - g.stop(stop_index - 1).offset : 0;
                                if ((stop.offset > 0 && stop.offset < std::numeric_limits<float>::min()) ||
                                    (gap > 0 && gap < std::numeric_limits<float>::min())) {
                                    errors.push_back(error(
                                        "gradient_stop_precision",
                                        path + "/gradient/stops/" + std::to_string(stop_index) + "/offset",
                                        "Stop offsets and nonzero gaps must not require subnormal shader arithmetic."));
                                    return false;
                                }
                                gradient_stops.push_back({premultiply(stop.color), {stop.offset, 0, 0, 0}});
                            }
                            return true;
                        };
                        if (fill) {
                            const auto& values = command.procedural->parameters;
                            data.params[1] = 12;
                            data.widths = {float(2 * gradient_stops.size()), float(values.size()),
                                           command.procedural->time, command.procedural->opacity};
                            auto slots = slots_of(values);
                            for (std::size_t v = 0, texture = 0; v < values.size(); ++v)
                                if (const auto* handle = std::get_if<ImageHandle>(&values[v]))
                                    slots[v] = {float(texture++),
                                                p.images.at(handle->value).alpha == ImageAlpha::premultiplied ? 1.f
                                                                                                              : 0.f,
                                                0, 0};
                            if (!append_slots(std::move(slots), path + "/procedural/parameters")) return;
                        } else if (shape) {
                            // params.x selects the paint: 0 solid, 1 linear or 2 radial stops at widths.xy.
                            data.params[0] = 0;
                            data.params[1] = 13;
                            if (command.gradient &&
                                !std::visit(
                                    [&](const auto& g) {
                                        constexpr bool radial =
                                            std::is_same_v<std::decay_t<decltype(g)>, RadialGradient>;
                                        data.params[0] = radial ? 2.f : 1.f;
                                        return gradient_source(g) && gradient_records(g);
                                    },
                                    *command.gradient))
                                return;
                            // Slot widths.z holds (time, parameter count), followed by the parameters.
                            const auto& values = command.custom_shape->parameters;
                            auto slots = slots_of(values);
                            slots.insert(slots.begin(), {command.custom_shape->time, float(values.size()), 0, 0});
                            data.widths[2] = float(2 * gradient_stops.size());
                            if (!append_slots(std::move(slots), path + "/custom_shape/parameters")) return;
                        } else if (command.gradient) {
                            const bool encoded = std::visit(
                                [&](const auto& g) {
                                    constexpr bool radial = std::is_same_v<std::decay_t<decltype(g)>, RadialGradient>;
                                    if (!gradient_source(g)) return false;
                                    data.color = premultiply(g.start_color);
                                    data.widths = premultiply(g.end_color);
                                    data.params[1] = radial ? 7.f : 5.f;
                                    if (g.stops.empty()) return true;
                                    data.widths = {};
                                    data.params[1] = radial ? 8.f : 6.f;
                                    return gradient_records(g);
                                },
                                *command.gradient);
                            if (!encoded) return;
                        }
                    }
                    packet.box = pixel_box(transformed, packet.scissor);
                    open.back().packets.push_back(packet);
                }
            },
            list.commands[i]);
    }
    if (!errors.empty()) return errors;
    // Layers upload first in completion order, so a nested layer's pass precedes the pass that composes it.
    std::vector<Packet> packets;
    for (auto& pass : passes) {
        pass.first = packets.size();
        packets.insert(packets.end(), pass.packets.begin(), pass.packets.end());
        pass.end = packets.size();
    }
    const auto main_first = packets.size();
    packets.insert(packets.end(), open[0].packets.begin(), open[0].packets.end());
    if (packets.size() > std::numeric_limits<std::uint32_t>::max())
        return {error("primitive_limit", "/commands", "Use at most UINT32_MAX emitted primitives per submission.")};
    // Surfaces of unretired frames stay untouched; retired ones are reused when large enough or replaced.
    std::size_t available = max_vulkan_layers - p.surfaces.size();
    for (const auto& surface : p.surfaces) available += surface.last_use <= p.completed;
    if (passes.size() > available)
        return {error("layer_limit", "/commands",
                      "Every layer surface belongs to an unretired frame; retire completed frames first.")};
    std::vector<bool> taken(p.surfaces.size());
    for (auto& pass : passes) {
        // Surfaces share the target's pixel grid from its origin, because shaders reconstruct sample positions
        // from framebuffer coordinates; only the area is cleared, drawn and composed.
        const VkExtent2D size{std::uint32_t(pass.area.offset.x) + pass.area.extent.width,
                              std::uint32_t(pass.area.offset.y) + pass.area.extent.height};
        std::optional<std::size_t> chosen, spare;
        for (std::size_t s = 0; s < p.surfaces.size(); ++s) {
            const auto& surface = p.surfaces[s];
            if (taken[s] || surface.last_use > p.completed) continue;
            if (surface.extent.width >= size.width && surface.extent.height >= size.height) {
                const auto area = [&](std::size_t at) {
                    return std::uint64_t(p.surfaces[at].extent.width) * p.surfaces[at].extent.height;
                };
                if (!chosen || area(s) < area(*chosen)) chosen = s;
            } else if (!spare) {
                spare = s;
            }
        }
        if (!chosen) {
            // Round up so small movements reuse the surface; the area already fits the device limits.
            const auto rounded = [](std::uint32_t value, std::uint32_t limit) {
                return std::max(value, std::min((value + 63) / 64 * 64, limit));
            };
            const VkExtent2D created{
                rounded(size.width, std::min(p.limits.maxFramebufferWidth, p.limits.maxImageDimension2D)),
                rounded(size.height, std::min(p.limits.maxFramebufferHeight, p.limits.maxImageDimension2D))};
            if (p.surfaces.size() < max_vulkan_layers) {
                p.surfaces.emplace_back();
                taken.push_back(false);
                chosen = p.surfaces.size() - 1;
            } else {
                chosen = spare;
                p.destroy(p.surfaces[*chosen]);
            }
            p.create(p.surfaces[*chosen], created);
        }
        taken[*chosen] = true;
        pass.surface = *chosen;
    }
    for (auto& packet : packets)
        if (packet.layer >= 0) {
            const auto& pass = passes[std::size_t(packet.layer)];
            const auto& surface = p.surfaces[pass.surface];
            packet.primitive.source = {float(double(pass.area.offset.x) / surface.extent.width),
                                       float(double(pass.area.offset.y) / surface.extent.height),
                                       float(double(pass.area.extent.width) / surface.extent.width),
                                       float(double(pass.area.extent.height) / surface.extent.height)};
        }
    const bool batch = p.batch_solid != VK_NULL_HANDLE;
    Impl::Upload* upload = nullptr;
    if (!packets.empty() && (batch || !gradient_stops.empty())) {
        auto owned = p.upload(packets, gradient_stops, batch);
        upload = owned.get();
        p.uploads.emplace(frame.frame, std::move(owned));
    }
    VulkanSubmissionStats stats{packets.size(),
                                0,
                                upload ? upload->bytes : 0,
                                procedural_fills,
                                procedural_fallbacks,
                                custom_shapes,
                                custom_shape_fallbacks,
                                passes.size()};
    // All diagnostics and allocations precede command recording and resource-use changes.
    // Records packets [first, end) into a pass on the target's pixel grid, with scissors clamped to `area`.
    const auto record = [&](VkCommandBuffer commands, std::size_t begin, std::size_t finish, VkRect2D area,
                            bool layer_pass) {
        const VkViewport viewport{0, 0, float(extent.width), float(extent.height), 0, 1};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        if (begin == finish) return;
        if (upload && batch) {
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commands, 0, 1, &upload->buffer, &offset);
        }
        const auto gradients = upload && upload->gradients ? upload->gradients : p.empty_gradients->gradients;
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 1, 1, &gradients, 0, nullptr);
        VkPipeline bound = VK_NULL_HANDLE;
        for (std::size_t first = begin; first < finish;) {
            const auto& packet = packets[first];
            std::size_t end = first + 1;
            if (batch)
                while (end < finish && compatible(packet, packets[end])) ++end;
            const std::int64_t x0 = std::max(packet.scissor.offset.x, area.offset.x);
            const std::int64_t y0 = std::max(packet.scissor.offset.y, area.offset.y);
            const std::int64_t x1 = std::min(std::int64_t(packet.scissor.offset.x) + packet.scissor.extent.width,
                                             std::int64_t(area.offset.x) + area.extent.width);
            const std::int64_t y1 = std::min(std::int64_t(packet.scissor.offset.y) + packet.scissor.extent.height,
                                             std::int64_t(area.offset.y) + area.extent.height);
            if (x1 <= x0 || y1 <= y0) {
                first = end;
                continue;
            }
            const auto* fill = static_cast<const Impl::Fill*>(packet.program);
            const auto pick = [&](VkPipeline main_single, VkPipeline main_batch, VkPipeline layer_single,
                                  VkPipeline layer_batch) {
                return layer_pass ? (batch ? layer_batch : layer_single) : (batch ? main_batch : main_single);
            };
            const auto pipeline =
                fill ? pick(fill->pipeline, fill->batch, fill->layer_pipeline, fill->layer_batch)
                : packet.layer < 0 && !packet.image ? pick(p.solid, p.batch_solid, p.layer_solid, p.layer_batch_solid)
                : packet.premultiplied
                    ? pick(p.premultiplied, p.batch_premultiplied, p.layer_premultiplied, p.layer_batch_premultiplied)
                    : pick(p.textured, p.batch_textured, p.layer_textured, p.layer_batch_textured);
            if (pipeline != bound) {
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                bound = pipeline;
            }
            if (packet.layer >= 0) {
                const auto set = p.surfaces[passes[std::size_t(packet.layer)].surface].set;
                vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &set, 0, nullptr);
            } else if (packet.image) {
                auto& image = p.images.at(packet.image);
                vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &image.set, 0,
                                        nullptr);
            } else if (packet.textures[0]) {
                // sample() reaches every texture binding, so absent textures repeat the first.
                VkDescriptorSet sets[max_shader_textures];
                for (std::size_t t = 0; t < max_shader_textures; ++t)
                    sets[t] = p.images.at(packet.textures[t] ? packet.textures[t] : packet.textures[0]).set;
                vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 1, &sets[0], 0,
                                        nullptr);
                vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 2, 2, &sets[1], 0,
                                        nullptr);
            }
            const VkRect2D scissor{{std::int32_t(x0), std::int32_t(y0)},
                                   {std::uint32_t(x1 - x0), std::uint32_t(y1 - y0)}};
            vkCmdSetScissor(commands, 0, 1, &scissor);
            if (!batch)
                vkCmdPushConstants(commands, p.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(Primitive), &packet.primitive);
            vkCmdDraw(commands, 6, std::uint32_t(end - first), 0, batch ? std::uint32_t(first) : 0);
            ++stats.draw_calls;
            first = end;
        }
    };
    for (const auto& pass : passes) {
        auto& surface = p.surfaces[pass.surface];
        const VkClearValue clear{}; // Transparent black: an isolated layer starts empty.
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = p.layer_pass;
        begin.framebuffer = surface.framebuffer;
        begin.renderArea = pass.area;
        begin.clearValueCount = 1;
        begin.pClearValues = &clear;
        vkCmdBeginRenderPass(p.target.layers, &begin, VK_SUBPASS_CONTENTS_INLINE);
        record(p.target.layers, pass.first, pass.end, pass.area, true);
        vkCmdEndRenderPass(p.target.layers);
        surface.last_use = frame.frame;
    }
    record(p.target.commands, main_first, packets.size(), {{0, 0}, extent}, false);
    for (auto image : referenced_images) p.images.at(image).last_use = frame.frame;
    for (auto* fill : referenced_fills) fill->last_use = frame.frame;
    p.last_frame = frame.frame;
    p.stats = stats;
    return {};
}
} // namespace tessera
