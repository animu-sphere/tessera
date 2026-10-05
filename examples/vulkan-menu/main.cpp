// Win32 + Vulkan native menu host. The host owns the window, OS input normalization, DPI, the
// Vulkan instance/device/queue/surface/swapchain, synchronization, presentation and retirement.
// Tessera receives logical pointer events, resolved styles and a recording command buffer only.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <tessera/vulkan/renderer.hpp>
#include <tessera/input/pointer.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/ui/serialization.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t frames_in_flight = 2;
constexpr tessera::PointerId mouse{1};
constexpr float base_dpi = 96;
constexpr const char* menu_json = R"({"version":1,"root":{"type":"Box","id":"screen","children":[
{"type":"Box","id":"panel","children":[
{"type":"Text","id":"title","properties":{"text":"Tessera"}},
{"type":"Box","id":"start","events":{"activate":"start-game"},"children":[{"type":"Text","id":"start-label","properties":{"text":"Start"}}]},
{"type":"Box","id":"quit","events":{"activate":"quit-game"},"children":[{"type":"Text","id":"quit-label","properties":{"text":"Quit"}}]}]}]}})";

void require(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(result));
}
void print(const std::vector<tessera::Diagnostic>& diagnostics) {
    for (const auto& d : diagnostics) std::cerr << d.code << " at '" << d.path << "': " << d.message << '\n';
}
template<class T> T take(tessera::Result<T> result, const char* operation) {
    if (!result) { print(result.diagnostics); throw std::runtime_error(operation); }
    return std::move(*result.value);
}
constexpr tessera::Color rgb(int r, int g, int b) { return {r/255.0f, g/255.0f, b/255.0f, 1}; }
std::vector<std::uint32_t> shader(const char* name) {
    std::ifstream file(std::filesystem::path(TESSERA_SHADER_DIR) / name, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error(std::string("Cannot open compiled shader ") + name);
    const auto bytes = file.tellg();
    if (bytes <= 0 || bytes % 4) throw std::runtime_error(std::string("Malformed shader artifact ") + name);
    std::vector<std::uint32_t> code(std::size_t(bytes)/4);
    file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), bytes);
    return code;
}

std::atomic<unsigned> validation_errors = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++validation_errors;
    std::cerr << "Vulkan validation: " << data->pMessage << '\n';
    return VK_FALSE;
}

// Monotonic host clock for InputEvent timestamps.
std::chrono::microseconds now() {
    static const LONGLONG frequency = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f.QuadPart; }();
    LARGE_INTEGER counter; QueryPerformanceCounter(&counter);
    return std::chrono::microseconds(counter.QuadPart / frequency * 1'000'000 + counter.QuadPart % frequency * 1'000'000 / frequency);
}
// Largest float logical length whose ceil(length * scale) equals the physical extent, as the
// renderer computes it in double. Exact division is not representable for every scale.
float logical_length(std::uint32_t pixels, float scale) {
    float value = float(double(pixels) / scale);
    while (std::ceil(double(value) * scale) > pixels) value = std::nextafter(value, 0.0f);
    while (std::ceil(double(value) * scale) < pixels) value = std::nextafter(value, std::numeric_limits<float>::infinity());
    return value;
}
// Client pixel (x, y) covers [x, x+1) x [y, y+1); its center is the rasterizer's coverage sample,
// so a pixel painted by a half-open border box is also a hit on that box.
tessera::Point logical_point(LPARAM lparam, float scale) {
    return {float((GET_X_LPARAM(lparam) + 0.5) / scale), float((GET_Y_LPARAM(lparam) + 0.5) / scale)};
}
tessera::Modifiers modifiers(WPARAM wparam) {
    return {(wparam & MK_SHIFT) != 0, (wparam & MK_CONTROL) != 0, GetKeyState(VK_MENU) < 0,
            GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0};
}

// Backend-neutral document state: JSON -> tree -> styles -> layout -> paint.
struct Menu {
    static constexpr tessera::Color screen = rgb(18,20,26), panel = rgb(34,39,52), label = rgb(255,255,255);
    static constexpr tessera::Color idle = rgb(47,74,128), hover = rgb(66,104,176), pressed = rgb(32,50,88);
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<std::uint32_t> parents;
    std::vector<tessera::ResolvedStyle> styles;
    tessera::PlaceholderTextShaper text;
    tessera::LayoutResult layout;
    tessera::UiDrawList paint;
    tessera::Size viewport;

