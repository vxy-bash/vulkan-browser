#include "vulkan/VulkanImage.h"
#include "vulkan/VulkanManager.h"

#include <stdexcept>

namespace vkb {

VulkanImage::VulkanImage(uint32_t width, uint32_t height,
                         VkFormat format, VkImageUsageFlags usage,
                         VkMemoryPropertyFlags memProps,
                         VkImageAspectFlags aspectMask)
    : m_format(format), m_width(width), m_height(height)
{
    auto& vm  = VulkanManager::instance();
    auto  dev = vm.device();

    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = format;
    ci.extent        = {width, height, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = usage;
    ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(dev, &ci, nullptr, &m_image) != VK_SUCCESS)
        throw std::runtime_error("VulkanImage: vkCreateImage failed");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev, m_image, &req);

    VkMemoryAllocateInfo ai{};
    ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize  = req.size;
    ai.memoryTypeIndex = vm.findMemoryType(req.memoryTypeBits, memProps);

    if (vkAllocateMemory(dev, &ai, nullptr, &m_memory) != VK_SUCCESS)
        throw std::runtime_error("VulkanImage: vkAllocateMemory failed");

    vkBindImageMemory(dev, m_image, m_memory, 0);

    VkImageViewCreateInfo vci{};
    vci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vci.image                           = m_image;
    vci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
    vci.format                          = format;
    vci.components                      = {VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY,
                                           VK_COMPONENT_SWIZZLE_IDENTITY};
    vci.subresourceRange.aspectMask     = aspectMask;
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = 1;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = 1;

    if (vkCreateImageView(dev, &vci, nullptr, &m_view) != VK_SUCCESS)
        throw std::runtime_error("VulkanImage: vkCreateImageView failed");
}

VulkanImage::~VulkanImage()
{
    destroy();
}

VulkanImage::VulkanImage(VulkanImage&& o) noexcept
    : m_image(o.m_image), m_memory(o.m_memory), m_view(o.m_view),
      m_format(o.m_format), m_width(o.m_width), m_height(o.m_height)
{
    o.m_image = VK_NULL_HANDLE;
    o.m_memory = VK_NULL_HANDLE;
    o.m_view   = VK_NULL_HANDLE;
}

VulkanImage& VulkanImage::operator=(VulkanImage&& o) noexcept
{
    if (this != &o) {
        destroy();
        m_image  = o.m_image;  o.m_image  = VK_NULL_HANDLE;
        m_memory = o.m_memory; o.m_memory = VK_NULL_HANDLE;
        m_view   = o.m_view;   o.m_view   = VK_NULL_HANDLE;
        m_format = o.m_format;
        m_width  = o.m_width;
        m_height = o.m_height;
    }
    return *this;
}

void VulkanImage::destroy() noexcept
{
    if (!valid()) return;
    auto dev = VulkanManager::instance().device();
    vkDestroyImageView(dev, m_view, nullptr);
    vkFreeMemory(dev, m_memory, nullptr);
    vkDestroyImage(dev, m_image, nullptr);
    m_image  = VK_NULL_HANDLE;
    m_memory = VK_NULL_HANDLE;
    m_view   = VK_NULL_HANDLE;
}

void VulkanImage::transitionLayout(VkImageLayout from, VkImageLayout to)
{
    VulkanManager::instance().submitImmediate([&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier barrier{};
        barrier.sType                           = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout                       = from;
        barrier.newLayout                       = to;
        barrier.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                           = m_image;
        barrier.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = 1;

        VkPipelineStageFlags srcStage{}, dstStage{};
        if (from == VK_IMAGE_LAYOUT_UNDEFINED &&
            to   == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        } else if (from == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
                   to   == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
            srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        }

        vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
    });
}

} // namespace vkb
