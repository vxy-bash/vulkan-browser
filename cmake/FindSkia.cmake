# FindSkia.cmake — locate a pre-built Skia with Vulkan backend.
#
# Usage:
#   cmake -DSKIA_DIR=/path/to/skia-source -DSKIA_LIB_DIR=/path/to/skia/out/Release ...
#
# Exported variables:
#   SKIA_FOUND
#   SKIA_INCLUDE_DIRS
#   SKIA_LIBRARIES

include(FindPackageHandleStandardArgs)

if(NOT DEFINED SKIA_DIR)
    set(SKIA_DIR "" CACHE PATH "Path to Skia source tree (built with Vulkan backend)")
endif()

if(NOT DEFINED SKIA_LIB_DIR)
    set(SKIA_LIB_DIR "${SKIA_DIR}/out/Release" CACHE PATH "Path to Skia build output directory")
endif()

find_path(SKIA_INCLUDE_DIR
    NAMES include/core/SkSurface.h
    PATHS "${SKIA_DIR}"
    NO_DEFAULT_PATH
)

find_library(SKIA_LIBRARY
    NAMES skia
    PATHS "${SKIA_LIB_DIR}"
    NO_DEFAULT_PATH
)

set(SKIA_INCLUDE_DIRS
    "${SKIA_INCLUDE_DIR}"
    "${SKIA_INCLUDE_DIR}/include"
    "${SKIA_INCLUDE_DIR}/include/core"
    "${SKIA_INCLUDE_DIR}/include/gpu"
    "${SKIA_INCLUDE_DIR}/include/gpu/vk"
    "${SKIA_INCLUDE_DIR}/include/effects"
)

set(SKIA_LIBRARIES "${SKIA_LIBRARY}")

find_package_handle_standard_args(Skia
    REQUIRED_VARS SKIA_LIBRARY SKIA_INCLUDE_DIR
    VERSION_VAR   SKIA_VERSION
)

if(Skia_FOUND AND NOT TARGET Skia::Skia)
    add_library(Skia::Skia UNKNOWN IMPORTED)
    set_target_properties(Skia::Skia PROPERTIES
        IMPORTED_LOCATION "${SKIA_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SKIA_INCLUDE_DIRS}"
    )
endif()
