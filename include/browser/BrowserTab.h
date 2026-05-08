#pragma once

#include "vulkan/VulkanSwapchain.h"
#include "renderer/SkiaVulkanContext.h"

#include <QWidget>
#include <QString>
#include <QUrl>
#include <QTimer>

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <array>
#include <cstdint>

namespace vkb {

class PageRenderer;   // forward — wraps LibWeb/Ultralight paint dispatch
class IpcChannel;     // forward — render-process IPC

// Maximum frames in flight (double-buffered).
inline constexpr uint32_t k_maxFramesInFlight = 2;

// BrowserTab owns one VkSurfaceKHR, one VulkanSwapchain, one SkiaVulkanContext,
// and per-frame synchronisation primitives.
// It is a QWidget so Qt's tab bar can embed it directly.
class BrowserTab final : public QWidget {
    Q_OBJECT

public:
    explicit BrowserTab(QWidget* parent = nullptr);
    ~BrowserTab() override;

    BrowserTab(const BrowserTab&)            = delete;
    BrowserTab& operator=(const BrowserTab&) = delete;

    // Navigate to URL — dispatches to the render process via IPC.
    void navigate(const QUrl& url);

    [[nodiscard]] QUrl     currentUrl()   const noexcept { return m_currentUrl; }
    [[nodiscard]] QString  title()        const noexcept { return m_title; }
    [[nodiscard]] bool     isLoading()    const noexcept { return m_loading; }

signals:
    void titleChanged(const QString& title);
    void urlChanged(const QUrl& url);
    void loadStarted();
    void loadFinished(bool success);
    void faviconReady();

protected:
    // QWidget overrides — we paint exclusively through Vulkan, so Qt's
    // paint engine is disabled.  Only resize/expose events matter.
    void paintEvent(QPaintEvent* event)        override;
    void resizeEvent(QResizeEvent* event)      override;
    void showEvent(QShowEvent* event)          override;
    void hideEvent(QHideEvent* event)          override;

    // Raw native event needed to get the Vulkan surface handle from Qt.
    bool nativeEvent(const QByteArray& eventType,
                     void*             message,
                     qintptr*          result) override;

private slots:
    void onRenderFrame();          // connected to m_renderTimer
    void onIpcMessage(QByteArray); // receive painted pixel data / commands

private:
    void initVulkanSurface();
    void initSyncObjects();
    void destroySyncObjects() noexcept;
    void recordAndSubmit(uint32_t imageIndex);
    void handleSwapchainOutOfDate();

    // Vulkan surface/swapchain owned per-tab
    VkSurfaceKHR                       m_surface{VK_NULL_HANDLE};
    std::unique_ptr<VulkanSwapchain>   m_swapchain;
    SkiaVulkanContext                  m_skia;

    // Per-frame sync
    std::array<VkSemaphore, k_maxFramesInFlight> m_imageAvailableSems{};
    std::array<VkSemaphore, k_maxFramesInFlight> m_renderFinishedSems{};
    std::array<VkFence,     k_maxFramesInFlight> m_inFlightFences{};
    std::vector<VkFence>                         m_imagesInFlight; // one per swapchain image
    uint32_t                                     m_currentFrame{0};

    // Render process IPC
    std::unique_ptr<IpcChannel> m_ipc;

    // Browser state
    QUrl    m_currentUrl;
    QString m_title;
    bool    m_loading{false};
    bool    m_vulkanReady{false};

    // Drive rendering at ~60 fps; paused when tab is hidden.
    QTimer  m_renderTimer;
};

} // namespace vkb
