cmake_minimum_required(VERSION 3.25)
project(VulkanBrowser VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# Prefer position-independent code for shared linkage with Skia.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Export compile commands for clangd / IDE tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# CMake modules
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# ---- Vulkan ----------------------------------------------------------------
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan SDK: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan lib: ${Vulkan_LIBRARIES}")

# ---- Qt6 -------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()
message(STATUS "Qt6 version: ${Qt6_VERSION}")

# ---- Skia ------------------------------------------------------------------
# Skia does not ship a first-party CMake config.  We locate it via:
#   SKIA_DIR  — path to the built Skia directory (contains out/Release)
#   SKIA_INCLUDE_DIRS — header roots (default: ${SKIA_DIR})
#   SKIA_LIBRARIES    — link targets  (default: libskia.a + libskshaper.a)
#
# Build Skia with Vulkan backend:
#   bin/gn gen out/Release --args='is_official_build=true skia_use_vulkan=true \
#       skia_use_gl=false skia_use_egl=false skia_enable_skottie=false \
#       cc="clang" cxx="clang++" extra_cflags=["-std=c++20"]'
#   ninja -C out/Release skia skshaper
# ---------------------------------------------------------------------------
if(NOT DEFINED SKIA_DIR)
    message(FATAL_ERROR
        "SKIA_DIR is not set.  Pass -DSKIA_DIR=/path/to/skia-source to cmake.\n"
        "Skia must be built with skia_use_vulkan=true and skia_use_gl=false.")
endif()

set(SKIA_INCLUDE_DIRS
    "${SKIA_DIR}"
    "${SKIA_DIR}/include"
    "${SKIA_DIR}/include/core"
    "${SKIA_DIR}/include/gpu"
    "${SKIA_DIR}/include/gpu/vk"
    "${SKIA_DIR}/include/effects"
    "${SKIA_DIR}/include/utils"
)

if(NOT DEFINED SKIA_LIB_DIR)
    set(SKIA_LIB_DIR "${SKIA_DIR}/out/Release")
endif()

find_library(SKIA_LIB       skia       PATHS "${SKIA_LIB_DIR}" REQUIRED)
find_library(SKSHAPER_LIB   skshaper   PATHS "${SKIA_LIB_DIR}")   # optional but recommended
find_library(SKPARAGRAPH_LIB skparagraph PATHS "${SKIA_LIB_DIR}") # optional

message(STATUS "Skia lib:      ${SKIA_LIB}")
message(STATUS "SkShaper lib:  ${SKSHAPER_LIB}")

set(SKIA_LIBRARIES ${SKIA_LIB})
if(SKSHAPER_LIB)
    list(APPEND SKIA_LIBRARIES ${SKSHAPER_LIB})
endif()
if(SKPARAGRAPH_LIB)
    list(APPEND SKIA_LIBRARIES ${SKPARAGRAPH_LIB})
endif()

# ---- (Optional) Ultralight -------------------------------------------------
# Set -DUSE_ULTRALIGHT=ON -DULTRALIGHT_SDK_DIR=/path to enable the real HTML engine.
option(USE_ULTRALIGHT "Link Ultralight for HTML/CSS rendering" OFF)
if(USE_ULTRALIGHT)
    if(NOT DEFINED ULTRALIGHT_SDK_DIR)
        message(FATAL_ERROR "USE_ULTRALIGHT=ON but ULTRALIGHT_SDK_DIR is not set.")
    endif()
    find_library(ULTRALIGHT_LIB   Ultralight PATHS "${ULTRALIGHT_SDK_DIR}/lib" REQUIRED)
    find_library(WEBCORE_LIB      WebCore    PATHS "${ULTRALIGHT_SDK_DIR}/lib" REQUIRED)
    find_library(APPCORE_LIB      AppCore    PATHS "${ULTRALIGHT_SDK_DIR}/lib" REQUIRED)
    set(ULTRALIGHT_INCLUDE_DIRS "${ULTRALIGHT_SDK_DIR}/include")
    set(ULTRALIGHT_LIBRARIES    ${ULTRALIGHT_LIB} ${WEBCORE_LIB} ${APPCORE_LIB})
    message(STATUS "Ultralight: enabled (${ULTRALIGHT_SDK_DIR})")
endif()

# ---- SPIR-V shader compilation (glslc) ------------------------------------
find_program(GLSLC glslc HINTS ENV VULKAN_SDK PATH_SUFFIXES bin)
if(NOT GLSLC)
    message(WARNING "glslc not found — shaders will not be compiled automatically.")
endif()

function(add_spirv_shader target glsl_file)
    get_filename_component(shader_name "${glsl_file}" NAME)
    set(spirv_out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}.spv")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    add_custom_command(
        OUTPUT  "${spirv_out}"
        COMMAND "${GLSLC}" -o "${spirv_out}" "${CMAKE_SOURCE_DIR}/${glsl_file}"
        DEPENDS "${CMAKE_SOURCE_DIR}/${glsl_file}"
        COMMENT "Compiling shader ${glsl_file} → ${spirv_out}"
    )
    target_sources(${target} PRIVATE "${spirv_out}")
