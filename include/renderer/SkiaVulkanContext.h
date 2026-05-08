#pragma once

// Skia public headers
#include "include/core/SkSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/vk/GrVkBackendContext.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <cstdint>

namespace vkb {

// Wraps a Skia GrDirectContext backed by our shared Vulkan device,
// and exposes an SkSurface sized to the tab's render area.
class SkiaVulkanContext final {
public:
    SkiaVulkanContext();
    ~SkiaVulkanContext();

    SkiaVulkanContext(const SkiaVulkanContext&)            = delete;
    SkiaVulkanContext& operator=(const SkiaVulkanContext&) = delete;

    // Initialize Skia GrDirectContext from the shared VulkanManager device.
    // Must be called after VulkanManager::initialize().
    void initialize();

    // Create or resize the SkSurface backed by the given VkImage.
    // imageLayout must be VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL when passed in.
    void createSurface(VkImage     image,
                       VkImageView view,
                       VkFormat    format,
                       uint32_t    width,
                       uint32_t    height);

    void destroySurface() noexcept;
    void destroy()        noexcept;

    // Flush all pending Skia draw calls and signal renderFinishedSemaphore.
    void flush(VkSemaphore imageAvailableSem, VkSemaphore renderFinishedSem);

    [[nodiscard]] SkCanvas*          canvas()  const noexcept;
    [[nodiscard]] SkSurface*         surface() const noexcept { return m_surface.get(); }
    [[nodiscard]] GrDirectContext*   grCtx()   const noexcept { return m_grCtx.get(); }
    [[nodiscard]] bool               ready()   const noexcept { return m_surface != nullptr; }

private:
    sk_sp<GrDirectContext> m_grCtx;
    sk_sp<SkSurface>       m_surface;
};

} // namespace vkb
