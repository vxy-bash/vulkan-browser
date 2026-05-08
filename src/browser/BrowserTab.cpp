#include "browser/BrowserTab.h"
#include "vulkan/VulkanManager.h"
#include "ipc/IpcChannel.h"

#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <QPaintEvent>
#include <QWindow>
#include <stdexcept>
#include <iostream>

// Qt platform-native handle needed to create a VkSurfaceKHR.
// We use QWindow::winId() + platform-specific Vulkan surface creation.
#if defined(Q_OS_LINUX)
#  include <vulkan/vulkan_xcb.h>   // or vulkan_wayland.h at runtime
#  include <QGuiApplication>
#  include <qpa/qplatformnativeinterface.h>
#elif defined(Q_OS_WIN)
#  include <vulkan/vulkan_win32.h>
#  include <windows.h>
#elif defined(Q_OS_MACOS)
#  include <vulkan/vulkan_metal.h>
#endif

namespace vkb {

BrowserTab::BrowserTab(QWidget* parent)
    : QWidget(parent)
{
    // Tell Qt to not paint this widget — Vulkan owns the pixels.
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);

    connect(&m_renderTimer, &QTimer::timeout, this, &BrowserTab::onRenderFrame);
}

BrowserTab::~BrowserTab()
{
    m_renderTimer.stop();

    auto dev = VulkanManager::instance().device();
    vkDeviceWaitIdle(dev);

    m_skia.destroy();
    m_swapchain.reset();
    destroySyncObjects();

    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(VulkanManager::instance().vkInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------
void BrowserTab::navigate(const QUrl& url)
{
    m_currentUrl = url;
    m_loading    = true;
    emit loadStarted();
    emit urlChanged(url);

    if (m_ipc)
        m_ipc->sendNavigate(url.toString());
}

// ---------------------------------------------------------------------------
// Protected overrides
// ---------------------------------------------------------------------------
void BrowserTab::paintEvent(QPaintEvent*)
{
    // Intentionally empty — Vulkan renders directly to the window surface.
    // Qt must never paint over our pixels.
}

void BrowserTab::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (!m_vulkanReady) return;

    const uint32_t w = static_cast<uint32_t>(event->size().width());
    const uint32_t h = static_cast<uint32_t>(event->size().height());
    if (w == 0 || h == 0) return;

    handleSwapchainOutOfDate();

    if (m_ipc) m_ipc->sendResize(w, h);
}

void BrowserTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (!m_vulkanReady && winId()) {
        try {
            initVulkanSurface();
            initSyncObjects();

            const uint32_t w = static_cast<uint32_t>(width());
            const uint32_t h = static_cast<uint32_t>(height());
            m_swapchain = std::make_unique<VulkanSwapchain>(m_surface);
            m_swapchain->create(w, h);

            m_skia.initialize();
            // Attach Skia to the first swapchain image (index 0).
            m_skia.createSurface(m_swapchain->images()[0],
                                  m_swapchain->imageViews()[0],
                                  m_swapchain->imageFormat(), w, h);

            m_imagesInFlight.assign(m_swapchain->imageCount(), VK_NULL_HANDLE);
            m_vulkanReady = true;
        } catch (const std::exception& ex) {
            std::cerr << "BrowserTab: Vulkan init failed: " << ex.what()
                      << " — browser requires Vulkan, terminating.\n";
            std::terminate();
        }
    }

    if (m_vulkanReady)
        m_renderTimer.start(16); // ~60 fps
}

void BrowserTab::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    m_renderTimer.stop(); // no GPU work while hidden — saves power
}

