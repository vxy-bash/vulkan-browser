#include "vulkan/VulkanSwapchain.h"
#include "vulkan/VulkanManager.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace vkb {

VulkanSwapchain::VulkanSwapchain(VkSurfaceKHR surface)
    : m_surface(surface)
{}

VulkanSwapchain::~VulkanSwapchain()
{
    destroy();
}

void VulkanSwapchain::create(uint32_t width, uint32_t height)
{
    createSwapchain(width, height);
    createImageViews();
    createRenderPass();
    createFramebuffers();
}

void VulkanSwapchain::recreate(uint32_t width, uint32_t height)
{
    auto& vm = VulkanManager::instance();
    vkDeviceWaitIdle(vm.device());
    destroy();
    create(width, height);
}

void VulkanSwapchain::destroy() noexcept
{
    auto dev = VulkanManager::instance().device();
    for (auto fb : m_framebuffers) vkDestroyFramebuffer(dev, fb, nullptr);
    m_framebuffers.clear();
    if (m_renderPass) { vkDestroyRenderPass(dev, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
    for (auto iv : m_imageViews) vkDestroyImageView(dev, iv, nullptr);
    m_imageViews.clear();
    m_images.clear();
    if (m_swapchain) { vkDestroySwapchainKHR(dev, m_swapchain, nullptr); m_swapchain = VK_NULL_HANDLE; }
}

std::optional<uint32_t> VulkanSwapchain::acquireNextImage(VkSemaphore imageAvailable)
{
    uint32_t idx = 0;
    VkResult result = vkAcquireNextImageKHR(
        VulkanManager::instance().device(), m_swapchain,
        UINT64_MAX, imageAvailable, VK_NULL_HANDLE, &idx);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) return std::nullopt;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("VulkanSwapchain: vkAcquireNextImageKHR failed");
    return idx;
}

bool VulkanSwapchain::present(uint32_t imageIndex, VkSemaphore renderFinished)
{
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &renderFinished;
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &m_swapchain;
    pi.pImageIndices      = &imageIndex;

    VkResult result = vkQueuePresentKHR(VulkanManager::instance().presentQueue(), &pi);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return false;
    if (result != VK_SUCCESS) throw std::runtime_error("VulkanSwapchain: vkQueuePresentKHR failed");
    return true;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------
void VulkanSwapchain::createSwapchain(uint32_t width, uint32_t height)
{
    auto& vm = VulkanManager::instance();
    auto  support = querySupport(vm.physicalDevice(), m_surface);

    VkSurfaceFormatKHR surfFmt  = chooseSurfaceFormat(support.formats);
    VkPresentModeKHR   mode     = choosePresentMode(support.presentModes);
    VkExtent2D         ext      = chooseExtent(support.capabilities, width, height);

    uint32_t imgCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0)
        imgCount = std::min(imgCount, support.capabilities.maxImageCount);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface          = m_surface;
    ci.minImageCount    = imgCount;
    ci.imageFormat      = surfFmt.format;
    ci.imageColorSpace  = surfFmt.colorSpace;
    ci.imageExtent      = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    std::array<uint32_t, 2> families{vm.graphicsFamily(), vm.presentFamily()};
    if (families[0] != families[1]) {
        ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2;
        ci.pQueueFamilyIndices   = families.data();
    } else {
        ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    ci.preTransform   = support.capabilities.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode    = mode;
    ci.clipped        = VK_TRUE;

    if (vkCreateSwapchainKHR(vm.device(), &ci, nullptr, &m_swapchain) != VK_SUCCESS)
        throw std::runtime_error("VulkanSwapchain: vkCreateSwapchainKHR failed");

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(vm.device(), m_swapchain, &count, nullptr);
    m_images.resize(count);
    vkGetSwapchainImagesKHR(vm.device(), m_swapchain, &count, m_images.data());

    m_imageFormat = surfFmt.format;
    m_extent      = ext;
}

void VulkanSwapchain::createImageViews()
{
    auto dev = VulkanManager::instance().device();
    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo ci{};
        ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image                           = m_images[i];
        ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ci.format                          = m_imageFormat;
        ci.components                      = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY,
                                              VK_COMPONENT_SWIZZLE_IDENTITY};
        ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        ci.subresourceRange.baseMipLevel   = 0;
        ci.subresourceRange.levelCount     = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount     = 1;

        if (vkCreateImageView(dev, &ci, nullptr, &m_imageViews[i]) != VK_SUCCESS)
            throw std::runtime_error("VulkanSwapchain: failed to create image view");
    }
}

void VulkanSwapchain::createRenderPass()
{
    VkAttachmentDescription colorAttach{};
    colorAttach.format         = m_imageFormat;
    colorAttach.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAttach.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttach.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttach.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttach.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttach.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttach.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = 1;
    ci.pAttachments    = &colorAttach;
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    if (vkCreateRenderPass(VulkanManager::instance().device(), &ci, nullptr, &m_renderPass) != VK_SUCCESS)
        throw std::runtime_error("VulkanSwapchain: failed to create render pass");
}

void VulkanSwapchain::createFramebuffers()
{
    auto dev = VulkanManager::instance().device();
    m_framebuffers.resize(m_imageViews.size());
    for (size_t i = 0; i < m_imageViews.size(); ++i) {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = m_renderPass;
        ci.attachmentCount = 1;
        ci.pAttachments    = &m_imageViews[i];
        ci.width           = m_extent.width;
        ci.height          = m_extent.height;
        ci.layers          = 1;

        if (vkCreateFramebuffer(dev, &ci, nullptr, &m_framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("VulkanSwapchain: failed to create framebuffer");
    }
}

SwapchainSupportDetails VulkanSwapchain::querySupport(VkPhysicalDevice pd, VkSurfaceKHR surface)
{
    SwapchainSupportDetails d;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &d.capabilities);

    uint32_t fmtCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtCount, nullptr);
    if (fmtCount) { d.formats.resize(fmtCount); vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fmtCount, d.formats.data()); }

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &modeCount, nullptr);
    if (modeCount) { d.presentModes.resize(modeCount); vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface, &modeCount, d.presentModes.data()); }

    return d;
}

VkSurfaceFormatKHR VulkanSwapchain::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& available)
{
    for (const auto& f : available)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    return available.front();
}

VkPresentModeKHR VulkanSwapchain::choosePresentMode(
    const std::vector<VkPresentModeKHR>& available)
{
    for (auto m : available)
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanSwapchain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps,
                                          uint32_t w, uint32_t h)
{
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
        return caps.currentExtent;
    return {
        std::clamp(w, caps.minImageExtent.width,  caps.maxImageExtent.width),
        std::clamp(h, caps.minImageExtent.height, caps.maxImageExtent.height)
    };
}

} // namespace vkb
