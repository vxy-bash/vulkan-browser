#include "vulkan/VulkanManager.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace vkb {

// ---------------------------------------------------------------------------
// Debug messenger callback
// ---------------------------------------------------------------------------
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userdata*/)
{
    const char* prefix = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                             ? "[VK ERROR]"
                         : (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                             ? "[VK WARN]"
                             : "[VK INFO]";
    std::cerr << prefix << ' ' << data->pMessage << '\n';
    return VK_FALSE;
}

static VkResult createDebugMessenger(VkInstance                                instance,
                                     const VkDebugUtilsMessengerCreateInfoEXT* ci,
                                     VkDebugUtilsMessengerEXT*                 out)
{
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    return fn ? fn(instance, ci, nullptr, out) : VK_ERROR_EXTENSION_NOT_PRESENT;
}

static void destroyDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT m)
{
    auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (fn) fn(instance, m, nullptr);
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
VulkanManager& VulkanManager::instance()
{
    static VulkanManager s;
    return s;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void VulkanManager::initialize(std::span<const char* const> instanceExtensions,
                                std::span<const char* const> deviceExtensions,
                                VkSurfaceKHR                 probeSurface)
{
    if (m_initialized)
        return;

    createInstance(instanceExtensions);
    if (k_enableValidation)
        setupDebugMessenger();
    pickPhysicalDevice(probeSurface, deviceExtensions);
    createLogicalDevice(deviceExtensions);
    createCommandPools();

    m_initialized = true;
}

void VulkanManager::shutdown() noexcept
{
    if (!m_initialized) return;
    m_initialized = false;

    vkDestroyCommandPool(m_dev.device, m_dev.transferCommandPool, nullptr);
    vkDestroyCommandPool(m_dev.device, m_dev.graphicsCommandPool, nullptr);
    vkDestroyDevice(m_dev.device, nullptr);

    if (k_enableValidation && m_debugMessenger != VK_NULL_HANDLE)
        destroyDebugMessenger(m_instance, m_debugMessenger);

    vkDestroyInstance(m_instance, nullptr);
}

VkCommandBuffer VulkanManager::beginSingleTimeCommands()
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool        = m_dev.graphicsCommandPool;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkCommandBuffer cmd{VK_NULL_HANDLE};
    vkAllocateCommandBuffers(m_dev.device, &ai, &cmd);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    return cmd;
}

void VulkanManager::endSingleTimeCommands(VkCommandBuffer cmd)
{
    vkEndCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;

    {
        std::lock_guard lock(m_submitMutex);
        vkQueueSubmit(m_dev.graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_dev.graphicsQueue);
    }

    vkFreeCommandBuffers(m_dev.device, m_dev.graphicsCommandPool, 1, &cmd);
}

void VulkanManager::submitImmediate(std::function<void(VkCommandBuffer)> fn)
{
    auto cmd = beginSingleTimeCommands();
    fn(cmd);
    endSingleTimeCommands(cmd);
}

uint32_t VulkanManager::findMemoryType(uint32_t typeFilter,
                                        VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(m_dev.physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("VulkanManager: failed to find suitable memory type");
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
void VulkanManager::createInstance(std::span<const char* const> extensions)
{
    VkApplicationInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName   = "VulkanBrowser";
    ai.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    ai.pEngineName        = "vkb";
    ai.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    ai.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> exts(extensions.begin(), extensions.end());
    if (k_enableValidation)
        exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();

    if (k_enableValidation) {
        ci.enabledLayerCount   = 1;
        ci.ppEnabledLayerNames = &validationLayer;
    }

    if (vkCreateInstance(&ci, nullptr, &m_instance) != VK_SUCCESS)
        throw std::runtime_error("VulkanManager: vkCreateInstance failed — no Vulkan driver?");
}

void VulkanManager::setupDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;

    if (createDebugMessenger(m_instance, &ci, &m_debugMessenger) != VK_SUCCESS)
        std::cerr << "VulkanManager: validation layer requested but messenger creation failed\n";
}

void VulkanManager::pickPhysicalDevice(VkSurfaceKHR surface,
                                        std::span<const char* const> deviceExtensions)
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    if (count == 0)
        throw std::runtime_error("VulkanManager: no Vulkan-capable GPU found — aborting");

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(m_instance, &count, devices.data());

    // Prefer discrete GPU, then integrated, then anything.
    auto rank = [](const VkPhysicalDeviceProperties& p) {
        switch (p.deviceType) {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 3;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 2;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 1;
            default:                                      return 0;
        }
    };

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;

    for (auto pd : devices) {
        if (!deviceSuitable(pd, surface, deviceExtensions)) continue;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(pd, &props);
        int score = rank(props);
        if (score > bestScore) { bestScore = score; best = pd; }
    }

    if (best == VK_NULL_HANDLE)
        throw std::runtime_error("VulkanManager: no suitable Vulkan GPU — OpenGL fallback disabled");

    m_dev.physicalDevice = best;
    vkGetPhysicalDeviceProperties(best, &m_dev.properties);
    m_dev.features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    vkGetPhysicalDeviceFeatures2(best, &m_dev.features);
    m_dev.queueFamilies = findQueueFamilies(best, surface);

    std::cout << "VulkanManager: selected GPU: " << m_dev.properties.deviceName << '\n';
}

void VulkanManager::createLogicalDevice(std::span<const char* const> deviceExtensions)
{
    std::set<uint32_t> uniqueQueues{
        m_dev.queueFamilies.graphics.value(),
        m_dev.queueFamilies.present.value()
    };
    if (m_dev.queueFamilies.transfer.has_value())
        uniqueQueues.insert(m_dev.queueFamilies.transfer.value());

    float prio = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueQueues.size());
    for (uint32_t qf : uniqueQueues) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = qf;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &prio;
        queueInfos.push_back(qi);
    }

    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;
    features.fillModeNonSolid  = VK_TRUE;

    std::vector<const char*> exts(deviceExtensions.begin(), deviceExtensions.end());

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = static_cast<uint32_t>(queueInfos.size());
    ci.pQueueCreateInfos       = queueInfos.data();
    ci.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ci.ppEnabledExtensionNames = exts.data();
    ci.pEnabledFeatures        = &features;

    if (vkCreateDevice(m_dev.physicalDevice, &ci, nullptr, &m_dev.device) != VK_SUCCESS)
        throw std::runtime_error("VulkanManager: failed to create logical device");

    vkGetDeviceQueue(m_dev.device, m_dev.queueFamilies.graphics.value(), 0,
                     &m_dev.graphicsQueue);
    vkGetDeviceQueue(m_dev.device, m_dev.queueFamilies.present.value(), 0,
                     &m_dev.presentQueue);
    if (m_dev.queueFamilies.transfer.has_value())
        vkGetDeviceQueue(m_dev.device, m_dev.queueFamilies.transfer.value(), 0,
                         &m_dev.transferQueue);
}

