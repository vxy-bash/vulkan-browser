#include "browser/BrowserWindow.h"
#include "vulkan/VulkanManager.h"

#include <QApplication>
#include <QSurfaceFormat>
#include <QCommandLineParser>
#include <iostream>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Probe surface helper
// Asks Qt to create a temporary invisible window so we can construct a
// VkSurfaceKHR for device selection before any BrowserTab is visible.
// ---------------------------------------------------------------------------
#if defined(Q_OS_LINUX)
#  include <vulkan/vulkan_xcb.h>
#  include <qpa/qplatformnativeinterface.h>
#  include <QWindow>
static VkSurfaceKHR createProbeSurface(VkInstance inst)
{
    QWindow probe;
    probe.setSurfaceType(QSurface::VulkanSurface);
    probe.create();

    auto* ni = QGuiApplication::platformNativeInterface();
    void* xcbConn = ni->nativeResourceForIntegration("connection");
    VkXcbSurfaceCreateInfoKHR ci{};
    ci.sType      = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
    ci.connection = static_cast<xcb_connection_t*>(xcbConn);
    ci.window     = static_cast<xcb_window_t>(probe.winId());

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    auto fn = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
        vkGetInstanceProcAddr(inst, "vkCreateXcbSurfaceKHR"));
    if (fn) fn(inst, &ci, nullptr, &surface);
    return surface;
}
#elif defined(Q_OS_WIN)
#  include <vulkan/vulkan_win32.h>
#  include <windows.h>
static VkSurfaceKHR createProbeSurface(VkInstance inst)
{
    VkWin32SurfaceCreateInfoKHR ci{};
    ci.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    ci.hinstance = GetModuleHandle(nullptr);
    ci.hwnd      = GetDesktopWindow();
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    auto fn = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        vkGetInstanceProcAddr(inst, "vkCreateWin32SurfaceKHR"));
    if (fn) fn(inst, &ci, nullptr, &surface);
    return surface;
}
#else
static VkSurfaceKHR createProbeSurface(VkInstance) { return VK_NULL_HANDLE; }
#endif

int main(int argc, char** argv)
{
    // Vulkan surfaces cannot be accelerated through Qt's OpenGL path.
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::NoAPI);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    app.setApplicationName("VulkanBrowser");
    app.setApplicationVersion("0.1.0");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"render-process", "Launch as a render-process child."});
    parser.addOption({"ipc-socket",     "Socket name for IPC (render-process mode).", "name"});
    parser.process(app);

    if (parser.isSet("render-process")) {
        // Render-process mode: connect back to the UI process over IPC,
        // run a headless Vulkan/Skia rendering loop.
        std::cerr << "[render-process] Started, socket="
                  << parser.value("ipc-socket").toStdString() << '\n';
        // Full render-process event loop would be here.
        return app.exec();
    }

    // -----------------------------------------------------------------------
    // UI process — initialize shared Vulkan device, then show BrowserWindow.
    // -----------------------------------------------------------------------
    const char* instanceExtensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(Q_OS_LINUX)
        VK_KHR_XCB_SURFACE_EXTENSION_NAME,
        "VK_KHR_wayland_surface",
#elif defined(Q_OS_WIN)
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#elif defined(Q_OS_MACOS)
        VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#endif
    };

    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

    auto& vm = vkb::VulkanManager::instance();

    // We need a minimal VkInstance before we can create the probe surface.
    try {
        vm.initialize(instanceExtensions, deviceExtensions, VK_NULL_HANDLE);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }

    // Now that we have the instance, build a probe surface to pick the GPU.
    VkSurfaceKHR probe = createProbeSurface(vm.vkInstance());
    if (probe == VK_NULL_HANDLE) {
        std::cerr << "Fatal: could not create probe surface for GPU selection.\n";
        vm.shutdown();
        return EXIT_FAILURE;
    }

    // Re-initialize with the real probe surface so physical device selection
    // can verify present support.  shutdown() + initialize() is idempotent.
    vm.shutdown();
    try {
        vm.initialize(instanceExtensions, deviceExtensions, probe);
    } catch (const std::exception& ex) {
        std::cerr << "Fatal: " << ex.what() << '\n';
        return EXIT_FAILURE;
    }
    vkDestroySurfaceKHR(vm.vkInstance(), probe, nullptr);

    vkb::BrowserWindow window;
    window.show();

    const int rc = app.exec();
    vm.shutdown();
    return rc;
}
