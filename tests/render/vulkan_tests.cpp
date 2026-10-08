#include <tessera/vulkan/renderer.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/ui/serialization.hpp>
#ifdef TESSERA_REAL_GLYPH_FIXTURES
#include <tessera/fonts/font_shaper.hpp>
#include <tessera/render/glyph_atlas.hpp>
#endif
#include "../check.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using tessera::test::check;
using tessera::test::has;
namespace {
void require(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
std::vector<std::uint32_t> shader(const char* name) {
    std::ifstream file(std::filesystem::path(TESSERA_SHADER_DIR) / name, std::ios::binary | std::ios::ate);
    check(bool(file), "Cannot open compiled shader");
    const auto bytes = file.tellg();
    check(bytes > 0 && bytes % 4 == 0, "Malformed shader artifact");
    std::vector<std::uint32_t> code(std::size_t(bytes)/4);
    file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), bytes);
    check(bool(file), "Cannot read compiled shader");
    return code;
}
std::atomic<unsigned> validation_errors = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++validation_errors;
    std::cerr << "Vulkan validation: " << data->pMessage << '\n';
    return VK_FALSE;
}
struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};
// This fixture is the host: all devices, commands, targets, fences and images live here.
struct Host {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkRenderPass pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Image target, texture;
    Buffer readback, staging;
    std::vector<Image> atlas_images;
    std::vector<Buffer> atlas_staging;
    VkExtent2D extent{};
    ~Host() {
        if (device) {
            vkDeviceWaitIdle(device);
            clear_atlas();
            if (sampler) vkDestroySampler(device, sampler, nullptr);
            destroy(texture); destroy_target(); destroy(staging);
            if (fence) vkDestroyFence(device, fence, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            if (pass) vkDestroyRenderPass(device, pass, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (messenger) {
            auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            destroy_debug(instance, messenger, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
    }
    void destroy(Image& image) {
        if (image.view) vkDestroyImageView(device, image.view, nullptr);
        if (image.image) vkDestroyImage(device, image.image, nullptr);
        if (image.memory) vkFreeMemory(device, image.memory, nullptr);
        image = {};
    }
    void destroy(Buffer& buffer) {
        if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
        if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
        buffer = {};
    }
    void clear_atlas() {
        for (auto& image : atlas_images) destroy(image);
        for (auto& buffer : atlas_staging) destroy(buffer);
        atlas_images.clear(); atlas_staging.clear();
    }
#ifdef TESSERA_REAL_GLYPH_FIXTURES
    void upload_atlas(const tessera::GlyphAtlasFrame& frame) {
        check(atlas_images.empty(), "Retire/unbind old atlas before upload");
        atlas_images.resize(frame.pages.size()); atlas_staging.resize(frame.pages.size());
        begin_commands();
        for (std::size_t i = 0; i < frame.pages.size(); ++i) {
            const auto& page = frame.pages[i];
            auto& image = atlas_images[i]; auto& buffer = atlas_staging[i];
            make_buffer(buffer, page.coverage.size()*4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            void* mapped;
            require(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map atlas staging");
            auto* destination = static_cast<std::uint8_t*>(mapped);
            for (std::size_t j = 0; j < page.coverage.size(); ++j) {
                destination[j*4] = destination[j*4+1] = destination[j*4+2] = 255;
                destination[j*4+3] = page.coverage[j];
            }
            vkUnmapMemory(device, buffer.memory);
            make_image(image, {page.size,page.size}, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = image.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
            copy.imageExtent = {page.size,page.size,1};
            vkCmdCopyBufferToImage(commands,buffer.buffer,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(commands,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        }
        execute(); // Host completion; staging may now be freed. No backend queue ownership.
        for (auto& buffer : atlas_staging) destroy(buffer);
        atlas_staging.clear();
    }
#endif
    void destroy_target() {
        if (framebuffer) vkDestroyFramebuffer(device, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
        destroy(target); destroy(readback);
    }
    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties memory;
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("No memory type for fixture");
    }
    void make_buffer(Buffer& buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
        VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size; info.usage = usage;
        require(vkCreateBuffer(device, &info, nullptr, &buffer.buffer), "create buffer");
        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(device, buffer.buffer, &requirements);
        VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        require(vkAllocateMemory(device, &allocation, nullptr, &buffer.memory), "allocate buffer");
        require(vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0), "bind buffer");
    }
    void make_image(Image& image, VkExtent2D size, VkImageUsageFlags usage) {
        VkImageCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_SRGB;
        info.extent = {size.width, size.height, 1}; info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT; info.tiling = VK_IMAGE_TILING_OPTIMAL; info.usage = usage;
        require(vkCreateImage(device, &info, nullptr, &image.image), "create image");
        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(device, image.image, &requirements);
        VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        require(vkAllocateMemory(device, &allocation, nullptr, &image.memory), "allocate image");
        require(vkBindImageMemory(device, image.image, image.memory, 0), "bind image");
        VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = image.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        require(vkCreateImageView(device, &view, nullptr, &image.view), "create view");
    }
    void initialize() {
        std::uint32_t count = 0;
        require(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate layers");
        std::vector<VkLayerProperties> layers(count);
        require(vkEnumerateInstanceLayerProperties(&count, layers.data()), "enumerate layers");
        bool validation = false;
        for (const auto& layer : layers) if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) validation = true;
        check(validation, "GPU fixtures require VK_LAYER_KHRONOS_validation");
        const char* layer = "VK_LAYER_KHRONOS_validation";
        const char* extensions[]{VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        const VkValidationFeatureEnableEXT feature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validation_features{}; validation_features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validation_features.enabledValidationFeatureCount = 1; validation_features.pEnabledValidationFeatures = &feature;
        VkDebugUtilsMessengerCreateInfoEXT debug_info{}; debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug_info.pfnUserCallback = debug; debug_info.pNext = &validation_features;
        VkApplicationInfo app{}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app; info.enabledLayerCount = 1; info.ppEnabledLayerNames = &layer;
        info.enabledExtensionCount = 2; info.ppEnabledExtensionNames = extensions; info.pNext = &debug_info;
        require(vkCreateInstance(&info, nullptr, &instance), "create instance");
        debug_info.pNext = nullptr;
        auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        require(create_debug(instance, &debug_info, nullptr, &messenger), "create debug messenger");
        require(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        require(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate GPUs");
        std::uint32_t family = 0;
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_1) continue;
            VkFormatProperties format;
            vkGetPhysicalDeviceFormatProperties(candidate, VK_FORMAT_R8G8B8A8_SRGB, &format);
            constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            if ((format.optimalTilingFeatures & needed) != needed) continue;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            for (std::uint32_t i = 0; i < count; ++i) if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                physical = candidate; family = i; break;
            }
            if (physical) {
                std::cout << "GPU: " << properties.deviceName << ", vendor=" << properties.vendorID << ", driver=" << properties.driverVersion
                    << ", Vulkan=" << VK_VERSION_MAJOR(properties.apiVersion) << '.' << VK_VERSION_MINOR(properties.apiVersion) << '.' << VK_VERSION_PATCH(properties.apiVersion) << '\n';
                break;
            }
        }
        check(bool(physical), "No Vulkan 1.1 graphics device with required sRGB capabilities");
        const float priority = 1;
        VkDeviceQueueCreateInfo queue_info{}; queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
        VkDeviceCreateInfo device_info{}; device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        device_info.queueCreateInfoCount = 1; device_info.pQueueCreateInfos = &queue_info;
        require(vkCreateDevice(physical, &device_info, nullptr, &device), "create device");
        vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo pool_info{}; pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = family; pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        require(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "create command pool");
        VkCommandBufferAllocateInfo command_info{}; command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        command_info.commandPool = pool; command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_info.commandBufferCount = 1;
        require(vkAllocateCommandBuffers(device, &command_info, &commands), "allocate command buffer");
        VkFenceCreateInfo fence_info{}; fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        require(vkCreateFence(device, &fence_info, nullptr, &fence), "create fence");
        VkAttachmentDescription attachment{};
        attachment.format = VK_FORMAT_R8G8B8A8_SRGB; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
        VkSubpassDependency dependencies[2]{};
        dependencies[0] = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0};
        dependencies[1] = {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0};
        VkRenderPassCreateInfo pass_info{}; pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        pass_info.attachmentCount = 1; pass_info.pAttachments = &attachment; pass_info.subpassCount = 1; pass_info.pSubpasses = &subpass;
        pass_info.dependencyCount = 2; pass_info.pDependencies = dependencies;
        require(vkCreateRenderPass(device, &pass_info, nullptr, &pass), "create render pass");
    }
    void begin_commands() {
        require(vkResetCommandBuffer(commands, 0), "reset commands");
        VkCommandBufferBeginInfo info{}; info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(vkBeginCommandBuffer(commands, &info), "begin commands");
    }
    void execute() {
        require(vkEndCommandBuffer(commands), "end commands");
        require(vkResetFences(device, 1, &fence), "reset fence");
        VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &commands;
        require(vkQueueSubmit(queue, 1, &submit, fence), "submit queue");
        require(vkWaitForFences(device, 1, &fence, VK_TRUE, 30'000'000'000ull), "wait fence");
    }
    void make_texture() {
        // Procedural test data: red, green / blue, white. No external asset dependency.
        const std::array<std::uint8_t, 16> pixels{255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
        make_buffer(staging, pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        void* mapped;
        require(vkMapMemory(device, staging.memory, 0, pixels.size(), 0, &mapped), "map texture staging");
        std::memcpy(mapped, pixels.data(), pixels.size()); vkUnmapMemory(device, staging.memory);
        make_image(texture, {2,2}, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        begin_commands();
        VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = texture.image; barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,1,0,1};
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent = {2,2,1};
        vkCmdCopyBufferToImage(commands, staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,0,nullptr,0,nullptr,1,&barrier);
        execute();
        VkSamplerCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter = info.minFilter = VK_FILTER_NEAREST; info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        require(vkCreateSampler(device, &info, nullptr, &sampler), "create sampler");
    }
    void resize(VkExtent2D size) {
        destroy_target(); extent = size;
        make_image(target, extent, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        make_buffer(readback, VkDeviceSize(extent.width)*extent.height*4, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        VkFramebufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = pass; info.attachmentCount = 1; info.pAttachments = &target.view;
        info.width = extent.width; info.height = extent.height; info.layers = 1;
        require(vkCreateFramebuffer(device, &info, nullptr, &framebuffer), "create framebuffer");
    }
    void begin(tessera::VulkanRenderer& renderer) {
        begin_commands();
        VkClearValue clear{}; clear.color.float32[3] = 1;
        VkRenderPassBeginInfo info{}; info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        info.renderPass = pass; info.framebuffer = framebuffer; info.renderArea = {{0,0},extent};
        info.clearValueCount = 1; info.pClearValues = &clear;
        vkCmdBeginRenderPass(commands, &info, VK_SUBPASS_CONTENTS_INLINE);
        renderer.set_target({commands, extent});
    }
    std::vector<std::uint8_t> finish(const char* artifact) {
        vkCmdEndRenderPass(commands);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        copy.imageExtent = {extent.width,extent.height,1};
        vkCmdCopyImageToBuffer(commands, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
        VkBufferMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = readback.buffer; barrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,0,nullptr,1,&barrier,0,nullptr);
        execute();
        std::vector<std::uint8_t> pixels(std::size_t(extent.width)*extent.height*4);
        void* mapped;
        require(vkMapMemory(device, readback.memory, 0, pixels.size(), 0, &mapped), "map readback");
        std::memcpy(pixels.data(), mapped, pixels.size()); vkUnmapMemory(device, readback.memory);
        std::filesystem::create_directories(TESSERA_GPU_ARTIFACT_DIR);
        std::ofstream file(std::filesystem::path(TESSERA_GPU_ARTIFACT_DIR) / artifact, std::ios::binary);
        file << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
        for (std::size_t i=0; i<pixels.size(); i+=4) file.write(reinterpret_cast<const char*>(pixels.data()+i), 3);
        check(bool(file), "Cannot write GPU image artifact");
        return pixels;
    }
};
void pixel(const std::vector<std::uint8_t>& pixels, unsigned width, unsigned x, unsigned y,
           std::array<int,4> expected, const char* message) {
    const auto index = (std::size_t(y)*width+x)*4;
    for (unsigned c=0; c<4; ++c) if (std::abs(int(pixels.at(index+c))-expected[c]) > 2) {
        throw std::runtime_error(std::string(message) + " at " + std::to_string(x) + "," + std::to_string(y) +
            " channel " + std::to_string(c) + ": got " + std::to_string(pixels[index+c]) + " expected " + std::to_string(expected[c]));
    }
}
void fixtures(Host& host, bool batched = false) {
    const auto vertex = shader("primitive.vertex.spv"), fragment = shader("primitive.fragment.spv"), image = shader("primitive.image.spv");
    const auto batch = shader("primitive.batch.spv");
    tessera::VulkanContext context{host.physical, host.device, host.pass, VK_FORMAT_R8G8B8A8_SRGB, vertex, fragment, image, 1};
    if (batched) context.batch_vertex_spirv = batch;
    tessera::VulkanRenderer renderer(context);
    host.resize({64,64});
    check(renderer.bind_image({1},host.texture.view,host.sampler).empty(), "Image binding failed");
    check(has(renderer.bind_image({2},host.texture.view,host.sampler), "image_limit", "/image"), "Image capacity not enforced");
    const tessera::UiDrawList list{{
        tessera::DrawRect{{{8,8},{28,28}},{1,0,0,1}},
        tessera::DrawRect{{{20,20},{28,28}},{0,0,1,0.5f}},
        tessera::DrawRect{{{44,0},{20,16}},{1,1,1,1},6},
        tessera::DrawBorder{{{0,40},{20,20}},{2,4,3,1},{0,1,0,1}},
        tessera::PushClip{{{30,40},{20,20}}}, tessera::PushClip{{{36,44},{6,8}}},
        tessera::DrawRect{{{0,0},{64,64}},{1,1,1,1}}, tessera::PopClip{}, tessera::PopClip{},
        tessera::PushTransform{{0,1,-1,0,60,40}},
        tessera::DrawRect{{{0,0},{8,12}},{0,1,1,1}}, tessera::PopTransform{},
        tessera::DrawImage{{{48,48},{16,16}},{1}},
    }};
    host.begin(renderer);
    check(renderer.submit({1,{64,64},1},list).empty(), "Primitive submission failed");
    check(has(renderer.unbind_image({1}),"resource_in_use","/image"), "In-flight image unbound");
    check(has(renderer.bind_image({1},host.texture.view,host.sampler),"resource_in_use","/image"), "In-flight image rebound");
    // Future retirement must not authorize resource mutation in later frames.
    const auto pixels = host.finish("primitives.ppm");
    pixel(pixels,64,12,12,{255,0,0,255},"Solid rectangle");
    pixel(pixels,64,24,24,{188,0,188,255},"Ordered linear-light alpha blend");
    pixel(pixels,64,40,24,{0,0,188,255},"Straight alpha blend over black");
    pixel(pixels,64,44,0,{0,0,0,255},"Rounded corner outside");
    pixel(pixels,64,54,8,{255,255,255,255},"Rounded rectangle interior");
    pixel(pixels,64,10,40,{0,255,0,255},"Top border");
    pixel(pixels,64,18,50,{0,255,0,255},"Right border");
    pixel(pixels,64,10,58,{0,255,0,255},"Bottom border");
    pixel(pixels,64,0,50,{0,255,0,255},"Left border");
    pixel(pixels,64,8,50,{0,0,0,255},"Border inner hole");
    pixel(pixels,64,38,46,{255,255,255,255},"Nested clip interior");
    pixel(pixels,64,34,50,{0,0,0,255},"Nested clip exterior");
    pixel(pixels,64,50,42,{0,255,255,255},"Rotated primitive");
    pixel(pixels,64,47,42,{0,0,188,255},"Rotated primitive bounds");
    pixel(pixels,64,50,50,{255,0,0,255},"Image top left");
    pixel(pixels,64,60,50,{0,255,0,255},"Image top right");
    pixel(pixels,64,50,60,{0,0,255,255},"Image bottom left");
    pixel(pixels,64,60,60,{255,255,255,255},"Image bottom right");
    renderer.retire(999);
    check(renderer.bind_image({1},host.texture.view,host.sampler).empty(), "Retired image cannot be rebound");

    host.begin(renderer);
    const tessera::UiDrawList bad{{tessera::DrawRect{{{0,0},{64,64}},{0,1,0,1}}, tessera::DrawGlyphRun{{},{{},16},{1,1,1,1}}}};
    check(has(renderer.submit({2,{64,64},1},bad),"unsupported_command","/commands/1"), "Unsupported glyph command accepted");
    check(has(renderer.submit({2,{64,64},1},{{tessera::DrawImage{{{0,0},{8,8}},{2}}}}),"invalid_handle","/commands/0/image"), "Unbound image accepted");
    check(has(renderer.submit({2,{64,64},1},{{tessera::DrawImage{{{0,0},{8,8}},{1},{{0.8f,0},{0.5f,1}}}}}),"out_of_range","/commands/0/source"), "Invalid normalized source accepted");
    check(has(renderer.submit({1,{64,64},1},{}),"stale_frame","/frame"), "Stale frame accepted");
    check(has(renderer.submit({2,{63,64},1},{}),"extent_mismatch","/target/extent"), "Extent mismatch accepted");
    const float huge = std::numeric_limits<float>::max();
    check(has(renderer.submit({2,{64,64},1},{{tessera::PushTransform{{huge,0,0,1,0,0}},
        tessera::PushTransform{{2,0,0,1,0,0}}, tessera::PopTransform{},tessera::PopTransform{}}}),"geometry_overflow","/commands/1"), "Composed overflow accepted");
    const auto rejected = host.finish("rejected.ppm");
    for (unsigned y=0;y<64;++y) for(unsigned x=0;x<64;++x) pixel(rejected,64,x,y,{0,0,0,255},"Rejected submission recorded commands");

    // Same unconsumed frame number is usable after rejection.
    host.begin(renderer);
    check(renderer.submit({2,{64,64},1},{{tessera::DrawImage{{{0,0},{8,8}},{1},{{0.5f,0},{0.5f,0.5f}},{0.5f,1,1,0.5f}}}}).empty(), "Cropped image submission failed");
    check(has(renderer.unbind_image({1}),"resource_in_use","/image"), "Future retire incorrectly covered a later frame");
    const auto cropped = host.finish("image-crop.ppm");
    pixel(cropped,64,4,4,{0,188,0,255},"Cropped image tint/alpha");
    renderer.retire(2);
    check(renderer.unbind_image({1}).empty(),"Retired image cannot be unbound");
    check(has(renderer.unbind_image({1}),"invalid_handle","/image"),"Unknown unbind accepted");

    check(renderer.bind_image({1},host.texture.view,host.sampler).empty(),"Image rebind failed");
    host.begin(renderer);
    const tessera::UiDrawList transforms{{
        tessera::PushTransform{{2,0,0,1,8,8}}, tessera::PushTransform{{1,0,0,2,3,2}},
        tessera::PushClip{{{0,0},{4,4}}}, tessera::DrawRect{{{-10,-10},{20,20}},{1,1,1,1}},
        tessera::PopClip{}, tessera::PopTransform{}, tessera::PopTransform{},
        tessera::PushTransform{{1,0,0.5f,1,28,8}}, tessera::DrawRect{{{0,0},{8,8}},{0,0,1,1}}, tessera::PopTransform{},
        tessera::PushClip{{{0,0},{0,0}}}, tessera::DrawImage{{{0,0},{8,8}},{1}}, tessera::PopClip{},
    }};
    check(renderer.submit({3,{64,64},1},transforms).empty(),"Composed transform submission failed");
    check(has(renderer.unbind_image({1}),"resource_in_use","/image"),"Clipped image lifetime was not retained");
    const auto transformed = host.finish("transforms.ppm");
    pixel(transformed,64,16,12,{255,255,255,255},"Composed transformed clip");
    pixel(transformed,64,13,12,{0,0,0,255},"Composed transformed clip left bound");
    pixel(transformed,64,22,12,{0,0,0,255},"Composed transformed clip right bound");
    pixel(transformed,64,34,12,{0,0,255,255},"Skewed primitive interior");
    pixel(transformed,64,29,14,{0,0,0,255},"Skewed primitive exterior");
    renderer.retire(3); check(renderer.unbind_image({1}).empty(),"Clipped image retirement failed");

    // Fractional scale, mirrored clips, empty intersection, stack restoration and resize.
    for (float scale : {1.0f,1.25f,1.5f,2.0f}) {
        const unsigned extent = unsigned(std::ceil(32*scale));
        host.resize({extent,extent}); host.begin(renderer);
        const tessera::UiDrawList scaled{{
            tessera::PushTransform{{-1,0,0,1,24,0}},
            tessera::PushClip{{{4.2f,4.2f},{6.4f,6.4f}}},
            tessera::DrawRect{{{0,0},{32,32}},{1,0,0,1}},
            tessera::PushClip{{{28,28},{2,2}}},
            tessera::DrawRect{{{0,0},{32,32}},{0,1,0,1}}, tessera::PopClip{},
            tessera::PopClip{}, tessera::PopTransform{},
            tessera::DrawRect{{{0,24},{8,8}},{0.5f,0.5f,0.5f,1}},
            tessera::DrawBorder{{{24,24},{8,8}},{1,1,1,1},{1,1,1,1},3},
        }};
        const auto frame = std::uint64_t(scale*4); // 4,5,6,8
        check(renderer.submit({frame,{32,32},scale},scaled).empty(),"Fractional scale submission failed");
        const std::string artifact = "scale-" + std::to_string(frame) + ".ppm";
        const auto scaled_pixels = host.finish(artifact.c_str()); renderer.retire(frame);
        // Pixel-center scissor: the same pixels an unclipped rectangle with these edges would cover.
        const auto edge = [&](double logical) { return unsigned(std::ceil(logical*scale - 0.5)); };
        const auto l = edge(13.4), r = edge(19.8), t = edge(4.2), b = edge(10.6);
        for (unsigned y=0; y<unsigned(20*scale); ++y) for(unsigned x=0;x<extent;++x)
            pixel(scaled_pixels,extent,x,y,x>=l && x<r && y>=t && y<b ? std::array<int,4>{255,0,0,255} : std::array<int,4>{0,0,0,255},"Fractional mirrored scissor");
        pixel(scaled_pixels,extent,unsigned(4*scale),unsigned(28*scale),{128,128,128,255},"Scale restoration/sRGB conversion");
        pixel(scaled_pixels,extent,unsigned(28*scale),unsigned(28*scale),{0,0,0,255},"Rounded border hole");
    }
    renderer.set_target({});
    check(has(renderer.submit({9,{32,32},1},{}),"invalid_target","/target"),"Null target accepted");
}

void glyph_fixtures(Host& host, bool batched = false) {
    const auto vertex = shader("primitive.vertex.spv"), fragment = shader("primitive.fragment.spv"), image = shader("primitive.image.spv");
    const auto batch = shader("primitive.batch.spv");
    tessera::VulkanContext context{host.physical,host.device,host.pass,VK_FORMAT_R8G8B8A8_SRGB,vertex,fragment,image,1,true};
    if (batched) context.batch_vertex_spirv = batch;
    tessera::VulkanRenderer renderer(context);
    tessera::PlaceholderTextShaper shaper;
    tessera::TextStyle style;
    style.size = 20; style.line_height = 24.0f;
    const auto shaped = shaper.shape("A a\nあ",style);
    check(bool(shaped),"Placeholder shaping failed");
    const tessera::DrawGlyphRun glyphs{{},*shaped.value,{1,1,1,1}};
    check(renderer.bind_image({1},host.texture.view,host.sampler).empty(),"Glyph fixture image binding failed");

    // Rejected glyphs must not record preceding commands, consume frames or pin images.
    host.resize({48,56}); host.begin(renderer);
    auto bad = glyphs; bad.run.font = {7};
    tessera::UiDrawList rejected{{tessera::DrawImage{{{0,0},{48,56}},{1}},bad}};
    check(has(renderer.submit({1,{48,56},1},rejected),"unsupported_font","/commands/1/run/font"),"Real font accepted by placeholder mode");
    bad = glyphs; bad.run.glyphs[0].id = 0xd800;
    check(has(renderer.submit({1,{48,56},1},{{bad}}),"invalid_glyph","/commands/0/run/glyphs/0/id"),"Surrogate glyph accepted");
    bad.run.glyphs[0].id = 0x110000;
    check(has(renderer.submit({1,{48,56},1},{{bad}}),"invalid_glyph","/commands/0/run/glyphs/0/id"),"Out-of-range scalar accepted");
    bad = glyphs;
    bad.origin.x = bad.run.glyphs[0].position.x = std::numeric_limits<float>::max();
    check(has(renderer.submit({1,{48,56},1},{{tessera::PushClip{{{0,0},{0,0}}},bad,tessera::PopClip{}}}),
        "geometry_overflow","/commands/1/run/glyphs/0/position"),"Clipped glyph overflow accepted");
    bad = glyphs; bad.run.glyphs[0].position.x = std::numeric_limits<float>::quiet_NaN();
    check(has(renderer.submit({1,{48,56},1},{{bad}}),"invalid_number","/commands/0/run/glyphs/0/position/x"),"Nonfinite glyph accepted");
    const auto black = host.finish("glyph-rejected.ppm");
    for (unsigned y=0;y<56;++y) for (unsigned x=0;x<48;++x)
        pixel(black,48,x,y,{0,0,0,255},"Glyph rejection recorded partial commands");
    check(renderer.unbind_image({1}).empty(),"Rejected glyph submission pinned an image");

    // Exhaustive small images prove baseline, multiline spacing, blank space, missing
    // marks, lowercase policy and device scale against independent A/box masks.
    std::uint64_t frame = 1;
    for (float scale : {1.0f,1.25f,1.5f,2.0f}) {
        const unsigned width = unsigned(48*scale), height = unsigned(56*scale);
        host.resize({width,height}); host.begin(renderer);
        check(renderer.submit({frame,{48,56},scale},{{glyphs}}).empty(),"Placeholder GPU submission failed");
        const std::string artifact = "glyph-scale-" + std::to_string(frame) + ".ppm";
        const auto pixels = host.finish(artifact.c_str()); renderer.retire(frame++);
        const std::array<unsigned,7> a{14,17,17,31,17,17,17}, box{31,17,17,17,17,17,31};
        for (unsigned y=0;y<height;++y) for (unsigned x=0;x<width;++x) {
            bool on = false, boundary = false;
            for (unsigned mark = 0; mark < 3; ++mark) {
                const double gx = mark == 1 ? 21 : 1, gy = mark == 2 ? 26 : 2;
                const double cx = ((x+0.5)/scale-gx)/1.6, cy = ((y+0.5)/scale-gy)/2;
                if (std::abs(cx-std::round(cx)) < 0.00001 || std::abs(cy-std::round(cy)) < 0.00001) boundary = true;
                if (cx >= 0 && cx < 5 && cy >= 0 && cy < 7)
                    on = on || (((mark == 2 ? box : a)[unsigned(cy)] & (1u << (4-unsigned(cx)))) != 0);
            }
            if (!boundary) pixel(pixels,width,x,y,on ? std::array<int,4>{255,255,255,255} : std::array<int,4>{0,0,0,255},"Placeholder bitmap/scale");
        }
    }

    // Glyphs use the same ordered blending, transform and clip path as primitives.
    auto single = shaper.shape("A",style);
    tessera::DrawGlyphRun red{{},*single.value,{1,0,0,1}};
    tessera::DrawGlyphRun blue = red; blue.color = {0,0,1,0.5f};
    const tessera::UiDrawList transformed{{
        red,blue,
        tessera::PushTransform{{-1,0,0,1,24,0}},tessera::PushClip{{{1,2},{4,14}}},
        tessera::DrawGlyphRun{{},*single.value,{0,1,0,1}},tessera::PopClip{},tessera::PopTransform{},
        tessera::PushTransform{{0,1,-1,0,48,4}},red,tessera::PopTransform{},
        tessera::PushClip{{{0,0},{0,0}}},red,tessera::PopClip{},
        tessera::DrawRect{{{0,40},{4,4}},{1,1,1,1}},
    }};
    host.resize({48,56}); host.begin(renderer);
    check(renderer.submit({frame,{48,56},1},transformed).empty(),"Transformed glyph submission failed");
    const auto transformed_pixels = host.finish("glyph-transforms.ppm"); renderer.retire(frame++);
    pixel(transformed_pixels,48,4,2,{188,0,188,255},"Ordered glyph alpha");
    pixel(transformed_pixels,48,1,2,{0,0,0,255},"Glyph transparent cell");
    pixel(transformed_pixels,48,20,2,{0,255,0,255},"Mirrored glyph and clip");
    pixel(transformed_pixels,48,17,4,{0,0,0,255},"Mirrored glyph clip exterior");
    pixel(transformed_pixels,48,45,8,{255,0,0,255},"Rotated glyph");
    pixel(transformed_pixels,48,1,41,{255,255,255,255},"Clip/transform restoration after glyphs");

    // Full JSON -> tree -> layout -> paint -> GPU, retaining backend-neutral Text.
    const auto document = tessera::load_document(R"({"version":1,"root":{"type":"Box","id":"menu","children":[{"type":"Text","id":"start","properties":{"text":"Start"}},{"type":"Text","id":"quit","properties":{"text":"Quit"}}]}})");
    check(bool(document),"GPU menu JSON rejected");
    const auto tree = tessera::UiTree::create(*document.value);
    check(bool(tree),"GPU menu tree rejected");
    std::vector<tessera::ResolvedStyle> styles((*tree.value)->size());
    styles[0].padding = {4,4,4,4}; styles[0].gap = 4;
    for (std::size_t i=1;i<styles.size();++i) { styles[i].text = style; styles[i].color = {1,1,1,1}; }
    const auto layout = tessera::compute_layout({tree.value->get(),styles,{96,64},&shaper});
    check(bool(layout),"GPU menu layout rejected");
    const auto paint = tessera::build_paint_list({tree.value->get(),styles,&*layout.value,&shaper});
    check(bool(paint),"GPU menu paint rejected");
    const auto repeated = tessera::build_paint_list({tree.value->get(),styles,&*layout.value,&shaper});
    check(repeated && *paint.value == *repeated.value,"GPU menu paint is nondeterministic");
    host.resize({96,64}); host.begin(renderer);
    check(renderer.submit({frame,{96,64},1},*paint.value).empty(),"Full document Text paint rejected by Vulkan");
    const auto menu = host.finish("placeholder-menu.ppm"); renderer.retire(frame++);
    pixel(menu,96,8,6,{255,255,255,255},"Menu Start first row");
    pixel(menu,96,8,34,{255,255,255,255},"Menu Quit first row");
    pixel(menu,96,0,0,{0,0,0,255},"Menu padding");
    host.begin(renderer);
    const auto empty = shaper.shape("",style), spaces = shaper.shape("  ",style);
    check(renderer.submit({frame,{96,64},1},{{tessera::DrawGlyphRun{{},*empty.value,{1,1,1,1}},
        tessera::DrawGlyphRun{{},*spaces.value,{1,1,1,1}}}}).empty(),"Blank placeholder runs rejected");
    const auto blank = host.finish("glyph-blank.ppm"); renderer.retire(frame);
    for (unsigned y=0;y<64;++y) for (unsigned x=0;x<96;++x) pixel(blank,96,x,y,{0,0,0,255},"Blank runs drew pixels");
}
#ifdef TESSERA_REAL_GLYPH_FIXTURES
std::vector<std::byte> font_bytes(const char* name) {
    std::ifstream file(std::filesystem::path(TESSERA_FONT_DIR)/name, std::ios::binary | std::ios::ate);
    check(bool(file), "Cannot read fixture font");
    std::vector<std::byte> bytes(static_cast<std::size_t>(file.tellg()));
    file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(bool(file), "Cannot read fixture font bytes"); return bytes;
}
// Small independent image oracle: sample original rasters at unsnapped baselines,
// never read atlas coordinates or lowered DrawImages. Only fixture translations/clips.
std::vector<std::uint8_t> glyph_reference(const tessera::UiDrawList& list, tessera::GlyphCache& cache,
    float scale, VkExtent2D extent, std::uint32_t subpixel_bits) {
    struct State { double tx=0,ty=0,left=0,top=0,right=0,bottom=0; };
    State state{0,0,0,0,double(extent.width),double(extent.height)};
    std::vector<State> stack;
    std::vector<std::array<double,3>> colors(std::size_t(extent.width)*extent.height, {0,0,0});
    const auto linear = [](double c) { return c <= 0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); };
    const double precision=std::ldexp(1.0,int(subpixel_bits));
    for (const auto& command : list.commands) {
        if (const auto* transform = std::get_if<tessera::PushTransform>(&command)) {
            check(transform->transform.a==1 && transform->transform.d==1 && transform->transform.b==0 && transform->transform.c==0,
                "Reference fixture only supports translations");
            stack.push_back(state); state.tx += transform->transform.tx; state.ty += transform->transform.ty;
        } else if (const auto* clip = std::get_if<tessera::PushClip>(&command)) {
            stack.push_back(state);
            state.left = std::max(state.left, (clip->rect.origin.x+state.tx)*scale);
            state.top = std::max(state.top, (clip->rect.origin.y+state.ty)*scale);
            state.right = std::min(state.right, (double(clip->rect.origin.x)+clip->rect.size.width+state.tx)*scale);
            state.bottom = std::min(state.bottom, (double(clip->rect.origin.y)+clip->rect.size.height+state.ty)*scale);
        } else if (std::holds_alternative<tessera::PopClip>(command) || std::holds_alternative<tessera::PopTransform>(command)) {
            state=stack.back(); stack.pop_back();
        } else if (const auto* run = std::get_if<tessera::DrawGlyphRun>(&command)) {
            for (const auto& glyph : run->run.glyphs) {
                const auto raster = cache.get({glyph.font,glyph.id,std::uint32_t(std::round(double(run->run.size)*scale*64))});
                check(bool(raster), "Reference glyph raster failed"); const auto& bitmap = **raster.value;
                if (!bitmap.width) continue;
                const double left=(double(run->origin.x)+glyph.position.x+state.tx)*scale+bitmap.left;
                const double top=(double(run->origin.y)+glyph.position.y+state.ty)*scale-bitmap.top;
                const auto coverage = [&](int x,int y) {
                    return x<0 || y<0 || x>=int(bitmap.width) || y>=int(bitmap.height) ? 0.0 : double(bitmap.coverage[std::size_t(y)*bitmap.width+x])/255;
                };
                for(unsigned y=0;y<extent.height;++y) for(unsigned x=0;x<extent.width;++x) {
                    const double px=x+0.5,py=y+0.5;
                    // Quad coverage uses the declared device rasterizer precision;
                    // texture interpolation retains the continuous baseline phase.
                    const auto edge=[&](double v) { return std::round(v*precision)/precision; };
                    if(px<state.left || px>=state.right || py<state.top || py>=state.bottom ||
                        px<edge(left) || px>=edge(left+bitmap.width) || py<edge(top) || py>=edge(top+bitmap.height)) continue;
                    const double sx=px-left-0.5,sy=py-top-0.5;
                    const int ix=int(std::floor(sx)),iy=int(std::floor(sy));
                    const double fx=sx-ix,fy=sy-iy;
                    const double alpha=((1-fy)*((1-fx)*coverage(ix,iy)+fx*coverage(ix+1,iy))+
                        fy*((1-fx)*coverage(ix,iy+1)+fx*coverage(ix+1,iy+1)))*run->color.a;
                    auto& destination=colors[std::size_t(y)*extent.width+x];
                    const std::array<double,3> tint{linear(run->color.r),linear(run->color.g),linear(run->color.b)};
                    for(unsigned c=0;c<3;++c) destination[c]=tint[c]*alpha+destination[c]*(1-alpha);
                }
            }
        } else check(false, "Unexpected command in glyph reference fixture");
    }
    std::vector<std::uint8_t> pixels(colors.size()*4,255);
    for(std::size_t i=0;i<colors.size();++i) for(unsigned c=0;c<3;++c) {
        const auto v=colors[i][c];
        pixels[i*4+c]=std::uint8_t(std::round(255*(v<=0.0031308 ? 12.92*v : 1.055*std::pow(v,1/2.4)-0.055)));
    }
    return pixels;
}
void real_glyph_fixtures(Host& host) {
    tessera::FontShaper shaper;
    const auto latin=font_bytes("NotoSans-Regular.ttf");
    check(shaper.set_face({0},latin).empty() && shaper.set_face({1},font_bytes("NotoSansJP-Regular.otf")).empty(), "Fixture fonts rejected");
    check(shaper.set_fallback({0},{{1}}).empty(), "Fixture fallback rejected");
    tessera::GlyphCache cache(shaper);
    tessera::UiDocument document; document.root.kind=tessera::NodeKind::text;
    document.root.properties.emplace("text",std::string("Start スタート"));
    const auto tree=tessera::UiTree::create(document); check(bool(tree), "Real Text tree failed");
    tessera::ResolvedStyle style; style.text.size=20; style.padding={4,4,4,4}; style.color={0.6f,0.85f,1,0.75f};
    const std::vector<tessera::ResolvedStyle> styles{style};
    const auto layout=tessera::compute_layout({tree.value->get(),styles,{78.1f,100},&shaper});
    check(bool(layout), "Real Text layout failed");
    const auto paint=tessera::build_paint_list({tree.value->get(),styles,&*layout.value,&shaper});
    check(bool(paint), "Real Text paint failed");
    const auto mixed=shaper.shape("Start スタート",style.text,{70.1f});
    check(mixed && mixed.value->metrics.lines==2 && mixed.value->glyphs[6].font.value==1, "Mixed wrapped face/metrics differ");
    const auto marks=shaper.shape("j ffi a\xcc\x81",style.text);
    check(bool(marks), "Ligature/mark shaping failed");
    tessera::UiDrawList source{{tessera::PushTransform{{1,0,0,1,0.2f,0.3f}}, tessera::PushClip{{{7.3f,6.1f},{61.2f,82.2f}}}}};
    source.commands.insert(source.commands.end(),paint.value->commands.begin(),paint.value->commands.end());
    source.commands.push_back(tessera::DrawGlyphRun{{5,64},*marks.value,{1,0.7f,0.3f,0.65f}});
    source.commands.push_back(tessera::PopClip{}); source.commands.push_back(tessera::PopTransform{});
    check(tessera::validate(source).empty(), "Real glyph source invalid");
    const auto vertex=shader("primitive.vertex.spv"),fragment=shader("primitive.fragment.spv"),image=shader("primitive.image.spv"),batch=shader("primitive.batch.spv");
    // Dedicated linear sampler: coverage lives in alpha, never in encoded RGB.
    VkSamplerCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter=info.minFilter=VK_FILTER_LINEAR; info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU=info.addressModeV=info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler; require(vkCreateSampler(host.device,&info,nullptr,&sampler), "create glyph sampler");
    struct SamplerOwner { VkDevice device; VkSampler sampler; ~SamplerOwner(){vkDestroySampler(device,sampler,nullptr);} } sampler_owner{host.device,sampler};
    VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(host.physical,&properties);
    std::cout << "Glyph rasterizer subpixel bits=" << properties.limits.subPixelPrecisionBits << '\n';
    for(float scale : {1.0f,1.25f,1.5f,2.0f}) {
        host.resize({unsigned(100*scale),unsigned(100*scale)});
        const auto expected=glyph_reference(source,cache,scale,host.extent,properties.limits.subPixelPrecisionBits);
        std::vector<std::uint8_t> reference;
        for(bool batched : {false,true}) {
            tessera::VulkanContext context{host.physical,host.device,host.pass,VK_FORMAT_R8G8B8A8_SRGB,vertex,fragment,image,16};
            if(batched) context.batch_vertex_spirv=batch;
            tessera::VulkanRenderer renderer(context);
            auto prepared=tessera::prepare_glyph_atlas(source,cache,scale,{100},{batched ? 128u : 64u,16,4096});
            check(prepared && prepared.diagnostics.empty(), "Real atlas preparation failed");
            host.upload_atlas(*prepared.value);
            std::vector<tessera::ImageHandle> handles;
            for(std::size_t i=0;i<prepared.value->pages.size();++i) {
                handles.push_back(prepared.value->pages[i].image);
                check(renderer.bind_image(handles.back(),host.atlas_images[i].view,sampler).empty(), "Atlas bind failed");
            }
            // GPU copies survive CPU page/cache destruction and face replacement.
            prepared.value->pages.clear(); cache.clear();
            check(shaper.set_face({0},latin).empty(), "Face replacement failed");
            host.begin(renderer);
            check(renderer.submit({1,{100,100},scale},prepared.value->draw_list).empty(), "Real glyph image submission failed");
            for(const auto handle : handles) {
                check(has(renderer.unbind_image(handle),"resource_in_use","/image"), "Live atlas unbound");
                check(has(renderer.bind_image(handle,host.texture.view,host.sampler),"resource_in_use","/image"), "Live atlas replaced");
            }
            renderer.retire(0);
            const auto actual=host.finish(("real-glyph-"+std::string(batched ? "batch-" : "reference-")+std::to_string(int(scale*4))+".ppm").c_str());
            double max_linear_error=0;
            unsigned max_encoded_error=0;
            const auto decode=[](std::uint8_t v) { const double c=double(v)/255; return c<=0.04045 ? c/12.92 : std::pow((c+0.055)/1.055,2.4); };
            for(std::size_t i=0;i<actual.size();++i) {
                max_encoded_error=std::max(max_encoded_error,unsigned(std::abs(int(actual[i])-int(expected[i]))));
                max_linear_error=std::max(max_linear_error,std::abs(decode(actual[i])-decode(expected[i])));
                if(batched) check(std::abs(int(actual[i])-int(reference[i]))<=2,"Atlas placement/batch altered glyph pixels");
            }
            std::cout << "Real glyph scale=" << scale << " batch=" << batched << " max linear error=" << max_linear_error << " max encoded error=" << max_encoded_error << '\n';
            check(max_linear_error<=2.0/255,"Real glyph image differs from independent bilinear raster sampling (linear tolerance 2/255)");
            if(!batched) reference=actual;
            renderer.retire(1);
            check(renderer.pending_uploads()==0, "Glyph batch upload retained after completion");
            for(const auto handle : handles) check(renderer.unbind_image(handle).empty(), "Atlas retirement failed");
            host.clear_atlas();
        }
    }
}
#endif
void batch_fixtures(Host& host) {
    const auto vertex = shader("primitive.vertex.spv"), fragment = shader("primitive.fragment.spv"), image = shader("primitive.image.spv");
    const auto batch = shader("primitive.batch.spv");
    tessera::VulkanContext context{host.physical,host.device,host.pass,VK_FORMAT_R8G8B8A8_SRGB,vertex,fragment,image,2,true};
    tessera::VulkanRenderer reference(context);
    context.batch_vertex_spirv = batch;
    tessera::VulkanRenderer renderer(context);
    for (auto* current : {&reference, &renderer}) {
        check(current->bind_image({1},host.texture.view,host.sampler).empty(),"Batch image binding failed");
        check(current->bind_image({2},host.texture.view,host.sampler).empty(),"Second batch image binding failed");
    }
    tessera::PlaceholderTextShaper shaper;
    tessera::TextStyle style; style.size = 20;
    const auto run = shaper.shape("Aa",style); check(bool(run),"Batch glyph shaping failed");
    const tessera::UiDrawList list{{
        tessera::DrawRect{{{0,0},{64,64}},{1,0,0,1}},
        tessera::DrawRect{{{8,8},{48,48}},{0,0,1,0.5f},4},
        tessera::DrawImage{{{12,12},{32,32}},{1}},
        tessera::DrawImage{{{16,16},{32,32}},{1},{{0.5f,0},{0.5f,1}},{1,1,1,0.5f}},
        tessera::DrawImage{{{24,24},{32,32}},{2}},
        tessera::DrawRect{{{4,4},{32,32}},{0,1,0,0.25f}},
        tessera::PushTransform{{0,1,-1,0,48,4}},
        tessera::DrawGlyphRun{{},*run.value,{1,1,1,0.5f}}, tessera::PopTransform{},
        tessera::PushTransform{{-1,0,0,1,64,0}},
        tessera::PushClip{{{5.2f,5.2f},{30.4f,30.4f}}},
        tessera::DrawRect{{{0,0},{64,64}},{1,1,0,0.25f}},
        tessera::DrawBorder{{{4,4},{40,40}},{2,3,4,1},{0,1,1,0.5f},5},
        tessera::PopClip{}, tessera::PopTransform{},
        tessera::DrawImage{{{0,40},{24,24}},{1}},
        tessera::DrawRect{{{8,48},{16,16}},{1,1,1,0.25f}},
    }};
    const tessera::UiDrawList overlay{{tessera::DrawRect{{{0,0},{64,64}},{0,0,1,0.125f}}}};
    std::uint64_t frame = 1;
    for (float scale : {1.0f,1.25f,1.5f,2.0f}) {
        host.resize({unsigned(64*scale),unsigned(64*scale)});
        host.begin(reference);
        check(reference.submit({frame,{64,64},scale},list).empty(),"Batch reference submission failed");
        check(reference.submission_stats().primitives == 12 && reference.submission_stats().draw_calls == 12 &&
            reference.submission_stats().upload_bytes == 0 && reference.pending_uploads() == 0,"Reference counters disagree");
        check(reference.submit({frame+1,{64,64},scale},overlay).empty(),"Reference overlay failed");
        const auto expected = host.finish(("batch-reference-" + std::to_string(frame) + ".ppm").c_str());
        reference.retire(frame+1);

        host.begin(renderer);
        check(renderer.submit({frame,{64,64},scale},list).empty(),"Adjacent batch submission failed");
        const auto stats = renderer.submission_stats();
        check(stats.primitives == 12 && stats.draw_calls == 7 && stats.upload_bytes == 12*112,
            "Adjacent batches must split at scissor, pipeline and image changes without sorting");
        check(renderer.pending_uploads() == 1,"Batch upload was not retained");
        const tessera::UiDrawList bad{{tessera::DrawRect{{{0,0},{64,64}},{1,1,1,1}},
            tessera::DrawImage{{{0,0},{64,64}},{3}}}};
        check(has(renderer.submit({frame+1,{64,64},scale},bad),"invalid_handle","/commands/1/image"),"Invalid batched image accepted");
        check(renderer.pending_uploads() == 1 && renderer.submission_stats().draw_calls == stats.draw_calls &&
            renderer.submission_stats().upload_bytes == stats.upload_bytes,"Rejected batch changed uploads/counters");
        check(renderer.submit({frame+1,{64,64},scale},overlay).empty(),"Rejected batch consumed frame number");
        check(renderer.pending_uploads() == 2,"Multiple submissions reused in-flight upload memory");
        check(has(renderer.unbind_image({2}),"resource_in_use","/image"),"Batch image unbound before completion");
        renderer.retire(frame-1);
        check(renderer.pending_uploads() == 2,"Early retirement freed live uploads");
        const auto actual = host.finish(("batch-adjacent-" + std::to_string(frame) + ".ppm").c_str());
        for (std::size_t i=0; i<actual.size(); ++i)
            check(std::abs(int(actual[i])-int(expected[i])) <= 2,"Batched pixels differ from ordered reference");
        renderer.retire(frame);
        check(renderer.pending_uploads() == 1,"Partial retirement did not free only the completed upload");
        renderer.retire(UINT64_MAX);
        check(renderer.pending_uploads() == 0,"Completed batch uploads leaked");
        check(renderer.bind_image({2},host.texture.view,host.sampler).empty(),"Retired batch image cannot be replaced");
        frame += 2;
    }
    host.begin(renderer);
    check(renderer.submit({frame,{64,64},2},{}).empty(),"Empty batched submission failed");
    check(renderer.pending_uploads() == 0 && renderer.submission_stats().draw_calls == 0 &&
        renderer.submission_stats().upload_bytes == 0,"Empty batch allocated upload memory");
    host.finish("batch-empty.ppm"); renderer.retire(frame);
}
}
int main() {
    try {
        { Host host; host.initialize(); host.make_texture();
          fixtures(host); glyph_fixtures(host); fixtures(host,true); glyph_fixtures(host,true); batch_fixtures(host);
#ifdef TESSERA_REAL_GLYPH_FIXTURES
          real_glyph_fixtures(host);
#endif
        }
        check(validation_errors == 0,"Vulkan validation errors occurred");
        std::cout << "Vulkan GPU primitive, image, placeholder Text/menu, scale, rejection and retirement fixtures passed (RGBA8 sRGB, tolerance 2/255).\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
