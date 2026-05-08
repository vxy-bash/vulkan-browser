# Building VulkanBrowser

## Prerequisites

| Dependency   | Version   | Notes                                         |
|-------------|-----------|-----------------------------------------------|
| CMake        | ≥ 3.25    |                                               |
| Clang/GCC    | C++20     | Clang 16+ recommended for `std::span`         |
| Vulkan SDK   | ≥ 1.3     | https://vulkan.lunarg.com/                    |
| Qt6          | ≥ 6.5     | Widgets, Network, Gui modules                 |
| Skia         | HEAD      | Built with Vulkan backend (no GL, no EGL)     |
| libxcb       | any       | Linux/X11 surface creation                    |
| libwayland   | any       | Linux/Wayland surface creation (optional)     |
| glslc        | any       | Shader compilation (part of Vulkan SDK)       |

---

## Step 1 — Build Skia with Vulkan

```bash
git clone https://skia.googlesource.com/skia.git
cd skia
python3 tools/git-sync-deps

bin/gn gen out/Release --args='
  is_official_build   = true
  skia_use_vulkan     = true
  skia_use_gl         = false
  skia_use_egl        = false
  skia_use_x11        = false
  skia_enable_skottie = false
  cc  = "clang"
  cxx = "clang++"
  extra_cflags = ["-std=c++20"]
'

ninja -C out/Release skia skshaper
```

---

## Step 2 — Build VulkanBrowser

```bash
git clone https://github.com/vxy-bash/vulkan-browser.git
cd vulkan-browser
mkdir build && cd build

cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DSKIA_DIR=/path/to/skia \
  -DSKIA_LIB_DIR=/path/to/skia/out/Release

cmake --build . -j$(nproc)
```

### With Ultralight HTML engine

```bash
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DSKIA_DIR=/path/to/skia \
  -DUSE_ULTRALIGHT=ON \
  -DULTRALIGHT_SDK_DIR=/path/to/ultralight-sdk
```

---

## Step 3 — Run

```bash
./vulkan-browser
```

The browser **terminates** if no Vulkan-capable GPU is found.  
There is no OpenGL/EGL fallback — this is by design.

---

## Debug build (with ASAN + validation layers)

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DSKIA_DIR=/path/to/skia
cmake --build . -j$(nproc)
VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./vulkan-browser
```

---

## Architecture overview

```
UI Process
├── BrowserWindow (QMainWindow)
│   └── QTabWidget
│       └── BrowserTab (QWidget, WA_PaintOnScreen)
│           ├── VkSurfaceKHR        (one per tab)
│           ├── VulkanSwapchain     (recreated on resize, never re-inits device)
│           └── SkiaVulkanContext   (GrDirectContext wraps shared VkDevice)
│               └── SkSurface       (backed by swapchain VkImage)
│
├── VulkanManager (Singleton)
│   └── shared VkInstance + VkDevice + VkQueues
│
└── IpcChannel → Render Process (child)
    └── PageRenderer
        ├── Ultralight/LibWeb (HTML engine) — optional
        └── SkiaVulkanContext (same Vulkan device, Skia paints HTML)
```

## Shader compilation

SPIR-V shaders are compiled at build time by CMake if `glslc` is found:

```
shaders/blit.vert → build/shaders/blit.vert.spv
shaders/blit.frag → build/shaders/blit.frag.spv
```

These implement the full-screen blit pipeline used to composite the
Skia-rendered texture onto the swapchain image.