    Menu() {
        const tessera::ValidationContext actions{{"start-game", "quit-game"}};
        tree = take(tessera::UiTree::create(take(tessera::load_document(menu_json, actions), "Menu JSON rejected"), actions), "Menu tree rejected");
        parents.assign(tree->size(), 0);
        std::vector<tessera::NodeHandle> pending{tree->root()};
        while (!pending.empty()) {
            const auto node = pending.back(); pending.pop_back();
            for (const auto child : tree->children(node)) { parents[child.index] = node.index; pending.push_back(child); }
        }
        styles.resize(tree->size());
        auto& root = style("screen");
        root.justify = tessera::Justify::center; root.align = tessera::Align::center; root.background = screen;
        auto& box = style("panel");
        box.width = tessera::Dimension::points(240); box.padding = {20,20,20,20}; box.gap = 12;
        box.border = {1,1,1,1}; box.border_color = rgb(70,82,105); box.corner_radius = 10; box.background = panel;
        auto& title = style("title");
        title.text.size = 28; title.color = rgb(235,238,245);
        for (const char* id : {"start", "quit"}) {
            auto& button = style(id);
            button.padding = {10,16,10,16}; button.border = {1,1,1,1}; button.border_color = rgb(90,120,180);
            button.corner_radius = 6; button.align = tessera::Align::center; button.background = idle;
            auto& caption = style((std::string(id) + "-label").c_str());
            caption.text.size = 20; caption.color = label;
        }
    }
    tessera::ResolvedStyle& style(const char* id) { return styles[tree->find(id)->index]; }
    const tessera::LayoutBox& box(const char* id) const {
        const auto node = *tree->find(id);
        for (const auto& b : layout.boxes) if (b.node == node) return b;
        throw std::logic_error("Displayed box missing");
    }
    tessera::HitTestInput snapshot() const { return {tree.get(), styles, &layout}; }
    void resize(tessera::Size size) {
        viewport = size;
        layout = take(tessera::compute_layout({tree.get(), styles, viewport, &text}), "Menu layout rejected");
        repaint();
    }
    void repaint() { paint = take(tessera::build_paint_list({tree.get(), styles, &layout, &text}), "Menu paint rejected"); }
    // Host-side visual policy until stylesheet pseudo states exist: background only, so the
    // existing layout stays coherent with the styles.
    void show(std::optional<tessera::NodeHandle> hovered, std::optional<tessera::NodeHandle> active) {
        bool changed = false;
        for (const char* id : {"start", "quit"}) {
            const auto node = *tree->find(id);
            const auto color = active == node ? pressed : hovered == node ? hover : idle;
            if (styles[node.index].background != color) { styles[node.index].background = color; changed = true; }
        }
        if (changed) repaint();
    }
    std::optional<tessera::NodeHandle> button_of(std::optional<tessera::NodeHandle> node) const {
        while (node) {
            if (tree->get(*node)->events.contains("activate")) return node;
            if (node->index == 0) return std::nullopt;
            node = tessera::NodeHandle{node->tree, parents[node->index]};
        }
        return std::nullopt;
    }
};

struct SwapchainImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkSemaphore rendered = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE; // Borrowed from the frame slot last rendering this image.
    std::uint64_t frame = 0;
};
struct FrameSlot {
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::uint64_t frame = 0; // Last successful renderer frame covered by this slot's fence.
};

struct App {
    bool smoke = false;
    HWND window = nullptr;
    float scale = 1;
    bool minimized = false, resizing = false, swapchain_dirty = true, quit = false;
    Menu menu;
    tessera::PointerDispatcher dispatcher;
    std::vector<tessera::PointerState> pointers;
    std::vector<std::string> actions;
    unsigned buttons = 0;
    bool releasing_capture = false, tracking_leave = false;
    std::string failure;
    unsigned submit_errors = 0, input_errors = 0;

    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkSurfaceFormatKHR format{};
    VkRenderPass present_pass = VK_NULL_HANDLE, capture_pass = VK_NULL_HANDLE;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::array<FrameSlot, frames_in_flight> slots{};
    std::uint32_t slot_index = 0;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{};
    std::vector<SwapchainImage> images;
    std::unique_ptr<tessera::VulkanRenderer> renderer;
    std::uint64_t last_frame = 0, presented = 0;

    // Smoke-only presentation readback.
    VkBuffer readback = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory = VK_NULL_HANDLE;
    VkDeviceSize readback_size = 0;
    bool capture_next = false;
    unsigned captures = 0;
    unsigned step = 0;
    std::uint64_t step_ready = 1;

    void fail(std::string message) {
        if (failure.empty()) failure = std::move(message);
        quit = true;
    }

