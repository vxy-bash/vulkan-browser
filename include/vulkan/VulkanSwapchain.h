#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <functional>
#include <optional>

namespace vkb {

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};

// Per-tab swapchain.  The VkDevice is shared (from VulkanManager).
// On resize or tab-show, call recreate() — no device re-init needed.
class VulkanSwapchain final {
public:
    explicit VulkanSwapchain(VkSurfaceKHR surface);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&)            = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    // Create or recreate swapchain for the given pixel dimensions.
    void create(uint32_t width, uint32_t height);
    void recreate(uint32_t width, uint32_t height);
    void destroy() noexcept;

    // Acquire the next image; returns false if swapchain is out-of-date (call recreate).
    [[nodiscard]] std::optional<uint32_t> acquireNextImage(VkSemaphore imageAvailable);

    // Present the rendered image.  Returns false if swapchain is out-of-date.
    [[nodiscard]] bool present(uint32_t imageIndex, VkSemaphore renderFinished);

    // --- Accessors ---
    [[nodiscard]] VkSwapchainKHR             handle()       const noexcept { return m_swapchain; }
    [[nodiscard]] VkFormat                   imageFormat()  const noexcept { return m_imageFormat; }
    [[nodiscard]] VkExtent2D                 extent()       const noexcept { return m_extent; }
    [[nodiscard]] uint32_t                   imageCount()   const noexcept { return static_cast<uint32_t>(m_images.size()); }
    [[nodiscard]] const std::vector<VkImage>&     images()      const noexcept { return m_images; }
    [[nodiscard]] const std::vector<VkImageView>& imageViews()  const noexcept { return m_imageViews; }
    [[nodiscard]] VkRenderPass               renderPass()   const noexcept { return m_renderPass; }
    [[nodiscard]] const std::vector<VkFramebuffer>& framebuffers() const noexcept { return m_framebuffers; }

    [[nodiscard]] static SwapchainSupportDetails querySupport(VkPhysicalDevice pd,
                                                               VkSurfaceKHR     surface);

private:
    void createSwapchain(uint32_t width, uint32_t height);
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();

    [[nodiscard]] static VkSurfaceFormatKHR chooseSurfaceFormat(
        const std::vector<VkSurfaceFormatKHR>& available);
    [[nodiscard]] static VkPresentModeKHR choosePresentMode(
        const std::vector<VkPresentModeKHR>& available);
    [[nodiscard]] static VkExtent2D chooseExtent(
        const VkSurfaceCapabilitiesKHR& caps, uint32_t w, uint32_t h);

    VkSurfaceKHR                m_surface{VK_NULL_HANDLE};
    VkSwapchainKHR              m_swapchain{VK_NULL_HANDLE};
    VkFormat                    m_imageFormat{VK_FORMAT_UNDEFINED};
    VkExtent2D                  m_extent{};
    std::vector<VkImage>        m_images;
    std::vector<VkImageView>    m_imageViews;
    VkRenderPass                m_renderPass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer>  m_framebuffers;
};

} // namespace vkb