bool BrowserTab::nativeEvent(const QByteArray& /*eventType*/,
                              void* /*message*/, qintptr* /*result*/)
{
    return false; // let Qt handle it
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------
void BrowserTab::onRenderFrame()
{
    if (!m_vulkanReady) return;

    auto  dev  = VulkanManager::instance().device();
    const uint32_t frame = m_currentFrame;

    // Wait for this frame's previous work to finish.
    vkWaitForFences(dev, 1, &m_inFlightFences[frame], VK_TRUE, UINT64_MAX);

    auto imgIdx = m_swapchain->acquireNextImage(m_imageAvailableSems[frame]);
    if (!imgIdx.has_value()) {
        handleSwapchainOutOfDate();
        return;
    }
    const uint32_t imageIndex = *imgIdx;

    // If this swapchain image is still in use by a previous frame, wait.
    if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(dev, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    m_imagesInFlight[imageIndex] = m_inFlightFences[frame];

    vkResetFences(dev, 1, &m_inFlightFences[frame]);

    // Re-bind Skia to the correct swapchain image for this frame.
    m_skia.createSurface(m_swapchain->images()[imageIndex],
                          m_swapchain->imageViews()[imageIndex],
                          m_swapchain->imageFormat(),
                          static_cast<uint32_t>(m_swapchain->extent().width),
                          static_cast<uint32_t>(m_swapchain->extent().height));

    // Draw.  PageRenderer (or stub) paints onto the SkCanvas and flushes.
    // For the UI process, we draw a simple gradient to prove the pipeline works.
    if (auto* canvas = m_skia.canvas()) {
        SkPaint p;
        SkPoint pts[2]   = {{0, 0}, {SkScalar(width()), SkScalar(height())}};
        SkColor cols[2]  = {0xFF1a1a2e, 0xFF0f3460};
        p.setShader(SkGradientShader::MakeLinear(pts, cols, nullptr, 2,
                                                  SkTileMode::kClamp));
        canvas->drawPaint(p);
    }

    m_skia.flush(m_imageAvailableSems[frame], m_renderFinishedSems[frame]);

    // Submit a dummy command buffer to carry the fence signal.
    VkCommandBuffer cmd = VulkanManager::instance().beginSingleTimeCommands();
    // (Skia already did the real work; this carries the fence.)
    VulkanManager::instance().endSingleTimeCommands(cmd);

    if (!m_swapchain->present(imageIndex, m_renderFinishedSems[frame])) {
        handleSwapchainOutOfDate();
        return;
    }

    m_currentFrame = (frame + 1) % k_maxFramesInFlight;
}

void BrowserTab::onIpcMessage(QByteArray /*payload*/)
{
    // Decode IPC messages from the render process.
    // (PaintReady, TitleChanged, etc.)
    // Full implementation would decode the IpcMsgType header here.
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------
void BrowserTab::initVulkanSurface()
{
    auto inst = VulkanManager::instance().vkInstance();

#if defined(Q_OS_LINUX)
    // Try to get native Wayland or XCB surface via Qt's platform interface.
    auto* ni = QGuiApplication::platformNativeInterface();

    void* waylandSurface = ni->nativeResourceForWindow("surface", windowHandle());
    void* waylandDisplay = ni->nativeResourceForIntegration("wl_display");

    if (waylandSurface && waylandDisplay) {
        VkWaylandSurfaceCreateInfoKHR ci{};
        ci.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
        ci.display = static_cast<struct wl_display*>(waylandDisplay);
        ci.surface = static_cast<struct wl_surface*>(waylandSurface);
        auto fn = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
            vkGetInstanceProcAddr(inst, "vkCreateWaylandSurfaceKHR"));
        if (!fn || fn(inst, &ci, nullptr, &m_surface) != VK_SUCCESS)
            throw std::runtime_error("BrowserTab: Wayland surface creation failed");
    } else {
        // Fall back to XCB.
        void* xcbConn = ni->nativeResourceForIntegration("connection");
        WId xid = winId();
        VkXcbSurfaceCreateInfoKHR ci{};
        ci.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        ci.connection = static_cast<xcb_connection_t*>(xcbConn);
        ci.window     = static_cast<xcb_window_t>(xid);
        auto fn = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
            vkGetInstanceProcAddr(inst, "vkCreateXcbSurfaceKHR"));
        if (!fn || fn(inst, &ci, nullptr, &m_surface) != VK_SUCCESS)
            throw std::runtime_error("BrowserTab: XCB surface creation failed");
    }
#elif defined(Q_OS_WIN)
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandle(nullptr);
    ci.hwnd      = reinterpret_cast<HWND>(winId());
    auto fn = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(inst, "vkCreateWin32SurfaceKHR"));
    if (!fn || fn(inst, &ci, nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("BrowserTab: Win32 surface creation failed");
#elif defined(Q_OS_MACOS)
    // Requires MoltenVK
    VkMetalSurfaceCreateInfoEXT ci{};
    ci.sType  = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    ci.pLayer = reinterpret_cast<const CAMetalLayer*>(
        QGuiApplication::platformNativeInterface()
            ->nativeResourceForWindow("nsview", windowHandle()));
    auto fn = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
        vkGetInstanceProcAddr(inst, "vkCreateMetalSurfaceEXT"));
    if (!fn || fn(inst, &ci, nullptr, &m_surface) != VK_SUCCESS)
        throw std::runtime_error("BrowserTab: Metal surface creation failed");
#else
    throw std::runtime_error("BrowserTab: unsupported platform for Vulkan surface creation");
#endif
}

void BrowserTab::initSyncObjects()
{
    auto dev = VulkanManager::instance().device();

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo     fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                               nullptr, VK_FENCE_CREATE_SIGNALED_BIT};

    for (uint32_t i = 0; i < k_maxFramesInFlight; ++i) {
        if (vkCreateSemaphore(dev, &sci, nullptr, &m_imageAvailableSems[i]) != VK_SUCCESS ||
            vkCreateSemaphore(dev, &sci, nullptr, &m_renderFinishedSems[i]) != VK_SUCCESS ||
            vkCreateFence(dev, &fci, nullptr, &m_inFlightFences[i]) != VK_SUCCESS)
            throw std::runtime_error("BrowserTab: failed to create sync objects");
    }
}

void BrowserTab::destroySyncObjects() noexcept
{
    auto dev = VulkanManager::instance().device();
    for (uint32_t i = 0; i < k_maxFramesInFlight; ++i) {
        if (m_imageAvailableSems[i]) vkDestroySemaphore(dev, m_imageAvailableSems[i], nullptr);
        if (m_renderFinishedSems[i]) vkDestroySemaphore(dev, m_renderFinishedSems[i], nullptr);
        if (m_inFlightFences[i])     vkDestroyFence(dev, m_inFlightFences[i], nullptr);
        m_imageAvailableSems[i] = VK_NULL_HANDLE;
        m_renderFinishedSems[i] = VK_NULL_HANDLE;
        m_inFlightFences[i]     = VK_NULL_HANDLE;
    }
}

void BrowserTab::handleSwapchainOutOfDate()
{
    auto dev = VulkanManager::instance().device();
    vkDeviceWaitIdle(dev);
    m_skia.destroySurface();

    const uint32_t w = static_cast<uint32_t>(width());
    const uint32_t h = static_cast<uint32_t>(height());
    if (w == 0 || h == 0) return;

    m_swapchain->recreate(w, h);
    m_imagesInFlight.assign(m_swapchain->imageCount(), VK_NULL_HANDLE);
}

} // namespace vkb