    // ---- Vulkan ownership ----------------------------------------------------------------
    void initialize_vulkan() {
        std::uint32_t count = 0;
        require(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate layers");
        std::vector<VkLayerProperties> layers(count);
        require(vkEnumerateInstanceLayerProperties(&count, layers.data()), "enumerate layers");
        bool validation = false;
        for (const auto& layer : layers) if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) validation = true;
        if (smoke && !validation) throw std::runtime_error("The smoke host requires VK_LAYER_KHRONOS_validation");
        if (!validation) std::cout << "Khronos validation layer not found; running without validation.\n";
        const char* layer = "VK_LAYER_KHRONOS_validation";
        std::vector<const char*> extensions{VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        const VkValidationFeatureEnableEXT feature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT features{}; features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.enabledValidationFeatureCount = 1; features.pEnabledValidationFeatures = &feature;
        VkDebugUtilsMessengerCreateInfoEXT debug_info{}; debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        debug_info.pfnUserCallback = debug; debug_info.pNext = &features;
        if (validation) { extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME); }
        VkApplicationInfo app{}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion = VK_API_VERSION_1_1;
        app.pApplicationName = "tessera_vulkan_menu";
        VkInstanceCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo = &app;
        info.enabledExtensionCount = std::uint32_t(extensions.size()); info.ppEnabledExtensionNames = extensions.data();
        if (validation) { info.enabledLayerCount = 1; info.ppEnabledLayerNames = &layer; info.pNext = &debug_info; }
        require(vkCreateInstance(&info, nullptr, &instance), "create instance");
        if (validation) {
            debug_info.pNext = nullptr;
            auto create_debug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            require(create_debug(instance, &debug_info, nullptr, &messenger), "create debug messenger");
        }
        VkWin32SurfaceCreateInfoKHR surface_info{}; surface_info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surface_info.hinstance = GetModuleHandleW(nullptr); surface_info.hwnd = window;
        require(vkCreateWin32SurfaceKHR(instance, &surface_info, nullptr, &surface), "create Win32 surface");
        select_device();
        create_device();
    }
    void select_device() {
        std::uint32_t count = 0;
        require(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate GPUs");
        std::vector<VkPhysicalDevice> devices(count);
        require(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate GPUs");
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(candidate, &properties);
            if (properties.apiVersion < VK_API_VERSION_1_1) continue;
            require(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, nullptr), "enumerate device extensions");
            std::vector<VkExtensionProperties> extensions(count);
            require(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &count, extensions.data()), "enumerate device extensions");
            if (std::none_of(extensions.begin(), extensions.end(), [](const auto& e) { return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0; })) continue;
            // One graphics family that also presents; separate present queues are not handled.
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> families(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
            std::optional<std::uint32_t> chosen;
            for (std::uint32_t i = 0; i < count && !chosen; ++i) {
                VkBool32 present = VK_FALSE;
                require(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present), "query present support");
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) chosen = i;
            }
            if (!chosen) continue;
            require(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &count, nullptr), "query surface formats");
            std::vector<VkSurfaceFormatKHR> formats(count);
            require(vkGetPhysicalDeviceSurfaceFormatsKHR(candidate, surface, &count, formats.data()), "query surface formats");
            std::optional<VkSurfaceFormatKHR> selected;
            for (const VkFormat wanted : {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB}) {
                VkFormatProperties support;
                vkGetPhysicalDeviceFormatProperties(candidate, wanted, &support);
                constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
                if ((support.optimalTilingFeatures & needed) != needed) continue;
                for (const auto& f : formats)
                    if (!selected && f.format == wanted && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) selected = f;
            }
            if (!selected) continue;
            VkSurfaceCapabilitiesKHR capabilities;
            require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(candidate, surface, &capabilities), "query surface capabilities");
            if (smoke && !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) continue;
            physical = candidate; family = *chosen; format = *selected;
            std::cout << "GPU: " << properties.deviceName << ", vendor=0x" << std::hex << properties.vendorID << ", device=0x" << properties.deviceID
                << std::dec << ", driver=" << properties.driverVersion << ", Vulkan=" << VK_VERSION_MAJOR(properties.apiVersion) << '.'
                << VK_VERSION_MINOR(properties.apiVersion) << '.' << VK_VERSION_PATCH(properties.apiVersion)
                << ", swapchain format=" << (format.format == VK_FORMAT_B8G8R8A8_SRGB ? "B8G8R8A8_SRGB" : "R8G8B8A8_SRGB") << '\n';
            return;
        }
        throw std::runtime_error("No Vulkan 1.1 device presents an sRGB swapchain to this window from a graphics queue");
    }
    VkRenderPass make_pass(VkImageLayout final_layout) {
        VkAttachmentDescription attachment{};
        attachment.format = format.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = final_layout;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{}; subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &reference;
        // The acquire semaphore is waited at color output, so the load/clear is ordered after it.
        // Compatible passes may differ only in layouts/load/store, so both carry the transfer
        // dependency used by capture; presentation is ordered by the rendered semaphore.
        VkSubpassDependency dependencies[2]{};
        dependencies[0] = {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0};
        dependencies[1] = {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0};
        VkRenderPassCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1; info.pAttachments = &attachment; info.subpassCount = 1; info.pSubpasses = &subpass;
        info.dependencyCount = 2; info.pDependencies = dependencies;
        VkRenderPass pass;
        require(vkCreateRenderPass(device, &info, nullptr, &pass), "create render pass");
        return pass;
    }
    void create_device() {
        const float priority = 1;
        VkDeviceQueueCreateInfo queue_info{}; queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info.queueFamilyIndex = family; queue_info.queueCount = 1; queue_info.pQueuePriorities = &priority;
        const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = 1; info.pQueueCreateInfos = &queue_info;
        info.enabledExtensionCount = 1; info.ppEnabledExtensionNames = &extension;
        require(vkCreateDevice(physical, &info, nullptr, &device), "create device");
        vkGetDeviceQueue(device, family, 0, &queue);
        // Render-pass compatibility ignores final layouts, so one renderer serves both passes.
        present_pass = make_pass(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        capture_pass = make_pass(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkCommandPoolCreateInfo pool_info{}; pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.queueFamilyIndex = family; pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        require(vkCreateCommandPool(device, &pool_info, nullptr, &pool), "create command pool");
        for (auto& slot : slots) {
            VkCommandBufferAllocateInfo command_info{}; command_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            command_info.commandPool = pool; command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; command_info.commandBufferCount = 1;
            require(vkAllocateCommandBuffers(device, &command_info, &slot.commands), "allocate command buffer");
            VkSemaphoreCreateInfo semaphore{}; semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            require(vkCreateSemaphore(device, &semaphore, nullptr, &slot.acquired), "create semaphore");
            VkFenceCreateInfo fence{}; fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO; fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            require(vkCreateFence(device, &fence, nullptr, &slot.fence), "create fence");
        }
        const auto vertex = shader("primitive.vertex.spv"), fragment = shader("primitive.fragment.spv"), image = shader("primitive.image.spv");
        const auto batch = shader("primitive.batch.spv");
        // Explicit placeholder opt-in: every glyph run comes from PlaceholderTextShaper with FontId 0.
        renderer = std::make_unique<tessera::VulkanRenderer>(tessera::VulkanContext{
            physical, device, present_pass, format.format, vertex, fragment, image, 1, true, batch});
    }
    void destroy_images() {
        for (auto& image : images) {
            if (image.framebuffer) vkDestroyFramebuffer(device, image.framebuffer, nullptr);
            if (image.view) vkDestroyImageView(device, image.view, nullptr);
            if (image.rendered) vkDestroySemaphore(device, image.rendered, nullptr);
        }
        images.clear();
    }
    // Resize/DPI update point: idle the queue, retire, recreate, then lay out at the new size.
    void recreate_swapchain() {
        require(vkDeviceWaitIdle(device), "wait idle before swapchain recreation");
        renderer->retire(last_frame);
        VkSurfaceCapabilitiesKHR capabilities;
        require(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &capabilities), "query surface capabilities");
        VkExtent2D size = capabilities.currentExtent;
        if (size.width == UINT32_MAX) {
            RECT client; GetClientRect(window, &client);
            size = {std::clamp(std::uint32_t(client.right), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
                    std::clamp(std::uint32_t(client.bottom), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)};
        }
        if (!size.width || !size.height) { minimized = true; return; }
        destroy_images();
        std::uint32_t count = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount) count = std::min(count, capabilities.maxImageCount);
        VkSwapchainCreateInfoKHR info{}; info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface; info.minImageCount = count; info.imageFormat = format.format; info.imageColorSpace = format.colorSpace;
        info.imageExtent = size; info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | (smoke ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; info.preTransform = capabilities.currentTransform;
        info.compositeAlpha = (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            : VkCompositeAlphaFlagBitsKHR(capabilities.supportedCompositeAlpha & ~(capabilities.supportedCompositeAlpha - 1));
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR; info.clipped = VK_TRUE; info.oldSwapchain = swapchain;
        VkSwapchainKHR replacement;
        require(vkCreateSwapchainKHR(device, &info, nullptr, &replacement), "create swapchain");
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = replacement; extent = size;
        require(vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr), "get swapchain images");
        std::vector<VkImage> handles(count);
        require(vkGetSwapchainImagesKHR(device, swapchain, &count, handles.data()), "get swapchain images");
        images.resize(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto& image = images[i];
            image.image = handles[i];
            VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = image.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            require(vkCreateImageView(device, &view, nullptr, &image.view), "create swapchain view");
            VkFramebufferCreateInfo framebuffer{}; framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer.renderPass = present_pass; framebuffer.attachmentCount = 1; framebuffer.pAttachments = &image.view;
            framebuffer.width = extent.width; framebuffer.height = extent.height; framebuffer.layers = 1;
            require(vkCreateFramebuffer(device, &framebuffer, nullptr, &image.framebuffer), "create framebuffer");
            VkSemaphoreCreateInfo semaphore{}; semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            require(vkCreateSemaphore(device, &semaphore, nullptr, &image.rendered), "create semaphore");
        }
        swapchain_dirty = false;
        const tessera::Size logical{logical_length(extent.width, scale), logical_length(extent.height, scale)};
        std::cout << "Swapchain " << extent.width << 'x' << extent.height << " at scale " << scale
                  << " -> logical " << logical.width << 'x' << logical.height << '\n';
        menu.resize(logical);
        apply(take(dispatcher.refresh(menu.snapshot()), "Pointer refresh rejected"));
    }
    void shutdown_vulkan() {
        if (device) {
            vkDeviceWaitIdle(device);
            if (renderer) { renderer->retire(last_frame); renderer.reset(); }
            destroy_images();
            if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
            if (readback) vkDestroyBuffer(device, readback, nullptr);
            if (readback_memory) vkFreeMemory(device, readback_memory, nullptr);
            for (auto& slot : slots) {
                if (slot.fence) vkDestroyFence(device, slot.fence, nullptr);
                if (slot.acquired) vkDestroySemaphore(device, slot.acquired, nullptr);
            }
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            if (capture_pass) vkDestroyRenderPass(device, capture_pass, nullptr);
            if (present_pass) vkDestroyRenderPass(device, present_pass, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (messenger) {
            auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            destroy_debug(instance, messenger, nullptr);
        }
        if (instance) vkDestroyInstance(instance, nullptr);
        device = VK_NULL_HANDLE; surface = VK_NULL_HANDLE; messenger = VK_NULL_HANDLE; instance = VK_NULL_HANDLE;
    }

    // ---- Frame ---------------------------------------------------------------------------
    void render() {
        if (minimized || quit || !device) return;
        if (swapchain_dirty) { recreate_swapchain(); if (swapchain_dirty || minimized) return; }
        auto& slot = slots[slot_index];
        require(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX), "wait frame fence");
        // A fence signal covers earlier submissions on this queue, so every frame <= slot.frame completed.
        renderer->retire(slot.frame);
        std::uint32_t index = 0;
        const auto acquired = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, slot.acquired, VK_NULL_HANDLE, &index);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) { swapchain_dirty = true; return; }
        if (acquired != VK_SUBOPTIMAL_KHR) require(acquired, "acquire swapchain image");
        auto& target = images[index];
        if (target.fence && target.fence != slot.fence) {
            require(vkWaitForFences(device, 1, &target.fence, VK_TRUE, UINT64_MAX), "wait image fence");
            renderer->retire(target.frame);
        }
        target.fence = slot.fence;
        require(vkResetFences(device, 1, &slot.fence), "reset fence");
        const bool capture = smoke && capture_next;
        require(vkResetCommandBuffer(slot.commands, 0), "reset commands");
        VkCommandBufferBeginInfo begin{}; begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        require(vkBeginCommandBuffer(slot.commands, &begin), "begin commands");
        VkClearValue clear{}; clear.color.float32[3] = 1;
        VkRenderPassBeginInfo pass{}; pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = capture ? capture_pass : present_pass; pass.framebuffer = target.framebuffer;
        pass.renderArea = {{0, 0}, extent}; pass.clearValueCount = 1; pass.pClearValues = &clear;
        vkCmdBeginRenderPass(slot.commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
        renderer->set_target({slot.commands, extent});
        const tessera::FrameInfo frame{last_frame + 1, menu.viewport, scale};
        const auto diagnostics = renderer->submit(frame, menu.paint);
        if (smoke && diagnostics.empty()) {
            const auto stats = renderer->submission_stats();
            if (stats.draw_calls != 1 || stats.primitives <= stats.draw_calls ||
                stats.upload_bytes != stats.primitives * 112 || renderer->pending_uploads() > frames_in_flight)
                fail("Menu adjacent batching or upload retirement counters disagree");
            if (last_frame == 0) std::cout << "Menu batching: " << stats.primitives << " primitives, "
                << stats.draw_calls << " draw, " << stats.upload_bytes << " upload bytes per frame.\n";
        }
        if (diagnostics.empty()) last_frame = frame.frame;
        else { print(diagnostics); ++submit_errors; }
        vkCmdEndRenderPass(slot.commands);
        if (capture) record_capture(slot.commands, target.image);
        require(vkEndCommandBuffer(slot.commands), "end commands");
        const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{}; submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &slot.acquired; submit.pWaitDstStageMask = &wait_stage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &slot.commands;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &target.rendered;
        require(vkQueueSubmit(queue, 1, &submit, slot.fence), "submit queue");
        slot.frame = target.frame = last_frame;
        VkPresentInfoKHR present{}; present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &target.rendered;
        present.swapchainCount = 1; present.pSwapchains = &swapchain; present.pImageIndices = &index;
        const auto presented_result = vkQueuePresentKHR(queue, &present);
        if (presented_result == VK_ERROR_OUT_OF_DATE_KHR || presented_result == VK_SUBOPTIMAL_KHR) swapchain_dirty = true;
        else require(presented_result, "present");
        slot_index = (slot_index + 1) % frames_in_flight;
        ++presented;
        if (capture) {
            require(vkWaitForFences(device, 1, &slot.fence, VK_TRUE, UINT64_MAX), "wait capture fence");
            renderer->retire(slot.frame);
            verify_capture();
        }
    }
    void record_capture(VkCommandBuffer commands, VkImage image) {
        const VkDeviceSize size = VkDeviceSize(extent.width) * extent.height * 4;
        if (size != readback_size) {
            if (readback) vkDestroyBuffer(device, readback, nullptr);
            if (readback_memory) vkFreeMemory(device, readback_memory, nullptr);
            VkBufferCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.size = size; info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            require(vkCreateBuffer(device, &info, nullptr, &readback), "create readback");
            VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, readback, &requirements);
            VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical, &memory);
            constexpr VkMemoryPropertyFlags flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            std::uint32_t type = UINT32_MAX;
            for (std::uint32_t i = 0; i < memory.memoryTypeCount && type == UINT32_MAX; ++i)
                if ((requirements.memoryTypeBits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) type = i;
            if (type == UINT32_MAX) throw std::runtime_error("No coherent readback memory");
            VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = type;
            require(vkAllocateMemory(device, &allocation, nullptr, &readback_memory), "allocate readback");
            require(vkBindBufferMemory(device, readback, readback_memory, 0), "bind readback");
            readback_size = size;
        }
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(commands, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        VkImageMemoryBarrier to_present{}; to_present.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        to_present.srcQueueFamilyIndex = to_present.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_present.image = image; to_present.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkBufferMemoryBarrier to_host{}; to_host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        to_host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        to_host.srcQueueFamilyIndex = to_host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        to_host.buffer = readback; to_host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, nullptr, 1, &to_host, 1, &to_present);
    }
    // Checks presented pixels against the styles that produced this frame's paint.
    void verify_capture() {
        capture_next = false;
        std::vector<std::uint8_t> pixels(readback_size);
        void* mapped;
        require(vkMapMemory(device, readback_memory, 0, readback_size, 0, &mapped), "map readback");
        std::memcpy(pixels.data(), mapped, pixels.size()); vkUnmapMemory(device, readback_memory);
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB)
            for (std::size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
        const auto name = "vulkan-menu-" + std::to_string(++captures) + ".ppm";
        std::filesystem::create_directories(TESSERA_GPU_ARTIFACT_DIR);
        std::ofstream file(std::filesystem::path(TESSERA_GPU_ARTIFACT_DIR) / name, std::ios::binary);
        file << "P6\n" << extent.width << ' ' << extent.height << "\n255\n";
        for (std::size_t i = 0; i < pixels.size(); i += 4) file.write(reinterpret_cast<const char*>(pixels.data() + i), 3);
        if (!file) return fail("Cannot write " + name);
        const auto matches = [&](unsigned x, unsigned y, tessera::Color c) {
            const auto at = (std::size_t(y) * extent.width + x) * 4;
            const float expected[3]{c.r, c.g, c.b};
            for (int i = 0; i < 3; ++i) if (std::abs(int(pixels[at + i]) - int(std::lround(expected[i] * 255))) > 2) return false;
            return true;
        };
        const auto pixel = [&](float logical) { return unsigned(logical * scale); };
        const auto expect = [&](float x, float y, tessera::Color c, const char* what) {
            if (!matches(pixel(x), pixel(y), c))
                fail(std::string(what) + " pixel mismatch in " + name + " at logical " + std::to_string(x) + ',' + std::to_string(y));
        };
        expect(2, 2, Menu::screen, "Screen background");
        const auto panel = menu.box("panel").border_box;
        expect(panel.origin.x + 6, panel.origin.y + panel.size.height / 2, Menu::panel, "Panel padding");
        for (const char* id : {"start", "quit"}) {
            const auto button = menu.box(id).border_box;
            expect(button.origin.x + 6, button.origin.y + button.size.height / 2, menu.style(id).background, "Button padding");
            const auto label = menu.box((std::string(id) + "-label").c_str()).content_box();
            bool glyph = false;
            for (auto y = pixel(label.origin.y); y < pixel(label.origin.y + label.size.height) && !glyph; ++y)
                for (auto x = pixel(label.origin.x); x < pixel(label.origin.x + label.size.width) && !glyph; ++x)
                    glyph = matches(x, y, Menu::label);
            if (!glyph) fail(std::string("No placeholder label pixels for ") + id + " in " + name);
        }
        std::cout << "Captured " << name << " (" << extent.width << 'x' << extent.height << ")\n";
    }

    // ---- Input normalization -------------------------------------------------------------
    void apply(const tessera::PointerDispatchResult& result) {
        pointers = result.pointers;
        std::optional<tessera::NodeHandle> hovered, active;
        for (const auto& pointer : pointers) { hovered = menu.button_of(pointer.hovered); active = pointer.active; }
        menu.show(hovered, active);
        // Action names are owned copies; host work runs after dispatch has returned.
        for (const auto& action : result.actions) {
            actions.push_back(action.action);
            std::cout << "Host action " << action.binding << ": " << action.action << " from " << *menu.tree->get(action.target)->id << '\n';
            if (action.action == "start-game") SetWindowTextW(window, L"Tessera Vulkan menu - started");
            else if (action.action == "quit-game") quit = true;
        }
    }
    void pointer(const tessera::InputEvent& event) {
        if (menu.layout.boxes.empty()) return; // No snapshot before the first swapchain/layout.
        const auto result = dispatcher.dispatch(menu.snapshot(), event);
        if (!result) { print(result.diagnostics); ++input_errors; return; }
        apply(*result.value);
    }
    bool pressed() const {
        return std::any_of(pointers.begin(), pointers.end(), [](const auto& p) { return p.pressed.has_value(); });
    }
    void cancel(const char* reason) {
        if (pressed()) std::cout << "Host cancelled pointer press: " << reason << '\n';
        buttons = 0;
        if (GetCapture() == window) { releasing_capture = true; ReleaseCapture(); releasing_capture = false; }
        pointer({now(), tessera::PointerCancel{mouse}});
    }
    void button(LPARAM lparam, WPARAM wparam, tessera::PointerButton which, bool down) {
        const unsigned bit = 1u << unsigned(which);
        const auto position = logical_point(lparam, scale);
        if (down) {
            if (!buttons) SetCapture(window);
            buttons |= bit;
            pointer({now(), tessera::PointerDown{mouse, position, which, modifiers(wparam)}});
        } else {
            pointer({now(), tessera::PointerUp{mouse, position, which, modifiers(wparam)}});
            buttons &= ~bit;
            if (!buttons && GetCapture() == window) { releasing_capture = true; ReleaseCapture(); releasing_capture = false; }
        }
    }
    LRESULT message(UINT msg, WPARAM wparam, LPARAM lparam) {
        switch (msg) {
        case WM_MOUSEMOVE:
            if (!tracking_leave) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                tracking_leave = TrackMouseEvent(&track) != FALSE;
            }
            pointer({now(), tessera::PointerMove{mouse, logical_point(lparam, scale), modifiers(wparam)}});
            return 0;
        case WM_LBUTTONDOWN: button(lparam, wparam, tessera::PointerButton::primary, true); return 0;
        case WM_LBUTTONUP: button(lparam, wparam, tessera::PointerButton::primary, false); return 0;
        case WM_RBUTTONDOWN: button(lparam, wparam, tessera::PointerButton::secondary, true); return 0;
        case WM_RBUTTONUP: button(lparam, wparam, tessera::PointerButton::secondary, false); return 0;
        case WM_MBUTTONDOWN: button(lparam, wparam, tessera::PointerButton::middle, true); return 0;
        case WM_MBUTTONUP: button(lparam, wparam, tessera::PointerButton::middle, false); return 0;
        case WM_MOUSELEAVE:
            tracking_leave = false;
            if (!buttons) cancel("pointer left the window"); // Clears hover; no press can exist.
            return 0;
        case WM_CAPTURECHANGED:
            if (!releasing_capture && buttons && HWND(lparam) != window) cancel("capture lost");
            return 0;
        case WM_CANCELMODE: cancel("cancel mode"); break;
        case WM_KILLFOCUS: cancel("focus lost"); break;
        case WM_SIZE:
            // Update point: swapchain, scale and layout change together before later input maps.
            minimized = wparam == SIZE_MINIMIZED;
            swapchain_dirty = true;
            if (!minimized && device) recreate_swapchain();
            return 0;
        case WM_ENTERSIZEMOVE: resizing = true; return 0;
        case WM_EXITSIZEMOVE: resizing = false; return 0;
        case WM_PAINT:
            ValidateRect(window, nullptr);
            if (resizing) render(); // The modal size loop blocks the main loop.
            return 0;
        case WM_DPICHANGED: {
            scale = LOWORD(wparam) / base_dpi;
            const auto* r = reinterpret_cast<const RECT*>(lparam);
            swapchain_dirty = true;
            SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            if (swapchain_dirty && !minimized && device) recreate_swapchain(); // Same size, new scale.
            return 0;
        }
        case WM_TIMER: fail("Smoke sequence timed out at step " + std::to_string(step)); return 0;
        case WM_CLOSE: quit = true; return 0; // Vulkan objects are destroyed before the window.
        case WM_ERASEBKGND: return 1;
        }
        return DefWindowProcW(window, msg, wparam, lparam);
    }

    // ---- Smoke sequence: OS mouse/cancel/DPI messages through the same window procedure ----
    POINT center(const char* id) const {
        const auto r = menu.box(id).border_box;
        return {LONG((r.origin.x + r.size.width / 2) * scale), LONG((r.origin.y + r.size.height / 2) * scale)};
    }
    void post(UINT msg, WPARAM wparam, POINT p) { PostMessageW(window, msg, wparam, MAKELPARAM(p.x, p.y)); }
    void click(const char* id) {
        const auto p = center(id);
        post(WM_MOUSEMOVE, 0, p); post(WM_LBUTTONDOWN, MK_LBUTTON, p); post(WM_LBUTTONUP, 0, p);
    }
    void change_dpi(UINT dpi, int width, int height) {
        RECT r{0, 0, width, height}, current;
        AdjustWindowRectExForDpi(&r, DWORD(GetWindowLongPtrW(window, GWL_STYLE)), FALSE, DWORD(GetWindowLongPtrW(window, GWL_EXSTYLE)), dpi);
        GetWindowRect(window, &current);
        OffsetRect(&r, current.left - r.left, current.top - r.top);
        SendMessageW(window, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), LPARAM(&r));
    }
    void expect(bool condition, const char* what) { if (!condition) fail(std::string("Smoke step ") + std::to_string(step) + ": " + what); }
    void expect_scale(float expected) {
        RECT client; GetClientRect(window, &client);
        expect(scale == expected, "device scale not applied");
        expect(extent.width == std::uint32_t(client.right) && extent.height == std::uint32_t(client.bottom), "swapchain extent differs from client area");
        expect(std::ceil(double(menu.viewport.width) * scale) == extent.width &&
               std::ceil(double(menu.viewport.height) * scale) == extent.height, "logical viewport does not map to the extent");
    }
    void smoke_step() {
        if (!smoke || quit || presented < step_ready) return;
        switch (step) {
        case 0: expect(captures == 1, "initial presentation was not captured"); click("start"); break;
        case 1: expect(actions == std::vector<std::string>{"start-game"}, "Start click did not request start-game");
                post(WM_LBUTTONDOWN, MK_LBUTTON, center("quit")); break;
        case 2: expect(pressed() && GetCapture() == window, "press did not capture");
                ReleaseCapture(); // Capture taken away while held: WM_CAPTURECHANGED.
                post(WM_LBUTTONUP, 0, center("quit")); break;
        case 3: expect(actions.size() == 1 && !pressed(), "release after capture loss activated");
                post(WM_LBUTTONDOWN, MK_LBUTTON, center("start")); break;
        case 4: expect(pressed(), "second press missing");
                SendMessageW(window, WM_CANCELMODE, 0, 0);
                post(WM_LBUTTONUP, 0, center("start")); break;
        case 5: expect(actions.size() == 1 && !pressed(), "release after WM_CANCELMODE activated");
                change_dpi(144, 720, 540); capture_next = true; break;
        case 6: expect_scale(1.5f); expect(captures == 2, "1.5x presentation was not captured"); click("start"); break;
        case 7: expect(actions.size() == 2 && actions.back() == "start-game", "1.5x Start click failed");
                change_dpi(120, 601, 451); capture_next = true; break;
        case 8: expect_scale(1.25f); expect(captures == 3, "1.25x presentation was not captured"); click("quit"); break;
        default: return;
        }
        ++step;
        step_ready = presented + 2;
    }
};

LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, LONG_PTR(app));
    }
    auto* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!app) return DefWindowProcW(window, msg, wparam, lparam);
    try { return app->message(msg, wparam, lparam); }
    catch (const std::exception& error) { app->fail(error.what()); return 0; } // Never unwind through Win32.
}

} // namespace

int main(int argc, char** argv) {
    App app;
    app.smoke = argc > 1 && std::strcmp(argv[1], "--smoke") == 0;
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSEXW type{sizeof(type)};
        type.lpfnWndProc = window_proc; type.hInstance = GetModuleHandleW(nullptr);
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.lpszClassName = L"TesseraVulkanMenu";
        if (!RegisterClassExW(&type)) throw std::runtime_error("RegisterClassExW failed");
        constexpr DWORD style = WS_OVERLAPPEDWINDOW;
        if (!CreateWindowExW(0, type.lpszClassName, L"Tessera Vulkan menu", style, CW_USEDEFAULT, CW_USEDEFAULT,
                             CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, type.hInstance, &app))
            throw std::runtime_error("CreateWindowExW failed");
        const UINT dpi = GetDpiForWindow(app.window);
        app.scale = dpi / base_dpi;
        RECT r{0, 0, LONG(480 * app.scale), LONG(360 * app.scale)};
        AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
        SetWindowPos(app.window, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
        app.initialize_vulkan();
        ShowWindow(app.window, SW_SHOWNORMAL);
        if (app.smoke) { app.capture_next = true; SetTimer(app.window, 1, 30'000, nullptr); }
        while (!app.quit) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) app.quit = true;
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (app.quit) break;
            if (app.minimized) { WaitMessage(); continue; }
            app.render();
            app.smoke_step();
        }
    } catch (const std::exception& error) {
        app.fail(error.what());
    }
    const auto frames = app.last_frame;
    app.shutdown_vulkan();
    if (app.window) DestroyWindow(app.window);
    if (app.smoke) {
        if (app.failure.empty() && app.step != 9) app.fail("Smoke sequence ended early at step " + std::to_string(app.step));
        if (app.actions != std::vector<std::string>{"start-game", "start-game", "quit-game"}) app.fail("Unexpected host action sequence");
    }
    if (validation_errors) app.fail(std::to_string(validation_errors) + " Vulkan validation errors");
    if (app.submit_errors || app.input_errors) app.fail("Rejected renderer submissions or pointer events");
    if (!app.failure.empty()) { std::cerr << app.failure << '\n'; return 1; }
    std::cout << "Presented " << app.presented << " swapchain frames (" << frames << " renderer frames retired)";
    if (app.smoke) std::cout << "; native Start/cancel/Quit smoke passed at scales 1, 1.5 and 1.25 with " << app.captures << " verified captures";
    std::cout << ".\n";
}
