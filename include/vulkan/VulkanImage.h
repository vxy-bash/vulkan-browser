#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace vkb {

// RAII wrapper for a VkImage + VkDeviceMemory + VkImageView triple.
class VulkanImage final {
public:
    VulkanImage() = default;
    VulkanImage(uint32_t width, uint32_t height,
                VkFormat format,
                VkImageUsageFlags usage,
                VkMemoryPropertyFlags memProps,
                VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);
    ~VulkanImage();

    VulkanImage(const VulkanImage&)            = delete;
    VulkanImage& operator=(const VulkanImage&) = delete;
    VulkanImage(VulkanImage&& o)               noexcept;
    VulkanImage& operator=(VulkanImage&& o)    noexcept;

    void destroy() noexcept;

    // Transition layout using a one-shot command buffer from VulkanManager.
    void transitionLayout(VkImageLayout from, VkImageLayout to);

    [[nodiscard]] VkImage     image()      const noexcept { return m_image; }
    [[nodiscard]] VkImageView view()       const noexcept { return m_view; }
    [[nodiscard]] VkFormat    format()     const noexcept { return m_format; }
    [[nodiscard]] uint32_t    width()      const noexcept { return m_width; }
    [[nodiscard]] uint32_t    height()     const noexcept { return m_height; }
    [[nodiscard]] bool        valid()      const noexcept { return m_image != VK_NULL_HANDLE; }

private:
    VkImage        m_image{VK_NULL_HANDLE};
    VkDeviceMemory m_memory{VK_NULL_HANDLE};
    VkImageView    m_view{VK_NULL_HANDLE};
    VkFormat       m_format{VK_FORMAT_UNDEFINED};
    uint32_t       m_width{};
    uint32_t       m_height{};
};

} // namespace vkb
