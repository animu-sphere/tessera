// Win32 + Vulkan native menu host. The host owns the window, OS input normalization, DPI, the
// Vulkan instance/device/queue/surface/swapchain, synchronization, presentation and retirement.
// Tessera receives logical pointer/scroll events and focus commands, resolved styles, scroll offsets
// and a recording command buffer only. Gamepad polling and its dead-zone/repeat policy also stay in the host.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <Xinput.h>
#define VK_USE_PLATFORM_WIN32_KHR
#include <tessera/vulkan/renderer.hpp>
#include <tessera/input/focus.hpp>
#include <tessera/input/scroll.hpp>
#include <tessera/render/paint.hpp>
#include <tessera/render/glyph_atlas.hpp>
#ifdef TESSERA_MENU_FONTS
#include <tessera/fonts/font_shaper.hpp>
#endif
#include <tessera/style/style_sheet.hpp>
#include <tessera/ui/serialization.hpp>
#include "gamepad.hpp"
#include "directinput.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cwchar>
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
constexpr std::uint32_t atlas_pages_per_slot = 16;
constexpr tessera::PointerId mouse{1};
constexpr float base_dpi = 96;
constexpr float wheel_step = 40; // Logical units per WHEEL_DELTA; partial deltas scroll proportionally.
constexpr const char* menu_json = R"({"version":1,"root":{"type":"Box","id":"screen","children":[
{"type":"Box","id":"panel","children":[
{"type":"Text","id":"title","properties":{"text":"Tessera"}},
{"type":"Box","id":"list","children":[
{"type":"Box","id":"start","classes":["button"],"properties":{"focusable":true},"events":{"activate":"start-game"},"children":[{"type":"Text","id":"start-label","properties":{"text":"Start"}}]},
{"type":"Box","id":"quit","classes":["button"],"properties":{"focusable":true},"events":{"activate":"quit-game"},"children":[{"type":"Text","id":"quit-label","properties":{"text":"Quit"}}]},
{"type":"Box","id":"credits","classes":["button"],"properties":{"focusable":true},"events":{"activate":"show-credits"},"children":[{"type":"Text","id":"credits-label","properties":{"text":"Credits"}}]}]}]}]}})";
constexpr const char* buttons[]{"start", "quit", "credits"};

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
#ifdef TESSERA_MENU_FONTS
std::string path_text(const std::filesystem::path& path) {
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
}
#endif
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
// Client pixel (x, y) covers [x, x+1) x [y, y+1); its center is the rasterizer's coverage and
// scissor sample, so a pixel painted by a half-open border box inside a clip is also a hit on it.
tessera::Point logical_point(int x, int y, float scale) {
    return {float((x + 0.5) / scale), float((y + 0.5) / scale)};
}
tessera::Point logical_point(LPARAM lparam, float scale) {
    return logical_point(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), scale);
}
gamepad::Sample sample(const XINPUT_GAMEPAD& pad) {
    constexpr std::pair<WORD, gamepad::Button> mapping[]{
        {XINPUT_GAMEPAD_DPAD_UP, gamepad::dpad_up}, {XINPUT_GAMEPAD_DPAD_DOWN, gamepad::dpad_down},
        {XINPUT_GAMEPAD_DPAD_LEFT, gamepad::dpad_left}, {XINPUT_GAMEPAD_DPAD_RIGHT, gamepad::dpad_right},
        {XINPUT_GAMEPAD_A, gamepad::accept}, {XINPUT_GAMEPAD_B, gamepad::back}};
    std::uint16_t held = 0;
    for (const auto& [xinput, button] : mapping) if (pad.wButtons & xinput) held |= button;
    return {held, pad.sThumbLX, pad.sThumbLY};
}
tessera::Modifiers modifiers(WPARAM wparam) {
    return {(wparam & MK_SHIFT) != 0, (wparam & MK_CONTROL) != 0, GetKeyState(VK_MENU) < 0,
            GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0};
}

// Backend-neutral document state: JSON -> tree -> stylesheet + interaction state -> styles -> layout -> paint.
struct Menu {
    static constexpr tessera::Color screen = rgb(18,20,26), panel = rgb(34,39,52), label = rgb(255,255,255);
    static constexpr tessera::Color idle = rgb(47,74,128), hover = rgb(66,104,176), pressed = rgb(32,50,88);
    static constexpr tessera::Color outline = rgb(90,120,180), focus_ring = rgb(255,214,102);
    std::unique_ptr<tessera::UiTree> tree;
    std::vector<std::uint32_t> parents;
    tessera::StyleSheet sheet;
    std::vector<tessera::ResolvedStyle> styles;
    std::unique_ptr<tessera::TextShaper> text;
    // The borrowed rasterizer in the cache is the real shaper; destroy the cache first.
    std::unique_ptr<tessera::GlyphCache> glyph_cache;
    tessera::LayoutResult layout;
    tessera::UiDrawList paint;
    tessera::Size viewport;
    std::vector<tessera::Point> offsets; // Host-owned requested scroll offsets by node index.

