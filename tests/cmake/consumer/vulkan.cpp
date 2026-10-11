#include <tessera/vulkan/renderer.hpp>
#include <iostream>
#include <stdexcept>

int main() {
    // Force linkage of the backend implementation and its transitive core/SDK
    // dependencies without making a device, window or GPU-runtime claim.
    try {
        tessera::VulkanRenderer renderer(tessera::VulkanContext{});
    } catch (const std::invalid_argument&) {
        std::cout << "Vulkan source consumer: empty host context rejected.\n";
        return 0;
    }
    return 1;
}
