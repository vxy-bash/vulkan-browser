#pragma once

#include "renderer/SkiaVulkanContext.h"
#include "ipc/IpcChannel.h"

#include <QObject>
#include <QUrl>
#include <QString>
#include <memory>

// Forward-declare LibWeb/Ultralight types conditionally.
// When built with -DUSE_ULTRALIGHT, we link Ultralight for HTML rendering.
// Otherwise a stub renderer is used so the Vulkan/Skia pipeline can be tested
// independently of a browser engine.
#ifdef USE_ULTRALIGHT
#  include <Ultralight/Ultralight.h>
#endif

namespace vkb {

// PageRenderer runs inside the render process.
// It receives navigation commands over IPC, drives the HTML engine,
// and paints each frame onto an SkCanvas via SkiaVulkanContext.
// The resulting VkImage is signalled back to the UI process via IPC.
class PageRenderer final : public QObject {
    Q_OBJECT

public:
    explicit PageRenderer(SkiaVulkanContext* skia, QObject* parent = nullptr);
    ~PageRenderer() override;

    // Connect to the UI process IPC channel.
    bool connectToUiProcess(const QString& socketName);

    // Called once to attach the Skia surface (after swapchain creation).
    void setSurface(SkSurface* surface);

    // Drive one frame: poll HTML engine, issue Skia draw calls, flush.
    void renderFrame(VkSemaphore imageAvail, VkSemaphore renderDone);

public slots:
    void navigate(const QUrl& url);
    void resize(uint32_t width, uint32_t height);

signals:
    void titleChanged(const QString& title);
    void urlChanged(const QUrl& url);
    void loadStarted();
    void loadFinished(bool success);

private:
    void drawPlaceholderPage(SkCanvas* canvas, uint32_t w, uint32_t h);
    void drawLoadingSpinner(SkCanvas* canvas, uint32_t w, uint32_t h, float t);

#ifdef USE_ULTRALIGHT
    ultralight::RefPtr<ultralight::Renderer> m_ultralightRenderer;
    ultralight::RefPtr<ultralight::View>     m_view;
#endif

    SkiaVulkanContext* m_skia{nullptr};  // not owned
    SkSurface*         m_surface{nullptr}; // not owned
    QUrl               m_pendingUrl;
    bool               m_loading{false};
    float              m_spinnerAngle{0.0f};
    uint32_t           m_width{0};
    uint32_t           m_height{0};
};

} // namespace vkb
