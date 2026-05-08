#include "renderer/SkiaVulkanContext.h"
#include "vulkan/VulkanManager.h"

#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/vk/GrVkTypes.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"

#include <stdexcept>

namespace vkb {

SkiaVulkanContext::SkiaVulkanContext()  = default;
SkiaVulkanContext::~SkiaVulkanContext() { destroy(); }

void SkiaVulkanContext::initialize()
{
    auto& vm = VulkanManager::instance();

    GrVkBackendContext backendCtx{};
    backendCtx.fInstance       = vm.vkInstance();
    backendCtx.fPhysicalDevice = vm.physicalDevice();
    backendCtx.fDevice         = vm.device();
    backendCtx.fQueue          = vm.graphicsQueue();
    backendCtx.fGraphicsQueueIndex = vm.graphicsFamily();
    // Skia needs Vulkan 1.1 or later; we request 1.3 in VulkanManager.
    backendCtx.fMaxAPIVersion  = VK_API_VERSION_1_3;

    m_grCtx = GrDirectContext::MakeVulkan(backendCtx);
    if (!m_grCtx)
        throw std::runtime_error(
            "SkiaVulkanContext: GrDirectContext::MakeVulkan failed — "
            "Vulkan GPU must support the required Skia extensions");
}

void SkiaVulkanContext::createSurface(VkImage     image,
                                       VkImageView view,
                                       VkFormat    format,
                                       uint32_t    width,
                                       uint32_t    height)
{
    destroySurface();

    GrVkImageInfo imageInfo{};
    imageInfo.fImage             = image;
    imageInfo.fImageLayout       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    imageInfo.fFormat            = format;
    imageInfo.fLevelCount        = 1;
    imageInfo.fCurrentQueueFamily = VulkanManager::instance().graphicsFamily();

    // Skia wraps the swapchain image; it does NOT own it.
    GrBackendRenderTarget backendRT(
        static_cast<int>(width),
        static_cast<int>(height),
        /* sampleCnt= */ 1,
        imageInfo);

    SkSurfaceProps props(SkSurfaceProps::kUseDeviceIndependentFonts_Flag,
                         kUnknown_SkPixelGeometry);

    m_surface = SkSurface::MakeFromBackendRenderTarget(
        m_grCtx.get(),
        backendRT,
        kTopLeft_GrSurfaceOrigin,
        kBGRA_8888_SkColorType,
        SkColorSpace::MakeSRGB(),
        &props);

    if (!m_surface)
        throw std::runtime_error(
            "SkiaVulkanContext: SkSurface::MakeFromBackendRenderTarget failed");

    (void)view; // stored in imageInfo; kept for potential future use
}

void SkiaVulkanContext::destroySurface() noexcept
{
    m_surface.reset();
    if (m_grCtx) m_grCtx->flushAndSubmit();
}

void SkiaVulkanContext::destroy() noexcept
{
    destroySurface();
    m_grCtx.reset();
}

void SkiaVulkanContext::flush(VkSemaphore imageAvailableSem,
                               VkSemaphore renderFinishedSem)
{
    if (!m_surface || !m_grCtx) return;

    GrFlushInfo flushInfo{};
    // Signal renderFinishedSem so the swapchain can present once Skia is done.
    flushInfo.fNumSemaphores   = 1;
    GrBackendSemaphore signalSem;
    signalSem.initVulkan(renderFinishedSem);
    flushInfo.fSignalSemaphores = &signalSem;

    m_surface->flush(flushInfo);
    m_grCtx->submit();
}

SkCanvas* SkiaVulkanContext::canvas() const noexcept
{
    return m_surface ? m_surface->getCanvas() : nullptr;
}

} // namespace vkb