void VulkanManager::createCommandPools()
{
    auto makePool = [&](uint32_t family, VkCommandPoolCreateFlags flags) {
        VkCommandPoolCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.queueFamilyIndex = family;
        ci.flags            = flags;
        VkCommandPool pool{VK_NULL_HANDLE};
        if (vkCreateCommandPool(m_dev.device, &ci, nullptr, &pool) != VK_SUCCESS)
            throw std::runtime_error("VulkanManager: failed to create command pool");
        return pool;
    };

    m_dev.graphicsCommandPool = makePool(
        m_dev.queueFamilies.graphics.value(),
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    uint32_t transferFamily = m_dev.queueFamilies.transfer.value_or(
        m_dev.queueFamilies.graphics.value());
    m_dev.transferCommandPool = makePool(
        transferFamily,
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);
}

QueueFamilyIndices VulkanManager::findQueueFamilies(VkPhysicalDevice pd,
                                                     VkSurfaceKHR     surface) const
{
    QueueFamilyIndices idx;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(pd, &count, families.data());

    for (uint32_t i = 0; i < count; ++i) {
        const auto& f = families[i];

        if ((f.queueFlags & VK_QUEUE_GRAPHICS_BIT) && !idx.graphics)
            idx.graphics = i;

        if ((f.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(f.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !idx.transfer)
            idx.transfer = i;

        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface, &presentSupport);
        if (presentSupport && !idx.present)
            idx.present = i;

        if (idx.isComplete()) break;
    }
    return idx;
}

bool VulkanManager::deviceSuitable(VkPhysicalDevice pd,
                                    VkSurfaceKHR     surface,
                                    std::span<const char* const> exts) const
{
    if (!findQueueFamilies(pd, surface).isComplete()) return false;
    if (!checkDeviceExtensionSupport(pd, exts))       return false;

    uint32_t fmtCount = 0, modeCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtCount, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &modeCount, nullptr);
    return fmtCount > 0 && modeCount > 0;
}

bool VulkanManager::checkDeviceExtensionSupport(VkPhysicalDevice pd,
                                                 std::span<const char* const> required) const
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateDeviceExtensionProperties(pd, nullptr, &count, available.data());

    for (const char* req : required) {
        bool found = std::ranges::any_of(available,
            [req](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, req) == 0;
            });
        if (!found) return false;
    }
    return true;
}

} // namespace vkb
