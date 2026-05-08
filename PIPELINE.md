# Rendering Pipeline — VulkanBrowser

Vollständige technische Dokumentation der Rendering-Pipeline von Applikationsstart
bis zum letzten Pixel auf dem Bildschirm.

---

## Inhaltsverzeichnis

1. [Übersicht](#1-übersicht)
2. [Phase 0 — Applikationsstart & Vulkan-Initialisierung](#2-phase-0--applikationsstart--vulkan-initialisierung)
3. [Phase 1 — Tab-Initialisierung](#3-phase-1--tab-initialisierung)
4. [Phase 2 — Pro-Frame-Render-Loop](#4-phase-2--pro-frame-render-loop)
5. [Phase 3 — Skia-interne Vulkan-Kommandos](#5-phase-3--skia-interne-vulkan-kommandos)
6. [Phase 4 — Swapchain Present](#6-phase-4--swapchain-present)
7. [Resize & Tab-Switch](#7-resize--tab-switch)
8. [Multi-Process IPC](#8-multi-process-ipc)
9. [Synchronisations-Modell](#9-synchronisations-modell)
10. [Speicher-Layout](#10-speicher-layout)
11. [Shader-Pipeline](#11-shader-pipeline)
12. [Fehlerbehandlung & Terminierungsstrategie](#12-fehlerbehandlung--terminierungsstrategie)
13. [Vollständiges Sequenzdiagramm](#13-vollständiges-sequenzdiagramm)

---

## 1. Übersicht

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          VulkanBrowser — Architektur                     │
│                                                                          │
│   UI-Prozess                          Render-Prozess (je Tab, optional)  │
│  ┌─────────────────────────┐         ┌────────────────────────────────┐  │
│  │  BrowserWindow          │  IPC    │  PageRenderer                  │  │
│  │  └─ QTabWidget          │◄───────►│  ├─ Ultralight/LibWeb (HTML)   │  │
│  │      └─ BrowserTab      │         │  └─ SkiaVulkanContext           │  │
│  │          ├─ VkSurface   │         │      └─ GrDirectContext         │  │
│  │          ├─ VkSwapchain │         └────────────────────────────────┘  │
│  │          └─ SkiaSurface │                                             │
│  └─────────────────────────┘                                             │
│                                                                          │
│   VulkanManager (Singleton)                                              │
│   ├─ VkInstance      (eine, global)                                      │
│   ├─ VkPhysicalDevice (ausgewählt beim Start)                            │
│   ├─ VkDevice        (eine, von allen Tabs geteilt)                      │
│   ├─ VkQueue         (Graphics + Present + Transfer)                     │
│   └─ VkCommandPool   (Graphics + Transfer)                               │
└──────────────────────────────────────────────────────────────────────────┘
```

**Kernprinzip:** Ein einziges `VkDevice` wird von allen Tabs geteilt.
Jeder Tab besitzt eine eigene `VkSurfaceKHR`, einen eigenen `VkSwapchainKHR`
und eine eigene `SkSurface`. Kein OpenGL, kein EGL — bei Vulkan-Fehler terminiert
der Browser hart.

---

## 2. Phase 0 — Applikationsstart & Vulkan-Initialisierung

### 2.1 main()

```
main(argc, argv)
  │
  ├─ QSurfaceFormat::setDefaultFormat(NoAPI)
  │     Qt darf keinen OpenGL-Kontext anlegen.
  │
  ├─ QApplication::exec() Infrastruktur starten
  │
  ├─ VulkanManager::initialize(instanceExtensions, deviceExtensions, probeSurface)
  │
  └─ BrowserWindow::show()
```

### 2.2 VkInstance anlegen

```cpp
vkCreateInstance({
    apiVersion     = VK_API_VERSION_1_3,
    extensions     = [VK_KHR_SURFACE, VK_KHR_XCB_SURFACE,        // Linux X11
                      VK_KHR_WAYLAND_SURFACE,                     // Linux Wayland
                      VK_EXT_DEBUG_UTILS],                        // nur Debug
    layers         = ["VK_LAYER_KHRONOS_validation"]              // nur Debug
})
```

Die Instance ist global — sie wird **nie** pro Tab erzeugt. Das wäre
funktional korrekt, aber ineffizient und von keinem Treiber optimiert.

### 2.3 Debug Messenger (nur Debug-Build)

```cpp
vkCreateDebugUtilsMessengerEXT({
    messageSeverity = WARNING | ERROR,
    messageType     = GENERAL | VALIDATION | PERFORMANCE,
    pfnUserCallback = debugCallback   // gibt Fehlermeldungen auf stderr aus
})
```

### 2.4 GPU-Auswahl

```
vkEnumeratePhysicalDevices() → Liste aller GPUs

Für jede GPU:
  ├─ vkGetPhysicalDeviceQueueFamilyProperties()
  │    → muss Graphics-Queue + Present-Queue besitzen
  ├─ vkEnumerateDeviceExtensionProperties()
  │    → muss VK_KHR_swapchain unterstützen
  ├─ vkGetPhysicalDeviceSurfaceFormatsKHR()
  │    → muss ≥1 Surface-Format haben
  └─ vkGetPhysicalDeviceSurfacePresentModesKHR()
       → muss ≥1 Present-Mode haben

Score: discrete GPU = 3, integrated = 2, virtual = 1, CPU/other = 0
→ GPU mit höchstem Score wird gewählt.
→ Kein geeignetes Gerät: std::runtime_error → Terminierung.
```

### 2.5 Logisches Device & Queues

```cpp
vkCreateDevice({
    extensions     = [VK_KHR_SWAPCHAIN_EXTENSION_NAME],
    features       = { samplerAnisotropy, fillModeNonSolid },
    queues         = [
        { family = graphicsFamily, count = 1, priority = 1.0 },
        { family = presentFamily,  count = 1, priority = 1.0 },   // ggf. same family
        { family = transferFamily, count = 1, priority = 1.0 }    // dediziert, optional
    ]
})

vkGetDeviceQueue(graphicsFamily) → m_graphicsQueue
vkGetDeviceQueue(presentFamily)  → m_presentQueue
vkGetDeviceQueue(transferFamily) → m_transferQueue   (oder = graphicsQueue)
```

### 2.6 Command Pools

```cpp
// Für persistente Kommandos (Render-Loop)
vkCreateCommandPool({
    queueFamilyIndex = graphicsFamily,
    flags            = RESET_COMMAND_BUFFER_BIT
}) → graphicsCommandPool

// Für kurzlebige Transfer-Ops (Texturen hochladen etc.)
vkCreateCommandPool({
    queueFamilyIndex = transferFamily,
    flags            = TRANSIENT_BIT
}) → transferCommandPool
```

---

## 3. Phase 1 — Tab-Initialisierung

Ausgelöst durch `BrowserTab::showEvent()` beim ersten Einblenden des Tabs.

### 3.1 VkSurfaceKHR anlegen

```
Qt liefert über QGuiApplication::platformNativeInterface() die
nativen Handles des OS-Fensters:

Linux/Wayland:
  wl_display* + wl_surface*
  → vkCreateWaylandSurfaceKHR()

Linux/X11:
  xcb_connection_t* + xcb_window_t (= WId)
  → vkCreateXcbSurfaceKHR()

Windows:
  HINSTANCE + HWND (= WId)
  → vkCreateWin32SurfaceKHR()

macOS (MoltenVK):
  CAMetalLayer*
  → vkCreateMetalSurfaceEXT()
```

`VkSurfaceKHR` ist das einzige Objekt, das pro Tab und pro Plattform
unterschiedlich erzeugt wird. Alles andere ist plattformunabhängig.

### 3.2 Swapchain anlegen

```
VulkanSwapchain::create(width, height)
  │
  ├─ vkGetPhysicalDeviceSurfaceCapabilitiesKHR()
  │    → minImageCount, maxImageCount, currentTransform, …
  │
  ├─ vkGetPhysicalDeviceSurfaceFormatsKHR()
  │    → bevorzugt: VK_FORMAT_B8G8R8A8_SRGB + SRGB_NONLINEAR_KHR
  │
  ├─ vkGetPhysicalDeviceSurfacePresentModesKHR()
  │    → bevorzugt: MAILBOX (triple-buffered, kein Tearing, niedrige Latenz)
  │       Fallback: FIFO (vsync-gebunden, immer verfügbar)
  │
  ├─ vkCreateSwapchainKHR({
  │      minImageCount    = caps.minImageCount + 1,   // meist 3
  │      imageFormat      = B8G8R8A8_SRGB,
  │      imageColorSpace  = SRGB_NONLINEAR_KHR,
  │      imageExtent      = {width, height},
  │      imageUsage       = COLOR_ATTACHMENT | TRANSFER_DST,
  │      imageSharingMode = EXCLUSIVE,                // wenn graphics == present
  │      preTransform     = currentTransform,
  │      compositeAlpha   = OPAQUE,
  │      presentMode      = MAILBOX,
  │      clipped          = VK_TRUE
  │  })
  │
  ├─ vkGetSwapchainImagesKHR()  → std::vector<VkImage> (3 Stück)
  │    Die VkImages gehören dem Treiber — wir dürfen sie nicht zerstören.
  │
  ├─ vkCreateImageView() × N
  │    Für jedes VkImage eine VkImageView:
  │    { viewType=2D, format=B8G8R8A8_SRGB, aspectMask=COLOR }
  │
  ├─ vkCreateRenderPass({
  │      attachment: {
  │          format      = swapchainFormat,
  │          loadOp      = CLEAR,
  │          storeOp     = STORE,
  │          finalLayout = PRESENT_SRC_KHR
  │      },
  │      subpass: { pipelineBindPoint=GRAPHICS, colorAttachment=0 },
  │      dependency: srcSubpass=EXTERNAL → dstSubpass=0,
  │                  srcStage=COLOR_ATTACHMENT_OUTPUT
  │  })
  │
  └─ vkCreateFramebuffer() × N
       Jeder Framebuffer bindet eine ImageView an den RenderPass.
       Größe: {width, height, layers=1}
```

### 3.3 Skia GrDirectContext initialisieren

```
SkiaVulkanContext::initialize()
  │
  └─ GrDirectContext::MakeVulkan(GrVkBackendContext{
         fInstance          = VulkanManager::vkInstance(),
         fPhysicalDevice    = VulkanManager::physicalDevice(),
         fDevice            = VulkanManager::device(),
         fQueue             = VulkanManager::graphicsQueue(),
         fGraphicsQueueIndex= VulkanManager::graphicsFamily(),
         fMaxAPIVersion     = VK_API_VERSION_1_3
     })

Skia legt intern an:
  - Eigene Pipeline-Cache (VkPipelineCache)
  - Descriptor-Pool für Shader-Ressourcen
  - Sampler-Cache
  - Staging-Buffer für CPU→GPU-Uploads (Textur-Daten, Schriftarten)

→ schlägt fehl: std::runtime_error → Terminierung.
   (Kein Software-Rasterizer-Fallback.)
```

### 3.4 Synchronisationsobjekte

```
Pro Frame-Slot (k_maxFramesInFlight = 2):

  vkCreateSemaphore() → imageAvailableSem[0], imageAvailableSem[1]
     GPU signalisiert: "Swapchain-Image ist frei zum Beschreiben"

  vkCreateSemaphore() → renderFinishedSem[0], renderFinishedSem[1]
     GPU signalisiert: "Skia ist fertig, Image darf präsentiert werden"

  vkCreateFence(SIGNALED) → inFlightFence[0], inFlightFence[1]
     CPU wartet: "GPU hat Frame N vollständig abgearbeitet"

  std::vector<VkFence> imagesInFlight[swapchainImageCount]
     Zeiger in inFlightFences — verhindert, dass dasselbe
     Swapchain-Image doppelt beschrieben wird.
```

---

## 4. Phase 2 — Pro-Frame-Render-Loop

Getrieben von `QTimer` (16 ms ≈ 60 fps). Pausiert wenn Tab unsichtbar.

```
BrowserTab::onRenderFrame()
  │
  ├─ [1] CPU SYNCHRONISATION
  │    vkWaitForFences(inFlightFences[currentFrame], timeout=UINT64_MAX)
  │    → CPU blockiert, bis GPU Frame (currentFrame) abgeschlossen hat.
  │    → Ohne diesen Wait würden wir CPU-seitig dem GPU davonlaufen.
  │
  ├─ [2] IMAGE ACQUIRE
  │    vkAcquireNextImageKHR(
  │        swapchain,
  │        timeout         = UINT64_MAX,
  │        semaphore       = imageAvailableSems[currentFrame],
  │        fence           = VK_NULL_HANDLE
  │    ) → imageIndex
  │
  │    Rückgabewerte:
  │      VK_SUCCESS          → imageIndex ist gültig
  │      VK_SUBOPTIMAL_KHR   → imageIndex ist gültig, aber Swapchain
  │                             passt nicht mehr perfekt (Fenster resized)
  │      VK_ERROR_OUT_OF_DATE_KHR → Swapchain ungültig → recreate()
  │
  ├─ [3] IMAGE-IN-FLIGHT-SCHUTZ
  │    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE):
  │        vkWaitForFences(imagesInFlight[imageIndex])
  │    imagesInFlight[imageIndex] = inFlightFences[currentFrame]
  │    → Verhindert, dass ein Image doppelt beschrieben wird, wenn
  │      der Swapchain mehr Images zurückgibt als Frame-Slots existieren.
  │
  ├─ [4] FENCE RESET
  │    vkResetFences(inFlightFences[currentFrame])
  │    → Fence muss manuell zurückgesetzt werden bevor erneuter Submit.
  │
  ├─ [5] SKIA SURFACE BINDEN
  │    SkiaVulkanContext::createSurface(
  │        images[imageIndex],
  │        imageViews[imageIndex],
  │        format, width, height
  │    )
  │    (Details → Phase 3)
  │
  ├─ [6] ZEICHNEN
  │    SkCanvas* canvas = skia.canvas()
  │    PageRenderer::renderFrame(canvas)
  │    (Details → Phase 3)
  │
  ├─ [7] SKIA FLUSH
  │    SkiaVulkanContext::flush(
  │        imageAvailableSems[currentFrame],
  │        renderFinishedSems[currentFrame]
  │    )
  │    (Details → Phase 3 & 5)
  │
  ├─ [8] PRESENT
  │    VulkanSwapchain::present(imageIndex, renderFinishedSems[currentFrame])
  │    (Details → Phase 4)
  │
  └─ [9] FRAME-COUNTER
       currentFrame = (currentFrame + 1) % k_maxFramesInFlight
```

---

## 5. Phase 3 — Skia-interne Vulkan-Kommandos

### 5.1 SkSurface anlegen (pro Frame)

```
GrVkImageInfo imageInfo {
    fImage              = swapchainImages[imageIndex],  // VkImage vom Treiber
    fImageLayout        = COLOR_ATTACHMENT_OPTIMAL,
    fFormat             = VK_FORMAT_B8G8R8A8_SRGB,
    fLevelCount         = 1,
    fCurrentQueueFamily = graphicsFamily
}

GrBackendRenderTarget backendRT(width, height, sampleCnt=1, imageInfo)

SkSurface::MakeFromBackendRenderTarget(
    grCtx,
    backendRT,
    kTopLeft_GrSurfaceOrigin,
    kBGRA_8888_SkColorType,
    SkColorSpace::MakeSRGB(),
    surfaceProps
)
```

**Kein Pixel-Copy:** Skia schreibt direkt in das `VkImage` des Swapchains.
Die `SkSurface` ist ein Wrapper — sie besitzt das Image nicht.

### 5.2 Skia Draw Calls (PageRenderer)

```
Ohne Ultralight (Stub-Renderer):

canvas->drawPaint(gradientShader)     // Hintergrund-Gradient
canvas->drawRRect(urlBarRect)          // URL-Bar-Hintergrund
canvas->drawSimpleText(urlText)        // URL-Text
canvas->drawSimpleText(statusMsg)      // Statusmeldung
canvas->drawArc(spinnerRect)           // Lade-Spinner (animiert)

Jeder draw-Call landet in Skias internem Op-Buffer.
Nichts wird sofort an Vulkan übergeben — das passiert erst beim flush().

Mit Ultralight:
  ultralightRenderer->Update()  // HTML-Engine verarbeitet Events, JS, Layout
  ultralightRenderer->Render()  // Ultralight ruft intern Skia-Draw-Calls auf
                                //  (Text-Rendering, Bézier-Kurven, Bitmaps)
```

### 5.3 Skia flush() — Übersetzung in Vulkan

```
GrDirectContext::flush(GrFlushInfo{
    fNumSemaphores    = 1,
    fSignalSemaphores = [renderFinishedSem]   // GPU signalisiert dieses Sem nach Render
})

Intern baut Skia einen oder mehrere VkCommandBuffer:

  vkBeginCommandBuffer(cmd, ONE_TIME_SUBMIT)

  // Skia sortiert Draw-Ops nach Render-Pass-Kompatibilität und bündelt sie:
  vkCmdBeginRenderPass(cmd, {
      renderPass  = skiaInternalRenderPass,
      framebuffer = skiaInternalFramebuffer,  // wraps swapchain image
      clearValues = [{color: {0,0,0,1}}]
  })

  // Pro Skia-Op (Rect, Path, Text, Bild, …):
  vkCmdBindPipeline()             // Skia wählt passende VkPipeline aus Cache
  vkCmdBindDescriptorSets()       // Texturen, Sampler, UBOs
  vkCmdPushConstants()            // Transform-Matrix, Farbe
  vkCmdDraw() / vkCmdDrawIndexed()

  vkCmdEndRenderPass(cmd)
  vkEndCommandBuffer(cmd)

  vkQueueSubmit(graphicsQueue, {
      waitSemaphores   = [imageAvailableSem],    // warten bis Image frei
      waitDstStageMask = [COLOR_ATTACHMENT_OUTPUT],
      commandBuffers   = [cmd],
      signalSemaphores = [renderFinishedSem],    // signalisieren wenn fertig
      fence            = inFlightFences[currentFrame]
  })
```

Skias interne Pipeline-Auswahl basiert auf:
- Vertex-Format (xy, uv, rgba)
- Blend-Mode (SrcOver, Xor, Multiply, …)
- Shader-Typ (Farbe, Textur, Gradient, Path-Fill, Text-Atlas)
- MSAA-Konfiguration (hier: 1× Sample)

---

## 6. Phase 4 — Swapchain Present

```
VulkanSwapchain::present(imageIndex, renderFinishedSem)
  │
  └─ vkQueuePresentKHR(presentQueue, {
         waitSemaphores = [renderFinishedSem],  // warten bis Render fertig
         swapchains     = [swapchain],
         imageIndices   = [imageIndex]
     })

     Der Treiber übergibt das Image an den Display-Compositor:
       Linux: KWin (Wayland) / Xorg Compositor
       Windows: DWM (Desktop Window Manager)
       macOS: Quartz Compositor (via MoltenVK/Metal)

     Der Compositor entscheidet, wann das Image auf dem Monitor erscheint
     (synchronisiert mit dem Vertical Blank des Displays).

     Rückgabewerte:
       VK_SUCCESS              → alles gut
       VK_SUBOPTIMAL_KHR       → nächster Frame: recreate()
       VK_ERROR_OUT_OF_DATE_KHR→ sofort recreate()
```

---

## 7. Resize & Tab-Switch

### Resize

```
BrowserTab::resizeEvent(newSize)
  │
  └─ handleSwapchainOutOfDate()
       │
       ├─ vkDeviceWaitIdle()            // GPU vollständig stoppen
       ├─ SkiaVulkanContext::destroySurface()  // SkSurface aufgeben
       ├─ VulkanSwapchain::destroy()    // alten Swapchain zerstören
       │    ├─ vkDestroyFramebuffer() × N
       │    ├─ vkDestroyRenderPass()
       │    ├─ vkDestroyImageView()  × N
       │    └─ vkDestroySwapchainKHR()
       │
       └─ VulkanSwapchain::create(newW, newH)  // neuen Swapchain anlegen
            (kompletter Ablauf wie Phase 1.2)

Kein vkDestroyDevice(), kein vkDestroyInstance().
Das VkDevice bleibt über alle Resizes aller Tabs hinweg am Leben.
```

### Tab-Switch

```
Tab wird unsichtbar:
  BrowserTab::hideEvent()
    └─ m_renderTimer.stop()    // QTimer pausieren → kein GPU-Work

Tab wird wieder sichtbar:
  BrowserTab::showEvent()
    └─ m_renderTimer.start(16) // QTimer wieder starten → normaler Loop

Die Swapchain bleibt bestehen — kein Rebuild nötig.
Das Swapchain-Image wird einfach nicht mehr acquired/presented,
solange der Tab versteckt ist.
```

---

## 8. Multi-Process IPC

### Prozessmodell

```
vulkan-browser (UI-Prozess)
  ├─ BrowserWindow + Qt-Event-Loop
  ├─ VulkanManager (Singleton, hält VkDevice)
  └─ pro Tab:
       ├─ BrowserTab (UI-Widget + Swapchain)
       └─ IpcChannel → [QLocalSocket] → Render-Prozess

vulkan-browser --render-process --ipc-socket=<name> (Render-Prozess)
  ├─ PageRenderer
  │    ├─ Ultralight-Engine (HTML/CSS/JS)
  │    └─ SkiaVulkanContext (eigene GrDirectContext-Instanz)
  └─ IpcChannel (verbindet sich zurück zum UI-Prozess)
```

### Framing-Protokoll (IpcChannel)

```
Nachrichtenformat (binär, little-endian):

  ┌──────────────────────────────────────────────┐
  │ IpcHeader (5 Bytes)                          │
  │  ├─ type       : uint8_t  (IpcMsgType enum)  │
  │  └─ payloadLen : uint32_t (Bytes nach Header)│
  └──────────────────────────────────────────────┘
  ┌──────────────────────────────────────────────┐
  │ Payload (payloadLen Bytes)                   │
  └──────────────────────────────────────────────┘

Nachrichten-Typen:
  0x01 Navigate     UI→Render   UTF-8 URL-String
  0x02 PaintReady   Render→UI   Shared-Memory-Handle + Größe
  0x03 TitleChanged Render→UI   UTF-8 Seiten-Titel
  0x04 UrlChanged   Render→UI   UTF-8 aktuelle URL (nach Redirect)
  0x05 LoadStarted  Render→UI   leer
  0x06 LoadFinished Render→UI   1 Byte: 0x01=Erfolg, 0x00=Fehler
  0x07 Resize       UI→Render   8 Bytes: uint32_t w + uint32_t h
  0xFF Shutdown     UI→Render   leer → Render-Prozess beendet sich
```

### Shared-Memory Pixel-Transfer

```
Render-Prozess:
  1. Skia flush() → Pixel in VkImage
  2. vkCmdCopyImageToBuffer() → Staging-Buffer
  3. Staging-Buffer gemappt → in Shared-Memory-Segment kopieren
  4. IPC: PaintReady(shmHandle, width, height)

UI-Prozess:
  1. PaintReady empfangen
  2. Shared-Memory öffnen
  3. Pixel in eigene VkImage per vkCmdCopyBufferToImage() hochladen
  4. Nächster Frame: diese VkImage als Textur via Blit-Pipeline darstellen
```

---

## 9. Synchronisations-Modell

### Doppelpuffer-Schema (k_maxFramesInFlight = 2)

```
Frame-Slot 0:  imageAvailableSem[0]  renderFinishedSem[0]  inFlightFence[0]
Frame-Slot 1:  imageAvailableSem[1]  renderFinishedSem[1]  inFlightFence[1]

GPU-Timeline (Swapchain mit 3 Images: A, B, C):

Zeit →   ──────────────────────────────────────────────────────────────────
Slot 0:  [Render A]─────────────[Render C]──────────────
Slot 1:       [Render B]─────────────[Render D]──────────
Present:           [A]    [B]    [C]    [D]
         ──────────────────────────────────────────────────────────────────

CPU:
  Slot 0 gestartet →
    warte auf inFlightFence[1] (C noch nicht gestartet)  →
    starte Slot 0 für C
```

### Pipeline-Barrieren (innerhalb Skia)

```
VkImage Zustandsübergänge (automatisch durch Skia verwaltet):

UNDEFINED
  │ (Swapchain erstellt)
  ▼
COLOR_ATTACHMENT_OPTIMAL     ← Skia schreibt hier
  │ (vkCmdEndRenderPass)
  ▼
PRESENT_SRC_KHR              ← Treiber präsentiert hier
  │ (nächstes vkAcquireNextImageKHR)
  ▼
COLOR_ATTACHMENT_OPTIMAL     ← Skia schreibt wieder

Barriere: srcStage=COLOR_ATTACHMENT_OUTPUT
          dstStage=COLOR_ATTACHMENT_OUTPUT
          srcAccess=COLOR_ATTACHMENT_WRITE
          dstAccess=COLOR_ATTACHMENT_READ|WRITE
```

---

## 10. Speicher-Layout

### GPU-Speicher pro Tab

```
VkDeviceMemory Allokationen (geschätzt bei 1920×1080):

Swapchain Images (3×):
  1920 × 1080 × 4 Bytes (BGRA8) × 3 = ~23 MB
  → DEVICE_LOCAL (GPU-VRAM, nicht mappbar)

Skia Interner Vertex-Buffer:
  ~1–4 MB je nach Komplexität der Seite
  → DEVICE_LOCAL

Skia Glyph-Atlas (Schriftarten):
  2048 × 2048 × 1 Byte (Alpha) = ~4 MB
  → DEVICE_LOCAL

Skia Staging-Buffer (CPU→GPU Uploads):
  ~1–8 MB (temporär, wird wiederverwendet)
  → HOST_VISIBLE | HOST_COHERENT

Gesamt pro Tab: ~30–40 MB GPU-VRAM
```

### Speicher-Typen

```
VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
  Schnellster GPU-Zugriff. CPU kann nicht direkt lesen/schreiben.
  Verwendet für: Swapchain Images, Render-Targets, Textur-Atlas.

VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | HOST_COHERENT_BIT
  CPU kann direkt lesen/schreiben (kein flush() nötig).
  Langsamer GPU-Zugriff. Verwendet für: Staging-Buffer.

findMemoryType(typeFilter, propertyFlags):
  Iteriert vkGetPhysicalDeviceMemoryProperties().memoryTypes
  und findet den ersten passenden Heap-Typ.
```

---

## 11. Shader-Pipeline

### Blit-Pipeline (UI-Prozess → Swapchain)

Verwendet für den Render-Prozess-Modus, wo Pixel via Shared Memory
übertragen wurden und als Textur auf den Swapchain geblit werden.

**Vertex-Shader** (`shaders/blit.vert`):
```glsl
// Full-Screen Triangle Trick — kein Vertex-Buffer nötig.
// gl_VertexIndex 0,1,2 deckt den gesamten NDC-Viewport ab.

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    fragUV  = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}

// Erzeugte Dreiecks-Koordinaten:
// Vertex 0: uv=(0,0)  pos=(-1,-1)  ← links oben
// Vertex 1: uv=(2,0)  pos=( 3,-1)  ← rechts außen (abgeschnitten)
// Vertex 2: uv=(0,2)  pos=(-1, 3)  ← unten außen  (abgeschnitten)
// → Das Dreieck deckt garantiert den gesamten Viewport ab.
```

**Fragment-Shader** (`shaders/blit.frag`):
```glsl
layout(set=0, binding=0) uniform sampler2D pageTexture;

void main() {
    outColor = texture(pageTexture, fragUV);
}
```

**Kompilierung** (CMake, automatisch):
```bash
glslc shaders/blit.vert -o build/shaders/blit.vert.spv
glslc shaders/blit.frag -o build/shaders/blit.frag.spv
```

### Skia-interne Shader

Skia kompiliert seine eigenen GLSL/SPIR-V-Shader zur Laufzeit beim ersten
Gebrauch und cached sie in `VkPipelineCache`. Typen:

| Shader-Typ          | Verwendung                          |
|---------------------|-------------------------------------|
| SolidColorShader    | `canvas->drawRect(paint)`           |
| LinearGradientShader| `SkGradientShader::MakeLinear()`    |
| TextureShader       | Bilder, Bitmaps                     |
| BitmapTextShader    | Glyph-Rendering via Atlas           |
| PathFillShader      | Komplexe Vektorpfade (SVG, Bézier)  |
| BlurShader          | `SkImageFilter` (Box-Blur etc.)     |

---

## 12. Fehlerbehandlung & Terminierungsstrategie

```
Designentscheidung: Kein OpenGL/EGL-Fallback.
Wenn Vulkan nicht verfügbar ist, ist der Browser nutzlos.
Statt degradierter Funktionalität: sofortige Terminierung mit klarer Fehlermeldung.
```

| Fehlerfall                                | Reaktion                                     |
|-------------------------------------------|----------------------------------------------|
| `vkCreateInstance` schlägt fehl           | `throw std::runtime_error` → `EXIT_FAILURE`  |
| Kein geeignetes GPU gefunden              | `throw std::runtime_error` → `EXIT_FAILURE`  |
| `vkCreateDevice` schlägt fehl             | `throw std::runtime_error` → `EXIT_FAILURE`  |
| `GrDirectContext::MakeVulkan` schlägt fehl| `throw std::runtime_error` → `std::terminate`|
| `SkSurface::Make...` schlägt fehl         | `throw std::runtime_error` → `std::terminate`|
| `vkCreateSwapchainKHR` schlägt fehl       | `throw std::runtime_error` → `std::terminate`|
| `VK_ERROR_OUT_OF_DATE_KHR` bei acquire    | `handleSwapchainOutOfDate()` → recreate      |
| `VK_ERROR_OUT_OF_DATE_KHR` bei present    | `handleSwapchainOutOfDate()` → recreate      |
| Render-Prozess crasht (IPC disconnect)    | Tab zeigt Fehlerseite, kein Browser-Crash    |
| Validation Layer meldet Error             | `stderr` ausgabe, kein Crash (nur Debug)     |

---

## 13. Vollständiges Sequenzdiagramm

```
  main()          VulkanManager    BrowserWindow   BrowserTab      VulkanSwapchain  SkiaVulkanCtx  PageRenderer
    │                   │                │               │                │                │              │
    │──initialize()────►│               │               │                │                │              │
    │   vkCreateInstance│               │               │                │                │              │
    │   pickGPU         │               │               │                │                │              │
    │   vkCreateDevice  │               │               │                │                │              │
    │   vkGetQueues     │               │               │                │                │              │
    │◄──────────────────│               │               │                │                │              │
    │                   │               │               │                │                │              │
    │──new BrowserWindow────────────────►│               │                │                │              │
    │                   │  openTab()    │               │                │                │              │
    │                   │──────────────►│──new BrowserTab──────────────►│                │              │
    │                   │               │               │                │                │              │
    │                   │               │  show()       │                │                │              │
    │                   │               │──────────────►│                │                │              │
    │                   │               │  initVulkanSurface()           │                │              │
    │                   │               │  vkCreateXcbSurface────────────────────────────►│              │
    │                   │               │               │                │                │              │
    │                   │               │  new VulkanSwapchain──────────►│                │              │
    │                   │               │               │  create(w,h)  │                │              │
    │                   │               │               │  vkCreateSwapchainKHR          │              │
    │                   │               │               │  vkCreateImageView×N           │              │
    │                   │               │               │  vkCreateRenderPass            │              │
    │                   │               │               │  vkCreateFramebuffer×N         │              │
    │                   │               │               │◄───────────────│                │              │
    │                   │               │               │                │                │              │
    │                   │               │  skia.initialize()─────────────────────────────►│              │
    │                   │               │               │                │  MakeVulkan()  │              │
    │                   │               │               │◄───────────────────────────────│              │
    │                   │               │               │                │                │              │
    │                   │               │  initSyncObjects()             │                │              │
    │                   │               │  vkCreateSemaphore×4          │                │              │
    │                   │               │  vkCreateFence×2              │                │              │
    │                   │               │               │                │                │              │
    │                   │               │  renderTimer.start(16ms)       │                │              │
    │                   │               │               │                │                │              │
    │    ═══════════════ PRO FRAME (alle 16ms) ══════════════════════════════════════════════════════   │
    │                   │               │               │                │                │              │
    │                   │               │  onRenderFrame()               │                │              │
    │                   │               │  vkWaitForFences               │                │              │
    │                   │               │  vkAcquireNextImageKHR────────►│                │              │
    │                   │               │               │◄───imageIndex──│                │              │
    │                   │               │               │                │                │              │
    │                   │               │  skia.createSurface(img[i])────────────────────►│              │
    │                   │               │               │                │  MakeFromBackendRT            │
    │                   │               │               │◄───────────────────────────────│              │
    │                   │               │               │                │                │              │
    │                   │               │  pageRenderer.renderFrame()────────────────────────────────►  │
    │                   │               │               │                │                │  drawCalls() │
    │                   │               │               │◄───────────────────────────────────────────── │
    │                   │               │               │                │                │              │
    │                   │               │  skia.flush(availSem,doneSem)──────────────────►│              │
    │                   │               │               │                │  vkQueueSubmit │              │
    │                   │               │               │                │  (wait:avail   │              │
    │                   │               │               │                │   signal:done  │              │
    │                   │               │               │                │   fence:inflight)             │
    │                   │               │               │◄───────────────────────────────│              │
    │                   │               │               │                │                │              │
    │                   │               │  swapchain.present(i, doneSem)►│                │              │
    │                   │               │               │  vkQueuePresentKHR              │              │
    │                   │               │               │  (wait: doneSem)                │              │
    │                   │               │               │  → Compositor → Monitor         │              │
    │                   │               │               │◄───────────────│                │              │
    │                   │               │  currentFrame = (currentFrame+1) % 2            │              │
    │    ═══════════════════════════════════════════════════════════════════════════════════════════════│
```

---

## Zusammenfassung der Datenflüsse

```
HTML/URL
  ↓
PageRenderer (Ultralight oder Stub)
  ↓ Skia Draw Calls
SkCanvas (auf SkSurface)
  ↓ GrDirectContext::flush()
VkCommandBuffer (erzeugt von Skia)
  ↓ vkQueueSubmit(waitSem=imageAvailable, signalSem=renderFinished)
GPU → VkImage (Swapchain)
  ↓ vkQueuePresentKHR(waitSem=renderFinished)
Display Compositor
  ↓ VSync
Monitor
```

Jede Ebene kommuniziert ausschließlich über definierte Vulkan-Objekte.
Es gibt keine Shared-Pointer zurück zur oberen Ebene während eines Frames,
keine globalen Locks außer dem Submit-Mutex in VulkanManager,
und keine GPU-CPU-Synchronisation außer den expliziten Fences und Semaphoren.
