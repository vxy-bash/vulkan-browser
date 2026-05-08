#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <functional>
#include <mutex>
#include <optional>
#include <span>

// Forward declarations
struct GLFWwindow;

namespace vkb {

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    std::optional<uint32_t> transfer;

    [[nodiscard]] bool isComplete() const noexcept {
        return graphics.has_value() && present.has_value();
    }
};

struct VulkanDeviceInfo {
    VkPhysicalDevice          physicalDevice{VK_NULL_HANDLE};
    VkDevice                  device{VK_NULL_HANDLE};
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures2  features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    QueueFamilyIndices         queueFamilies{};
    VkQueue                    graphicsQueue{VK_NULL_HANDLE};
    VkQueue                    presentQueue{VK_NULL_HANDLE};
    VkQueue                    transferQueue{VK_NULL_HANDLE};
    VkCommandPool              graphicsCommandPool{VK_NULL_HANDLE};
    VkCommandPool              transferCommandPool{VK_NULL_HANDLE};
};

// Singleton that owns the VkInstance and shared VkDevice.
// All tabs share one device; each tab owns its own swapchain and SkSurface.
class VulkanManager final {
public:
    static VulkanManager& instance();

    // Called once at application startup.
    // requiredExtensions must include surface extensions from Qt/GLFW.
    void initialize(std::span<const char* const> instanceExtensions,
                    std::span<const char* const> deviceExtensions,
                    VkSurfaceKHR                 probeSurface);

    void shutdown() noexcept;

    [[nodiscard]] VkInstance               vkInstance()       const noexcept { return m_instance; }
    [[nodiscard]] const VulkanDeviceInfo&  deviceInfo()       const noexcept { return m_dev; }
    [[nodiscard]] VkPhysicalDevice         physicalDevice()   const noexcept { return m_dev.physicalDevice; }
    [[nodiscard]] VkDevice                 device()           const noexcept { return m_dev.device; }
    [[nodiscard]] VkQueue                  graphicsQueue()    const noexcept { return m_dev.graphicsQueue; }
    [[nodiscard]] VkQueue                  presentQueue()     const noexcept { return m_dev.presentQueue; }
    [[nodiscard]] uint32_t                 graphicsFamily()   const noexcept { return m_dev.queueFamilies.graphics.value(); }
    [[nodiscard]] uint32_t                 presentFamily()    const noexcept { return m_dev.queueFamilies.present.value(); }
    [[nodiscard]] VkCommandPool            graphicsPool()     const noexcept { return m_dev.graphicsCommandPool; }

    // Allocate/free a primary command buffer from the shared graphics pool.
    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands();
    void                          endSingleTimeCommands(VkCommandBuffer cmd);

    // Convenience: run a lambda with a one-shot command buffer.
    void submitImmediate(std::function<void(VkCommandBuffer)> fn);

    // Memory helpers
    [[nodiscard]] uint32_t findMemoryType(uint32_t typeFilter,
                                          VkMemoryPropertyFlags props) const;

    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }

    // Non-copyable, non-movable
    VulkanManager(const VulkanManager&)            = delete;
    VulkanManager& operator=(const VulkanManager&) = delete;
    VulkanManager(VulkanManager&&)                 = delete;
    VulkanManager& operator=(VulkanManager&&)      = delete;

private:
    VulkanManager()  = default;
    ~VulkanManager() = default;

    void createInstance(std::span<const char* const> extensions);
    void setupDebugMessenger();
    void pickPhysicalDevice(VkSurfaceKHR surface,
                            std::span<const char* const> deviceExtensions);
    void createLogicalDevice(std::span<const char* const> deviceExtensions);
    void createCommandPools();

    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice pd,
                                                        VkSurfaceKHR     surface) const;
    [[nodiscard]] bool               deviceSuitable(VkPhysicalDevice pd,
                                                    VkSurfaceKHR     surface,
                                                    std::span<const char* const> exts) const;
    [[nodiscard]] bool               checkDeviceExtensionSupport(
                                        VkPhysicalDevice pd,
                                        std::span<const char* const> exts) const;

    VkInstance               m_instance{VK_NULL_HANDLE};
    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};
    VulkanDeviceInfo         m_dev{};
    bool                     m_initialized{false};
    mutable std::mutex       m_submitMutex;

    static constexpr bool k_enableValidation{
#ifdef NDEBUG
        false
#else
        true
#endif
    };
};

} // namespace vkb