    Menu(const std::vector<std::filesystem::path>& fonts = {}) {
        tessera::FontId menu_font;
        text = std::make_unique<tessera::PlaceholderTextShaper>();
        if (!fonts.empty()) {
#ifdef TESSERA_MENU_FONTS
            const auto bytes = [](const std::filesystem::path& path) {
                std::ifstream file(path, std::ios::binary | std::ios::ate);
                if (!file) throw std::runtime_error("Cannot open font asset: " + path_text(path));
                const auto size = file.tellg();
                if (size <= 0 || size > 64 * 1024 * 1024) throw std::runtime_error("Font asset must be nonempty and at most 64 MiB: " + path_text(path));
                std::vector<std::byte> data(static_cast<std::size_t>(size));
                file.seekg(0); file.read(reinterpret_cast<char*>(data.data()), size);
                if (!file) throw std::runtime_error("Cannot read font asset: " + path_text(path));
                return data;
            };
            auto real = std::make_unique<tessera::FontShaper>();
            for (std::uint32_t i = 0; i < 2; ++i) {
                const auto diagnostics = real->set_face({i}, bytes(fonts.at(i)));
                if (!diagnostics.empty()) { print(diagnostics); throw std::runtime_error("Font asset rejected: " + path_text(fonts[i])); }
            }
            for (const auto& diagnostics : {real->set_family("menu-latin", {{{0}, 400}}),
                                            real->set_family("menu-cjk", {{{1}, 400}})}) {
                if (!diagnostics.empty()) { print(diagnostics); throw std::runtime_error("Menu family rejected"); }
            }
            const auto diagnostics = real->set_alias({2}, "ui-sans", {"menu-latin", "menu-cjk"});
            if (!diagnostics.empty()) { print(diagnostics); throw std::runtime_error("Menu font alias rejected"); }
            menu_font = *real->find_alias("ui-sans");
            glyph_cache = std::make_unique<tessera::GlyphCache>(*real);
            text = std::move(real);
#else
            throw std::runtime_error("--fonts requires a build with TESSERA_BUILD_FONTS=ON");
#endif
        }
        const tessera::ValidationContext actions{{"start-game", "quit-game", "show-credits"}};
        auto document = take(tessera::load_document(menu_json, actions), "Menu JSON rejected");
        if (glyph_cache) {
            auto& items = document.root.children[0].children[1].children;
            items[0].children[0].properties["text"] = std::string("Start スタート");
            items[1].children[0].properties["text"] = std::string("Quit 終了");
            items[2].children[0].properties["text"] = std::string("Credits クレジット");
        }
        tree = take(tessera::UiTree::create(std::move(document), actions), "Menu tree rejected");
        parents.assign(tree->size(), 0);
        offsets.assign(tree->size(), {});
        std::vector<tessera::NodeHandle> pending{tree->root()};
        while (!pending.empty()) {
            const auto node = pending.back(); pending.pop_back();
            for (const auto child : tree->children(node)) { parents[child.index] = node.index; pending.push_back(child); }
        }
        // Button labels inherit text size and color. Pseudo states change colors only, so geometry
        // stays the same in every interaction state.
        using S = tessera::StyleSelector;
        const auto rule = [&](S selector, auto declare) {
            tessera::StyleDeclarations values; declare(values); sheet.rules.push_back({std::move(selector), values});
        };
        rule(S::of_id("screen"), [menu_font](auto& s) { s.justify = tessera::Justify::center; s.align = tessera::Align::center; s.background = screen; s.text.font = menu_font; });
        rule(S::of_id("panel"), [](auto& s) {
            s.width = tessera::Dimension::points(240); s.padding = tessera::Edges{20,20,20,20}; s.gap = 12.0f;
            s.border = tessera::Edges{1,1,1,1}; s.border_color = rgb(70,82,105); s.corner_radius = 10.0f; s.background = panel;
        });
        rule(S::of_id("title"), [](auto& s) { s.text.size = 28.0f; s.color = rgb(235,238,245); });
        // A fixed-height scrolling list cuts Credits roughly in half at a fractional edge when unscrolled.
        rule(S::of_id("list"), [](auto& s) { s.height = tessera::Dimension::points(141.3f); s.gap = 12.0f; s.overflow = tessera::Overflow::scroll; });
        rule(S::of_class("button"), [](auto& s) {
            s.padding = tessera::Edges{10,16,10,16}; s.border = tessera::Edges{1,1,1,1}; s.border_color = outline;
            s.corner_radius = 6.0f; s.align = tessera::Align::center; s.background = idle; s.text.size = 20.0f; s.color = label;
        });
        rule(S::of_class("button", {.hover = true}), [](auto& s) { s.background = hover; });
        rule(S::of_class("button", {.active = true}), [](auto& s) { s.background = pressed; });
        rule(S::of_class("button", {.focus = true}), [](auto& s) { s.border_color = focus_ring; });
        if (glyph_cache) {
            // Two lines in the declared mixed-script fixture; retain a partly clipped third button.
            rule(S::of_id("start-label"), [](auto& s) { s.width = tessera::Dimension::points(70.1f); });
            rule(S::of_id("list"), [](auto& s) { s.height = tessera::Dimension::points(171.3f); });
        }
        styles = take(tessera::resolve_styles({tree.get(), &sheet, {}, {}}), "Menu styles rejected");
    }
    const tessera::ResolvedStyle& style(const char* id) const { return styles[tree->find(id)->index]; }
    const tessera::LayoutBox& box(const char* id) const {
        const auto node = *tree->find(id);
        for (const auto& b : layout.boxes) if (b.node == node) return b;
        throw std::logic_error("Displayed box missing");
    }
    tessera::HitTestInput snapshot() const { return {tree.get(), styles, &layout}; }
    void resize(tessera::Size size) {
        viewport = size;
        layout = take(tessera::compute_layout({tree.get(), styles, viewport, text.get(), offsets}), "Menu layout rejected");
        repaint();
    }
    // Update point for new scroll offsets: layout clamps them, and paint and hit testing share the result.
    void scroll(const std::vector<tessera::ScrollUpdate>& updates) {
        for (const auto& update : updates) offsets[update.container.index] = update.offset;
        resize(viewport);
    }
    void repaint() { paint = take(tessera::build_paint_list({tree.get(), styles, &layout, text.get()}), "Menu paint rejected"); }
    // Full-tree restyle for the current interaction state; changed styles re-run layout and paint.
    void show(tessera::InteractionState state) {
        auto resolved = take(tessera::resolve_styles({tree.get(), &sheet, std::move(state), {}}), "Menu styles rejected");
        if (resolved == styles) return;
        styles = std::move(resolved);
        resize(viewport);
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
    struct AtlasImage {
        tessera::ImageHandle handle;
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        bool bound = false;
    };
    std::vector<AtlasImage> atlas; // Images and upload buffers live through this slot's fence.
};

struct App {
    bool smoke = false;
    HWND window = nullptr;
    float scale = 1;
    bool minimized = false, resizing = false, swapchain_dirty = true, quit = false;
    Menu menu;
    tessera::PointerDispatcher dispatcher;
    tessera::FocusDispatcher focus;
    std::vector<tessera::PointerState> pointers;
    std::optional<tessera::NodeHandle> focused;
    std::vector<std::string> actions;
    gamepad::Navigator navigator;
    std::optional<DWORD> pad_slot;
    gamepad::DualSenseInput dualsense;
    gamepad::JoyConPairInput joycons;
    bool joycon_pair = false;
    std::string gamepad_source;
    std::chrono::microseconds next_pad_scan{0};
    std::optional<gamepad::Sample> scripted_pad; // Smoke-only replacement for the controller.
    float wheeled_from = 0; // Smoke-only list offset before the scripted wheel.
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
    VkSampler glyph_sampler = VK_NULL_HANDLE;

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
        // Real runs are explicitly lowered to images; direct glyph submission stays disabled.
        renderer = std::make_unique<tessera::VulkanRenderer>(tessera::VulkanContext{
            physical, device, present_pass, format.format, vertex, fragment, image,
            atlas_pages_per_slot * frames_in_flight, !menu.glyph_cache, batch});
        if (menu.glyph_cache) {
            VkFormatProperties properties;
            vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &properties);
            constexpr auto features = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
            if ((properties.optimalTilingFeatures & features) != features)
                throw std::runtime_error("Menu glyph pages require linearly sampled RGBA8 UNORM transfer destinations");
            VkSamplerCreateInfo sampler{}; sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            require(vkCreateSampler(device, &sampler, nullptr, &glyph_sampler), "create glyph sampler");
        }
    }
    std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
        VkPhysicalDeviceMemoryProperties properties;
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("No memory type for menu glyph upload");
    }
    void clear_atlas(FrameSlot& slot, bool unbind = true) {
        for (auto& page : slot.atlas) {
            if (unbind && page.bound) {
                const auto diagnostics = renderer->unbind_image(page.handle);
                if (!diagnostics.empty()) { print(diagnostics); throw std::runtime_error("Menu glyph image retirement rejected"); }
            }
            if (page.view) vkDestroyImageView(device, page.view, nullptr);
            if (page.image) vkDestroyImage(device, page.image, nullptr);
            if (page.memory) vkFreeMemory(device, page.memory, nullptr);
            if (page.staging) vkDestroyBuffer(device, page.staging, nullptr);
            if (page.staging_memory) vkFreeMemory(device, page.staging_memory, nullptr);
            page = {}; // Partial cleanup remains safe if a later binding rejects retirement.
        }
        slot.atlas.clear();
    }
    // Record host transfers before the render pass on the same queue. No queue idle/upload
    // helper submission: retain staging and sampled images with the frame that consumes them.
    void upload_atlas(FrameSlot& slot, const tessera::GlyphAtlasFrame& atlas) {
        if (!slot.atlas.empty()) throw std::logic_error("Old menu atlas was not retired");
        slot.atlas.resize(atlas.pages.size());
        for (std::size_t i = 0; i < atlas.pages.size(); ++i) {
            const auto& source = atlas.pages[i];
            auto& page = slot.atlas[i]; page.handle = source.image;
            VkBufferCreateInfo buffer{}; buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer.size = VkDeviceSize(source.coverage.size()) * 4; buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            require(vkCreateBuffer(device, &buffer, nullptr, &page.staging), "create glyph staging");
            VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device, page.staging, &requirements);
            VkMemoryAllocateInfo allocation{}; allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            require(vkAllocateMemory(device, &allocation, nullptr, &page.staging_memory), "allocate glyph staging");
            require(vkBindBufferMemory(device, page.staging, page.staging_memory, 0), "bind glyph staging");
            void* mapped;
            require(vkMapMemory(device, page.staging_memory, 0, buffer.size, 0, &mapped), "map glyph staging");
            auto* pixels = static_cast<std::uint8_t*>(mapped);
            for (std::size_t j = 0; j < source.coverage.size(); ++j) {
                pixels[j * 4] = pixels[j * 4 + 1] = pixels[j * 4 + 2] = 255;
                pixels[j * 4 + 3] = source.coverage[j];
            }
            vkUnmapMemory(device, page.staging_memory);
            VkImageCreateInfo image{}; image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            image.imageType = VK_IMAGE_TYPE_2D; image.format = VK_FORMAT_R8G8B8A8_UNORM;
            image.extent = {source.size, source.size, 1}; image.mipLevels = image.arrayLayers = 1;
            image.samples = VK_SAMPLE_COUNT_1_BIT; image.tiling = VK_IMAGE_TILING_OPTIMAL;
            image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            require(vkCreateImage(device, &image, nullptr, &page.image), "create glyph image");
            vkGetImageMemoryRequirements(device, page.image, &requirements);
            allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            require(vkAllocateMemory(device, &allocation, nullptr, &page.memory), "allocate glyph image");
            require(vkBindImageMemory(device, page.image, page.memory, 0), "bind glyph image memory");
            VkImageViewCreateInfo view{}; view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = page.image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = image.format;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            require(vkCreateImageView(device, &view, nullptr, &page.view), "create glyph view");
            VkImageMemoryBarrier barrier{}; barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.image = page.image; barrier.subresourceRange = view.subresourceRange;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(slot.commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.imageExtent = image.extent;
            vkCmdCopyBufferToImage(slot.commands, page.staging, page.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            barrier.oldLayout = barrier.newLayout; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(slot.commands, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
            const auto diagnostics = renderer->bind_image(page.handle, page.view, glyph_sampler);
            if (!diagnostics.empty()) { print(diagnostics); throw std::runtime_error("Menu glyph image binding rejected"); }
            page.bound = true;
        }
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
        for (auto& slot : slots) clear_atlas(slot);
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
        // Host reveal policy: focus in view before the resize is revealed again if the new layout hides it;
        // focus the user scrolled away from stays where it is.
        const bool keep = focused && take(tessera::scroll_into_view(menu.snapshot(), *focused), "Focus reveal rejected").empty();
        menu.resize(logical);
        if (keep) {
            const auto revealed = take(tessera::scroll_into_view(menu.snapshot(), *focused), "Focus reveal rejected");
            if (!revealed.empty()) menu.scroll(revealed);
        }
        pointers = take(dispatcher.refresh(menu.snapshot()), "Pointer refresh rejected").pointers;
        focused = take(focus.refresh(menu.snapshot()), "Focus refresh rejected").focused;
        present();
    }
    void shutdown_vulkan() {
        if (device) {
            vkDeviceWaitIdle(device);
            if (renderer) { renderer->retire(last_frame); renderer.reset(); }
            for (auto& slot : slots) clear_atlas(slot, false);
            if (glyph_sampler) vkDestroySampler(device, glyph_sampler, nullptr);
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
        clear_atlas(slot);
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
        std::optional<tessera::GlyphAtlasFrame> atlas;
        if (menu.glyph_cache) {
            atlas = take(tessera::prepare_glyph_atlas(menu.paint, *menu.glyph_cache, scale,
                {1 + slot_index * atlas_pages_per_slot}, {256, atlas_pages_per_slot, 4096}), "Menu glyph preparation rejected");
            upload_atlas(slot, *atlas);
            atlas->pages.clear(); // GPU copies own coverage now; CPU page lifetime cannot affect presentation.
        }
        VkClearValue clear{}; clear.color.float32[3] = 1;
        VkRenderPassBeginInfo pass{}; pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        pass.renderPass = capture ? capture_pass : present_pass; pass.framebuffer = target.framebuffer;
        pass.renderArea = {{0, 0}, extent}; pass.clearValueCount = 1; pass.pClearValues = &clear;
        vkCmdBeginRenderPass(slot.commands, &pass, VK_SUBPASS_CONTENTS_INLINE);
        renderer->set_target({slot.commands, extent});
        const tessera::FrameInfo frame{last_frame + 1, menu.viewport, scale};
        const auto diagnostics = renderer->submit(frame, atlas ? atlas->draw_list : menu.paint);
        if (smoke && diagnostics.empty()) {
            const auto stats = renderer->submission_stats();
            // Placeholder: two solid batches. Real glyphs alternate solid/image batches.
            if ((!atlas && stats.draw_calls != 2) || stats.primitives <= stats.draw_calls ||
                stats.upload_bytes != stats.primitives * 112 || renderer->pending_uploads() > frames_in_flight)
                fail("Menu adjacent batching or upload retirement counters disagree");
            for (const auto& page : slot.atlas) {
                const auto unbound = renderer->unbind_image(page.handle);
                const auto rebound = renderer->bind_image(page.handle, page.view, glyph_sampler);
                const auto live = [](const auto& errors) {
                    return errors.size() == 1 && errors[0].code == "resource_in_use";
                };
                if (!live(unbound) || !live(rebound)) fail("Live menu glyph image was not protected through completion");
            }
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
    // Small native glyph oracle: sample the original rasters at continuous baselines over
    // each label's flat button background. It reads no atlas coordinates or DrawImages and
    // handles only the menu's untransformed label interiors, not general primitive rendering.
    void verify_real_labels(const std::vector<std::uint8_t>& pixels, const std::string& name) {
        VkPhysicalDeviceProperties properties; vkGetPhysicalDeviceProperties(physical, &properties);
        const double precision = std::ldexp(1.0, int(properties.limits.subPixelPrecisionBits));
        const auto linear = [](double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
        const auto inside = [&](const tessera::Rect& rect, unsigned x, unsigned y) {
            return x + 0.5 >= double(rect.origin.x) * scale && y + 0.5 >= double(rect.origin.y) * scale &&
                x + 0.5 < (double(rect.origin.x) + rect.size.width) * scale &&
                y + 0.5 < (double(rect.origin.y) + rect.size.height) * scale;
        };
        double maximum_error = 0;
        std::array<std::size_t, 2> ink{};
        for (const char* id : ::buttons) {
            const auto label_id = std::string(id) + "-label";
            const auto& box = menu.box(label_id.c_str());
            const auto content = box.content_box();
            const tessera::DrawGlyphRun* run = nullptr;
            for (const auto& command : menu.paint.commands)
                if (const auto* candidate = std::get_if<tessera::DrawGlyphRun>(&command); candidate && candidate->origin == content.origin) run = candidate;
            if (!run) return fail("Real label paint missing in " + name);
            const auto& node = *menu.tree->get(*menu.tree->find(label_id));
            const auto measured = take(menu.text->measure(std::get<std::string>(node.properties.at("text")),
                menu.style(label_id.c_str()).text, {content.size.width}), "Native label measurement failed");
            if (measured != run->run.metrics || std::abs(measured.size.height - content.size.height) > 0.001f ||
                (label_id == "start-label" && measured.lines != 2)) return fail("Native label layout/paint metrics differ in " + name);
            struct Raster { const tessera::Glyph* glyph; std::shared_ptr<const tessera::GlyphBitmap> bitmap; };
            std::vector<Raster> rasters;
            std::array<bool, 2> faces{};
            for (const auto& glyph : run->run.glyphs) {
                if (!glyph.id || glyph.font.value > 1) return fail("Missing or unexpected native glyph face in " + name);
                faces[glyph.font.value] = true;
                rasters.push_back({&glyph, take(menu.glyph_cache->get({glyph.font, glyph.id,
                    std::uint32_t(std::round(double(run->run.size) * scale * 64))}), "Native reference raster failed")});
            }
            if (!faces[0] || !faces[1]) return fail("Native mixed-script fallback missing in " + name);
            const auto background = menu.style(id).background;
            for (unsigned y = 0; y < extent.height; ++y) for (unsigned x = 0; x < extent.width; ++x) {
                if (!inside(content, x, y) || (box.clip && !inside(*box.clip, x, y))) continue;
                std::array<double, 3> expected{linear(background.r), linear(background.g), linear(background.b)};
                for (const auto& raster : rasters) {
                    const auto& bitmap = *raster.bitmap;
                    if (!bitmap.width) continue;
                    const double left = (double(run->origin.x) + raster.glyph->position.x) * scale + bitmap.left;
                    const double top = (double(run->origin.y) + raster.glyph->position.y) * scale - bitmap.top;
                    const auto edge = [&](double v) { return std::round(v * precision) / precision; };
                    const double px = x + 0.5, py = y + 0.5;
                    if (px < edge(left) || px >= edge(left + bitmap.width) || py < edge(top) || py >= edge(top + bitmap.height)) continue;
                    const auto coverage = [&](int cx, int cy) {
                        return cx < 0 || cy < 0 || cx >= int(bitmap.width) || cy >= int(bitmap.height) ? 0.0 :
                            double(bitmap.coverage[std::size_t(cy) * bitmap.width + cx]) / 255;
                    };
                    const double sx = px - left - 0.5, sy = py - top - 0.5;
                    const int ix = int(std::floor(sx)), iy = int(std::floor(sy));
                    const double fx = sx - ix, fy = sy - iy;
                    const double alpha = ((1 - fy) * ((1 - fx) * coverage(ix, iy) + fx * coverage(ix + 1, iy)) +
                        fy * ((1 - fx) * coverage(ix, iy + 1) + fx * coverage(ix + 1, iy + 1))) * run->color.a;
                    if (alpha > 0.1) ++ink[raster.glyph->font.value];
                    const std::array<double, 3> tint{linear(run->color.r), linear(run->color.g), linear(run->color.b)};
                    for (unsigned c = 0; c < 3; ++c) expected[c] = tint[c] * alpha + expected[c] * (1 - alpha);
                }
                const auto at = (std::size_t(y) * extent.width + x) * 4;
                for (unsigned c = 0; c < 3; ++c) {
                    const double error = std::abs(linear(double(pixels[at + c]) / 255) - expected[c]);
                    maximum_error = std::max(maximum_error, error);
                    if (error > 2.0 / 255) return fail("Native glyph sampling mismatch in " + name + " at " +
                        std::to_string(x) + ',' + std::to_string(y));
                }
            }
        }
        if (!ink[0] || !ink[1]) return fail("Native capture has no visible Latin/Japanese ink in " + name);
        std::cout << "Native glyph scale=" << scale << " max linear error=" << maximum_error
            << "; Latin/Japanese ink samples=" << ink[0] << '/' << ink[1] << '\n';
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
        const auto name = std::string(menu.glyph_cache ? "vulkan-menu-real-" : "vulkan-menu-") + std::to_string(++captures) + ".ppm";
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
            // First pixel whose center lies in the 1-unit left border; its color shows focus.
            const auto border_x = unsigned(std::ceil(double(button.origin.x) * scale - 0.5));
            if (!matches(border_x, pixel(button.origin.y + button.size.height / 2), menu.style(id).border_color))
                fail(std::string("Button border pixel mismatch for ") + id + " in " + name);
            if (menu.glyph_cache) continue;
            const auto label = menu.box((std::string(id) + "-label").c_str()).content_box();
            bool glyph = false;
            for (auto y = pixel(label.origin.y); y < pixel(label.origin.y + label.size.height) && !glyph; ++y)
                for (auto x = pixel(label.origin.x); x < pixel(label.origin.x + label.size.width) && !glyph; ++x)
                    glyph = matches(x, y, Menu::label);
            if (!glyph) fail(std::string("No placeholder label pixels for ") + id + " in " + name);
        }
        if (menu.glyph_cache) verify_real_labels(pixels, name);
        // Clip agreement: near a list clip edge, a pixel shows the straddling button exactly when its
        // pixel-center pointer position targets that button.
        const auto agree = [&](const char* id, float logical_edge) {
            const auto node = menu.tree->find(id);
            const auto& straddling = menu.box(id);
            const auto edge = pixel(logical_edge);
            const tessera::Color shown[]{menu.style(id).background, menu.style(id).border_color, Menu::label};
            unsigned painted = 0, clipped = 0;
            for (auto y = edge - 3; y <= edge + 3; ++y) {
                for (auto x = pixel(straddling.border_box.origin.x) - 2;
                     x < pixel(straddling.border_box.origin.x + straddling.border_box.size.width) + 2; ++x) {
                    // Real glyph coverage blends continuously with the button. In this
                    // fixture the unclipped backdrop is flat panel color at both list edges.
                    const bool paints = menu.glyph_cache ? !matches(x, y, Menu::panel) :
                        std::any_of(std::begin(shown), std::end(shown), [&](tessera::Color c) { return matches(x, y, c); });
                    const auto hit = tessera::hit_test(menu.snapshot(), logical_point(int(x), int(y), scale));
                    if (!hit) { fail("Hit test rejected during clip agreement"); return false; }
                    const bool targets = menu.button_of(hit.value->target) == node;
                    if (paints != targets) {
                        fail("Clip edge paint/hit disagreement for " + std::string(id) + " in " + name + " at pixel " +
                             std::to_string(x) + ',' + std::to_string(y));
                        return false;
                    }
                    ++(targets ? painted : clipped);
                }
            }
            if (!painted || !clipped) { fail("Clip agreement scan did not straddle the list clip for " + std::string(id) + " in " + name); return false; }
            return true;
        };
        // Credits straddles the bottom edge at every offset the smoke uses; Start the top edge once scrolled.
        const auto& clip = *menu.box("credits").clip;
        if (!agree("credits", clip.origin.y + clip.size.height)) return;
        if (menu.box("list").scroll->offset.y > 0 && !agree("start", clip.origin.y)) return;
        std::cout << "Captured " << name << " (" << extent.width << 'x' << extent.height << ")\n";
    }

    // ---- Input normalization -------------------------------------------------------------
    void present() {
        tessera::InteractionState state{{}, {}, focused};
        for (const auto& pointer : pointers) {
            if (pointer.hovered) state.hovered.push_back(*pointer.hovered);
            if (pointer.active) state.active.push_back(*pointer.active);
        }
        menu.show(std::move(state));
    }
    void run(const std::vector<tessera::ActionRequest>& requests) {
        // Action names are owned copies; host work runs after dispatch has returned.
        for (const auto& action : requests) {
            actions.push_back(action.action);
            std::cout << "Host action " << action.binding << ": " << action.action << " from " << *menu.tree->get(action.target)->id << '\n';
            if (action.action == "start-game") SetWindowTextW(window, L"Tessera Vulkan menu - started");
            else if (action.action == "quit-game") quit = true;
            else if (action.action == "show-credits") SetWindowTextW(window, L"Tessera Vulkan menu - credits");
        }
    }
    void pointer(const tessera::InputEvent& event) {
        if (menu.layout.boxes.empty()) return; // No snapshot before the first swapchain/layout.
        const auto result = dispatcher.dispatch(menu.snapshot(), event);
        if (!result) { print(result.diagnostics); ++input_errors; return; }
        pointers = result.value->pointers;
        // Host press policy: a primary press on a button also focuses it, so keyboard navigation
        // continues from the button the mouse last pressed. Pointer dispatch never moves focus.
        if (const auto* down = std::get_if<tessera::PointerDown>(&event.data); down && down->button == tessera::PointerButton::primary)
            for (const auto& state : pointers)
                if (state.pointer == down->pointer && state.pressed && state.pressed != focused) {
                    const auto moved = focus.focus(menu.snapshot(), *state.pressed);
                    if (!moved) { print(moved.diagnostics); ++input_errors; }
                    else focused = moved.value->focused;
                }
        present();
        run(result.value->actions);
    }
    // New offsets change geometry under stationary pointers and focus, so both dispatchers refresh.
    void scroll_to(const std::vector<tessera::ScrollUpdate>& updates) {
        if (updates.empty()) return;
        menu.scroll(updates);
        pointers = take(dispatcher.refresh(menu.snapshot()), "Pointer refresh rejected").pointers;
        focused = take(focus.refresh(menu.snapshot()), "Focus refresh rejected").focused;
        present();
    }
    void command(const tessera::InputEvent& event) {
        if (menu.layout.boxes.empty()) return;
        const auto result = focus.dispatch(menu.snapshot(), event);
        if (!result) { print(result.diagnostics); ++input_errors; return; }
        const auto previous = focused;
        focused = result.value->focused;
        present();
        // Host focus policy: focus moved by a logical command scrolls into view; pointer presses do not,
        // so a click never moves its button out from under the cursor.
        if (focused && focused != previous) {
            const auto revealed = tessera::scroll_into_view(menu.snapshot(), *focused);
            if (!revealed) { print(revealed.diagnostics); ++input_errors; }
            else scroll_to(*revealed.value);
        }
        run(result.value->actions);
    }
    // Wheel policy: WHEEL_DELTA scrolls wheel_step logical units. A forward vertical wheel reveals content
    // above, and a right horizontal tilt content to the right. Message positions are screen pixels.
    void wheel(WPARAM wparam, LPARAM lparam, bool horizontal) {
        if (menu.layout.boxes.empty()) return;
        POINT p{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window, &p);
        const float amount = float(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * wheel_step;
        const tessera::Scroll scroll{logical_point(p.x, p.y, scale), horizontal ? tessera::Point{amount, 0} : tessera::Point{0, -amount},
                                     modifiers(GET_KEYSTATE_WPARAM(wparam))};
        const auto routed = tessera::route_scroll(menu.snapshot(), scroll);
        if (!routed) { print(routed.diagnostics); ++input_errors; return; }
        scroll_to(routed.value->updates);
    }
    // Keyboard policy: each handled key press becomes one logical command and never also a
    // KeyDown. Navigation repeats with the OS auto-repeat; Activate/Cancel fire once per press.
    // Keys with Ctrl/Alt and unmapped keys are left to the system.
    bool keyboard(WPARAM key, LPARAM lparam) {
        const bool repeat = (lparam & (1 << 30)) != 0;
        if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_MENU) < 0) return false;
        const auto navigate = [&](tessera::Direction direction) { command({now(), tessera::Navigate{direction, repeat}}); };
        switch (key) {
        case VK_TAB:
            if (GetKeyState(VK_SHIFT) < 0) command({now(), tessera::FocusPrevious{}});
            else command({now(), tessera::FocusNext{}});
            return true;
        case VK_UP: navigate(tessera::Direction::up); return true;
        case VK_DOWN: navigate(tessera::Direction::down); return true;
        case VK_LEFT: navigate(tessera::Direction::left); return true;
        case VK_RIGHT: navigate(tessera::Direction::right); return true;
        case VK_RETURN: case VK_SPACE: if (!repeat) command({now(), tessera::Activate{}}); return true;
        case VK_ESCAPE: if (!repeat) command({now(), tessera::Cancel{}}); return true;
        default: return false;
        }
    }
    // Gamepad: the first connected XInput slot, read only while this window is in the foreground.
    // Polling empty slots is costly, so they are rescanned at most once per second. The smoke
    // supplies scripted samples instead, so a physical controller cannot perturb it.
    std::optional<gamepad::Sample> read_gamepad(std::chrono::microseconds time) {
        if (smoke) return scripted_pad;
        if (joycon_pair) {
            gamepad_source = "DirectInput Joy-Con L/R";
            return joycons.read(window, time);
        }
        if (GetForegroundWindow() != window) return std::nullopt;
        XINPUT_STATE state{};
        if (!pad_slot && time >= next_pad_scan) {
            next_pad_scan = time + std::chrono::seconds(1);
            for (DWORD slot = 0; slot < XUSER_MAX_COUNT && !pad_slot; ++slot)
                if (XInputGetState(slot, &state) == ERROR_SUCCESS) pad_slot = slot;
        }
        if (!pad_slot) {
            auto dualsense_state = dualsense.read(window, time);
            if (dualsense_state && gamepad_source != "DirectInput DualSense") {
                navigator = {}; gamepad_source = "DirectInput DualSense";
            }
            return dualsense_state;
        }
        if (XInputGetState(*pad_slot, &state) != ERROR_SUCCESS) { pad_slot.reset(); return std::nullopt; }
        const auto source = "XInput slot=" + std::to_string(*pad_slot);
        if (gamepad_source != source) { navigator = {}; gamepad_source = source; }
        return sample(state.Gamepad);
    }
    void poll_gamepad() {
        const auto time = now();
        const auto state = read_gamepad(time);
        for (const auto& event : navigator.update(state, time)) {
            if (!smoke) {
                std::cout << "Physical " << gamepad_source << " buttons=" << state->buttons
                          << " stick=" << state->stick_x << ',' << state->stick_y << " command=";
                if (const auto* navigate = std::get_if<tessera::Navigate>(&event.data)) {
                    const char* names[]{"up", "down", "left", "right"};
                    std::cout << names[static_cast<unsigned>(navigate->direction)] << " repeat=" << navigate->repeat;
                } else std::cout << (std::holds_alternative<tessera::Activate>(event.data) ? "activate" : "cancel");
                std::cout << '\n';
            }
            command(event);
            if (!smoke) {
                std::cout << "Gamepad focus=";
                if (focused) std::cout << menu.tree->get(*focused)->id.value_or("<unnamed>");
                else std::cout << "<none>";
                std::cout << std::endl;
            }
        }
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
        case WM_MOUSEWHEEL: wheel(wparam, lparam, false); return 0;
        case WM_MOUSEHWHEEL: wheel(wparam, lparam, true); return 0;
        case WM_KEYDOWN: if (keyboard(wparam, lparam)) return 0; break;
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

    // ---- Smoke sequence: OS mouse/cancel/DPI/key messages and scripted gamepad samples -------
    // Gamepad policy alone, with synthetic times: dead zone, hysteresis, axis and D-pad precedence,
    // repeat timing, press edges, and input held at (re)connection.
    void check_gamepad_mapping() {
        for (unsigned direction = 0; direction < 8; ++direction) {
            const auto sample = gamepad::dualsense_sample(true, true, direction * 4500, -32768, -32768);
            const std::uint16_t hats[]{gamepad::dpad_up, gamepad::dpad_up | gamepad::dpad_right,
                gamepad::dpad_right, gamepad::dpad_down | gamepad::dpad_right, gamepad::dpad_down,
                gamepad::dpad_down | gamepad::dpad_left, gamepad::dpad_left, gamepad::dpad_up | gamepad::dpad_left};
            expect(sample.buttons == (hats[direction] | gamepad::accept | gamepad::back) &&
                   sample.stick_x == -32768 && sample.stick_y == 32767, "DualSense normalization differs");
        }
        for (const auto neutral : {0xffffffffu, 0xffffu, 36000u})
            expect(gamepad::dualsense_sample(false, false, neutral, 0, 0).buttons == 0, "Neutral DualSense POV must not navigate");
        const std::pair<std::int16_t, std::int16_t> joycon_sticks[]{
            {32767, 0}, {32767, -32768}, {0, -32768}, {-32768, -32768},
            {-32768, 0}, {-32768, 32767}, {0, 32767}, {32767, 32767}};
        for (unsigned direction = 0; direction < 8; ++direction) {
            const auto mapped = gamepad::joycon_pair_sample(15, 5, direction * 4500);
            expect(mapped.buttons == 63 && mapped.stick_x == joycon_sticks[direction].first &&
                   mapped.stick_y == joycon_sticks[direction].second, "Joy-Con vertical pair mapping differs");
        }
        const std::uint16_t joycon_directions[]{gamepad::dpad_left, gamepad::dpad_down, gamepad::dpad_up, gamepad::dpad_right};
        for (unsigned bit = 0; bit < 4; ++bit)
            expect(gamepad::joycon_pair_sample(std::uint16_t(1u << bit), 0, 0xffffffffu).buttons == joycon_directions[bit], "Joy-Con D-pad mapping differs");
        for (const auto neutral : {0xffffffffu, 0xffffu, 36000u}) {
            const auto mapped = gamepad::joycon_pair_sample(0, 0, neutral);
            expect(mapped.buttons == 0 && mapped.stick_x == 0 && mapped.stick_y == 0, "Neutral Joy-Con POV must not navigate");
        }
        expect(gamepad::joycon_pair_sample(0, 1, 0xffffffffu).buttons == gamepad::accept &&
               gamepad::joycon_pair_sample(0, 4, 0xffffffffu).buttons == gamepad::back &&
               gamepad::joycon_pair_sample(0xfff0, 0xfffa, 0xffffffffu).buttons == 0, "Joy-Con A/B or ignored buttons differ");
        using tessera::Direction;
        using Data = decltype(tessera::InputEvent::data);
        const auto nav = [](Direction direction, bool repeat) -> Data { return tessera::Navigate{direction, repeat}; };
        struct Case { std::optional<gamepad::Sample> sample; int ms; std::vector<Data> expected; const char* what; };
        const Case cases[]{
            {gamepad::Sample{gamepad::accept | gamepad::dpad_down}, 0, {}, "input held at connection"},
            {gamepad::Sample{}, 10, {}, "release after connection"},
            {gamepad::Sample{0, 0, 12000}, 20, {}, "stick inside the engage threshold"},
            {gamepad::Sample{0, 0, 20000}, 30, {nav(Direction::up, false)}, "stick engaging up"},
            {gamepad::Sample{0, 0, 9000}, 420, {}, "held stick before the repeat delay"},
            {gamepad::Sample{0, 0, 9000}, 430, {nav(Direction::up, true)}, "first repeat"},
            {gamepad::Sample{0, 0, 9000}, 540, {}, "held stick before the repeat interval"},
            {gamepad::Sample{0, 0, 9000}, 550, {nav(Direction::up, true)}, "second repeat"},
            {gamepad::Sample{0, 0, 7000}, 560, {}, "stick inside the release dead zone"},
            {gamepad::Sample{0, 23000, -22000}, 570, {nav(Direction::right, false)}, "dominant horizontal axis"},
            {gamepad::Sample{gamepad::dpad_left, 23000, -22000}, 580, {nav(Direction::left, false)}, "D-pad over stick"},
            {gamepad::Sample{gamepad::dpad_left | gamepad::accept | gamepad::back}, 590, {tessera::Activate{}, tessera::Cancel{}}, "press edges"},
            {gamepad::Sample{gamepad::dpad_left | gamepad::accept | gamepad::back}, 600, {}, "held buttons"},
            {std::nullopt, 610, {}, "disconnection"},
            {gamepad::Sample{gamepad::dpad_up}, 620, {}, "direction held at reconnection"},
            {gamepad::Sample{gamepad::dpad_up}, 1100, {}, "repeat of a direction held at reconnection"},
            {gamepad::Sample{gamepad::dpad_down}, 1110, {nav(Direction::down, false)}, "direction change after reconnection"},
        };
        gamepad::Navigator policy;
        for (const auto& c : cases) {
            const std::chrono::microseconds time = std::chrono::milliseconds(c.ms);
            std::vector<tessera::InputEvent> expected;
            for (const auto& data : c.expected) expected.push_back({time, data});
            if (policy.update(c.sample, time) != expected) return fail(std::string("Gamepad mapping mismatch: ") + c.what);
        }
        std::cout << "Gamepad mapping checked with " << std::size(cases) << " scripted samples.\n";
    }
    POINT center(const char* id) const {
        const auto r = menu.box(id).border_box;
        return {LONG((r.origin.x + r.size.width / 2) * scale), LONG((r.origin.y + r.size.height / 2) * scale)};
    }
    void post(UINT msg, WPARAM wparam, POINT p) { PostMessageW(window, msg, wparam, MAKELPARAM(p.x, p.y)); }
    void click(POINT p) { post(WM_MOUSEMOVE, 0, p); post(WM_LBUTTONDOWN, MK_LBUTTON, p); post(WM_LBUTTONUP, 0, p); }
    void click(const char* id) { click(center(id)); }
    void post_key(WPARAM vk, bool repeat = false) { PostMessageW(window, WM_KEYDOWN, vk, 1 | (repeat ? 1 << 30 : 0)); }
    void post_wheel(POINT p, short delta) {
        ClientToScreen(window, &p);
        PostMessageW(window, WM_MOUSEWHEEL, MAKEWPARAM(0, WORD(delta)), MAKELPARAM(p.x, p.y));
    }
    const tessera::LayoutBox& list() const { return menu.box("list"); }
    // Revealing a fractional list position can land within float rounding of its limit.
    static bool close_to(float a, float b) { return std::abs(a - b) <= 1e-4f; }
    // The Credits column's last pixel row inside the list clip, or the first row outside it, by the
    // pixel-center rule the renderer's scissor also uses.
    POINT clip_edge(bool inside) const {
        const auto& clip = *menu.box("credits").clip;
        const auto outside = LONG(std::ceil(double(clip.origin.y + clip.size.height) * scale - 0.5));
        return {center("credits").x, inside ? outside - 1 : outside};
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
                expect(menu.style("quit").background == Menu::pressed, "Pressed Quit was not styled as active");
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
                click(clip_edge(false)); break;
        case 8: expect(actions.size() == 2, "Click below the list clip activated Credits");
                click(clip_edge(true)); break;
        case 9: expect(actions.size() == 3 && actions.back() == "show-credits", "Click inside the list clip did not activate Credits");
                change_dpi(120, 601, 451); capture_next = true; break;
        case 10: expect_scale(1.25f); expect(captures == 3, "1.25x presentation was not captured");
                 expect(focused == menu.tree->find("credits"), "Primary press did not focus Credits");
                 expect(list().scroll->offset == tessera::Point{}, "Primary press focus scrolled the list");
                 post_key(VK_UP); post_key(VK_UP, true); post_key(VK_RETURN); break;
        case 11: expect(focused == menu.tree->find("start"), "Up arrows did not focus Start");
                 expect(actions.size() == 4 && actions.back() == "start-game", "Enter did not request start-game");
                 post_key(VK_TAB); post_key(VK_RETURN, true); post_key(VK_ESCAPE); capture_next = true; break;
        case 12: expect(focused == menu.tree->find("quit"), "Tab did not focus Quit");
                 expect(actions.size() == 4, "Repeated Enter or unbound Escape requested an action");
                 expect(captures == 4 && menu.style("quit").border_color == Menu::focus_ring, "Focus ring was not presented");
                 scripted_pad = gamepad::Sample{gamepad::accept}; break; // Connects with A held.
        case 13: expect(actions.size() == 4 && focused == menu.tree->find("quit"), "Gamepad button held at connection acted");
                 scripted_pad = gamepad::Sample{0, 0, 12000}; break;
        case 14: expect(focused == menu.tree->find("quit"), "Stick inside the engage threshold navigated");
                 scripted_pad = gamepad::Sample{0, 0, 32767}; break;
        case 15: expect(focused == menu.tree->find("start"), "Stick up did not focus Start");
                 scripted_pad = gamepad::Sample{gamepad::dpad_down}; break;
        case 16: expect(focused == menu.tree->find("quit"), "D-pad down did not focus Quit");
                 scripted_pad = gamepad::Sample{}; break;
        case 17: scripted_pad = gamepad::Sample{gamepad::dpad_down}; break;
        case 18: expect(focused == menu.tree->find("credits"), "Second D-pad down did not focus Credits");
                 expect(list().scroll_limit().y > 0 && close_to(list().scroll->offset.y, list().scroll_limit().y),
                        "Gamepad focus did not scroll Credits into view");
                 scripted_pad = gamepad::Sample{gamepad::accept}; break;
        case 19: expect(actions.size() == 5 && actions.back() == "show-credits", "A did not request show-credits");
                 scripted_pad = gamepad::Sample{gamepad::back}; break;
        case 20: expect(actions.size() == 5, "B with no cancel binding requested an action");
                 scripted_pad = gamepad::Sample{gamepad::dpad_up}; break;
        case 21: expect(focused == menu.tree->find("quit"), "D-pad up did not focus Quit");
                 expect(close_to(list().scroll->offset.y, list().scroll_limit().y), "Focusing visible Quit scrolled the list");
                 scripted_pad = gamepad::Sample{};
                 wheeled_from = list().scroll->offset.y;
                 post_wheel(center("list"), WHEEL_DELTA / 4); capture_next = true; break; // Forward: reveal above.
        case 22: expect(list().scroll->offset.y == wheeled_from - wheel_step / 4, "Partial wheel did not scroll the list");
                 expect(captures == 5, "Scrolled presentation was not captured");
                 click("credits"); break;
        case 23: expect(actions.size() == 6 && actions.back() == "show-credits", "Click on scrolled Credits did not request show-credits");
                 post_wheel(center("title"), -WHEEL_DELTA); break;
        case 24: expect(list().scroll->offset.y == wheeled_from - wheel_step / 4, "Wheel outside the list scrolled it");
                 click("quit"); break;
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

int wmain(int argc, wchar_t** argv) {
    std::cout << std::unitbuf;
    App app;
    try {
        std::vector<std::filesystem::path> fonts;
        for (int i = 1; i < argc; ++i) {
            if (std::wcscmp(argv[i], L"--smoke") == 0) app.smoke = true;
            else if (std::wcscmp(argv[i], L"--joycons") == 0) app.joycon_pair = true;
            else if (std::wcscmp(argv[i], L"--fonts") == 0 && fonts.empty() && i + 2 < argc) {
                fonts.emplace_back(argv[++i]); fonts.emplace_back(argv[++i]);
            } else throw std::runtime_error("Usage: tessera_vulkan_menu [--smoke] [--joycons] [--fonts <Latin.ttf/otf> <Japanese.ttf/otf>]");
        }
        if (!fonts.empty()) app.menu = Menu(fonts);
        if (app.joycon_pair) std::cout << "Joy-Con pair selected: left directions/stick, right A=activate, B=cancel; both sides required.\n";
        std::cout << (app.menu.glyph_cache ? "Real text with explicit Latin/Japanese assets.\n" : "Placeholder text; pass --fonts in a fonts-enabled build for real glyphs.\n");
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
        if (app.smoke) { app.check_gamepad_mapping(); app.capture_next = true; SetTimer(app.window, 1, 30'000, nullptr); }
        while (!app.quit) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) app.quit = true;
                TranslateMessage(&msg); DispatchMessageW(&msg);
            }
            if (app.quit) break;
            if (app.minimized) { WaitMessage(); continue; }
            app.poll_gamepad();
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
        if (app.failure.empty() && app.step != 25) app.fail("Smoke sequence ended early at step " + std::to_string(app.step));
        if (app.actions != std::vector<std::string>{"start-game", "start-game", "show-credits", "start-game", "show-credits", "show-credits", "quit-game"}) app.fail("Unexpected host action sequence");
    }
    if (validation_errors) app.fail(std::to_string(validation_errors) + " Vulkan validation errors");
    if (app.submit_errors || app.input_errors) app.fail("Rejected renderer submissions or input events");
    if (!app.failure.empty()) { std::cerr << app.failure << '\n'; return 1; }
    std::cout << "Presented " << app.presented << " swapchain frames (" << frames << " renderer frames retired)";
    if (app.smoke) std::cout << "; native mouse/cancel/clip/keyboard/gamepad/scroll smoke passed at scales 1, 1.5 and 1.25 with " << app.captures << " verified captures";
    std::cout << ".\n";
}