endfunction()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(VULKAN_SOURCES
    src/vulkan/VulkanManager.cpp
    src/vulkan/VulkanSwapchain.cpp
    src/vulkan/VulkanImage.cpp
)

set(RENDERER_SOURCES
    src/renderer/SkiaVulkanContext.cpp
    src/renderer/PageRenderer.cpp
)

set(IPC_SOURCES
    src/ipc/IpcChannel.cpp
)

set(BROWSER_SOURCES
    src/browser/BrowserTab.cpp
    src/browser/BrowserWindow.cpp
)

set(ALL_SOURCES
    ${VULKAN_SOURCES}
    ${RENDERER_SOURCES}
    ${IPC_SOURCES}
    ${BROWSER_SOURCES}
    src/main.cpp
)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
qt_add_executable(vulkan-browser ${ALL_SOURCES})

# Shader targets (listed individually so CMake tracks changes)
if(GLSLC)
    add_spirv_shader(vulkan-browser shaders/blit.vert)
    add_spirv_shader(vulkan-browser shaders/blit.frag)
endif()

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
target_include_directories(vulkan-browser PRIVATE
    "${CMAKE_SOURCE_DIR}/include"
    "${Vulkan_INCLUDE_DIRS}"
    "${SKIA_INCLUDE_DIRS}"
    "$<$<BOOL:${USE_ULTRALIGHT}>:${ULTRALIGHT_INCLUDE_DIRS}>"
)

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
target_compile_definitions(vulkan-browser PRIVATE
    QT_NO_OPENGL                # Ensure Qt never silently pulls in GL
    VK_NO_PROTOTYPES            # Use function pointers for forward-compat
    "$<$<BOOL:${USE_ULTRALIGHT}>:USE_ULTRALIGHT>"
    "$<$<CONFIG:Release>:NDEBUG>"
    "$<$<CONFIG:Debug>:VK_DEBUG>"
)

# ---------------------------------------------------------------------------
# Compile options
# ---------------------------------------------------------------------------
target_compile_options(vulkan-browser PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
    "$<$<CONFIG:Release>:-O3;-march=native>"
    "$<$<CONFIG:Debug>:-g;-O0;-fsanitize=address,undefined>"
)

target_link_options(vulkan-browser PRIVATE
    "$<$<CONFIG:Debug>:-fsanitize=address,undefined>"
)

# ---------------------------------------------------------------------------
# Link libraries — NO OpenGL, NO EGL
# ---------------------------------------------------------------------------
target_link_libraries(vulkan-browser PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Vulkan::Vulkan
    ${SKIA_LIBRARIES}
    # Skia needs these system libs on Linux:
    "$<$<PLATFORM_ID:Linux>:pthread>"
    "$<$<PLATFORM_ID:Linux>:dl>"
    "$<$<PLATFORM_ID:Linux>:fontconfig>"
    "$<$<PLATFORM_ID:Linux>:freetype>"
    "$<$<BOOL:${USE_ULTRALIGHT}>:${ULTRALIGHT_LIBRARIES}>"
)

# ---------------------------------------------------------------------------
# Platform-specific Vulkan surface extensions
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    # Link XCB and Wayland for surface creation on Linux.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB         REQUIRED xcb)
    pkg_check_modules(WAYLAND_CLIENT wayland-client)

    target_include_directories(vulkan-browser PRIVATE ${XCB_INCLUDE_DIRS})
    target_link_libraries(vulkan-browser PRIVATE ${XCB_LIBRARIES})

    if(WAYLAND_CLIENT_FOUND)
        target_include_directories(vulkan-browser PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIRS})
        target_link_libraries(vulkan-browser PRIVATE ${WAYLAND_CLIENT_LIBRARIES})
        target_compile_definitions(vulkan-browser PRIVATE HAS_WAYLAND)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests (optional — enable with -DBUILD_TESTS=ON)
# ---------------------------------------------------------------------------
option(BUILD_TESTS "Build unit tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS vulkan-browser
        RUNTIME DESTINATION bin
        BUNDLE  DESTINATION .)

install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/shaders/"
        DESTINATION share/vulkan-browser/shaders
        FILES_MATCHING PATTERN "*.spv")

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
message(STATUS "------------------------------------------------------------")
message(STATUS "  VulkanBrowser ${PROJECT_VERSION}")
message(STATUS "  Build type    : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard  : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Vulkan        : ${Vulkan_VERSION}")
message(STATUS "  Qt6           : ${Qt6_VERSION}")
message(STATUS "  Skia dir      : ${SKIA_DIR}")
message(STATUS "  Ultralight    : ${USE_ULTRALIGHT}")
message(STATUS "  Shaders       : ${GLSLC}")
message(STATUS "  Tests         : ${BUILD_TESTS}")
message(STATUS "------------------------------------------------------------")
