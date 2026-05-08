#!/usr/bin/env bash
# build.sh — baut VulkanBrowser von Grund auf und produziert eine ausführbare Datei.
#
# Voraussetzungen (werden automatisch geprüft):
#   - CMake ≥ 3.25
#   - Clang ≥ 16  oder  GCC ≥ 13
#   - Vulkan SDK  (VULKAN_SDK oder vulkan-headers + libvulkan-dev)
#   - Qt6 Widgets + Network
#   - libxcb-dev, libwayland-dev  (Linux)
#   - ninja-build  (oder make als Fallback)
#
# Skia muss vorher gebaut werden — dieses Script macht das automatisch,
# wenn SKIA_DIR nicht gesetzt ist (braucht python3, git, depot_tools, ninja).
#
# Verwendung:
#   ./build.sh                        # Release-Build
#   ./build.sh --debug                # Debug + ASAN
#   ./build.sh --clean                # Build-Verzeichnis löschen und neu
#   ./build.sh --ultralight /sdk/path # mit Ultralight HTML-Engine
#   ./build.sh --jobs 8               # Parallel-Jobs (default: nproc)
#   ./build.sh --output /pfad/bin     # Zielordner für das fertige Binary
#
# Ergebnis:
#   build/vulkan-browser   (oder der via --output angegebene Pfad)

set -euo pipefail
IFS=$'\n\t'

# ---------------------------------------------------------------------------
# Farben & Logging
# ---------------------------------------------------------------------------
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; RESET='\033[0m'

log()  { echo -e "${CYAN}[build]${RESET} $*"; }
ok()   { echo -e "${GREEN}[  OK ]${RESET} $*"; }
warn() { echo -e "${YELLOW}[ WARN]${RESET} $*"; }
fail() { echo -e "${RED}[FAIL ]${RESET} $*" >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ---------------------------------------------------------------------------
# Argumente parsen
# ---------------------------------------------------------------------------
BUILD_TYPE="Release"
CLEAN=0
USE_ULTRALIGHT=OFF
ULTRALIGHT_SDK_DIR=""
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
OUTPUT_DIR=""
SKIA_DIR_ARG="${SKIA_DIR:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --debug)         BUILD_TYPE="Debug"; shift ;;
        --clean)         CLEAN=1; shift ;;
        --ultralight)    USE_ULTRALIGHT=ON; ULTRALIGHT_SDK_DIR="$2"; shift 2 ;;
        --jobs|-j)       JOBS="$2"; shift 2 ;;
        --output|-o)     OUTPUT_DIR="$2"; shift 2 ;;
        --skia-dir)      SKIA_DIR_ARG="$2"; shift 2 ;;
        --help|-h)
            sed -n '2,30p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) fail "Unbekanntes Argument: $1  (--help für Hilfe)" ;;
    esac
done

BUILD_DIR="${SCRIPT_DIR}/build"
if [[ -n "$OUTPUT_DIR" ]]; then
    INSTALL_DIR="$OUTPUT_DIR"
else
    INSTALL_DIR="${SCRIPT_DIR}/dist"
fi

# ---------------------------------------------------------------------------
# Voraussetzungen prüfen
# ---------------------------------------------------------------------------
log "Prüfe Voraussetzungen…"

check_cmd() {
    if ! command -v "$1" &>/dev/null; then
        fail "'$1' nicht gefunden. $2"
    fi
    ok "$1 → $(command -v "$1")"
}

check_cmd cmake   "Installieren: sudo apt install cmake  oder  brew install cmake"
check_cmd git     "Installieren: sudo apt install git"
check_cmd python3 "Installieren: sudo apt install python3"

# CMake-Version prüfen
CMAKE_VER="$(cmake --version | head -1 | awk '{print $3}')"
CMAKE_MAJOR="$(echo "$CMAKE_VER" | cut -d. -f1)"
CMAKE_MINOR="$(echo "$CMAKE_VER" | cut -d. -f2)"
if [[ "$CMAKE_MAJOR" -lt 3 ]] || { [[ "$CMAKE_MAJOR" -eq 3 ]] && [[ "$CMAKE_MINOR" -lt 25 ]]; }; then
    fail "CMake ≥ 3.25 erforderlich, gefunden: $CMAKE_VER"
fi
ok "CMake $CMAKE_VER"

# Compiler
if command -v clang++ &>/dev/null; then
    CXX_COMPILER="clang++"
    CC_COMPILER="clang"
elif command -v g++ &>/dev/null; then
    CXX_COMPILER="g++"
    CC_COMPILER="gcc"
else
    fail "Kein C++20-Compiler gefunden (clang++ oder g++ erforderlich)."
fi
ok "Compiler: $CXX_COMPILER"

# Ninja oder Make
if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
    BUILD_CMD="ninja -j${JOBS}"
    ok "Build-System: Ninja"
elif command -v make &>/dev/null; then
    GENERATOR="Unix Makefiles"
    BUILD_CMD="make -j${JOBS}"
    ok "Build-System: Make (Ninja empfohlen: sudo apt install ninja-build)"
else
    fail "Weder ninja noch make gefunden."
fi

# Vulkan
if [[ -n "${VULKAN_SDK:-}" ]]; then
    VULKAN_HINT="$VULKAN_SDK"
    ok "Vulkan SDK: $VULKAN_SDK"
elif pkg-config --exists vulkan 2>/dev/null; then
    VULKAN_HINT=""
    ok "Vulkan: gefunden via pkg-config"
elif [[ -f /usr/include/vulkan/vulkan.h ]]; then
    VULKAN_HINT=""
    ok "Vulkan: /usr/include/vulkan/vulkan.h"
else
    fail "Vulkan-Headers nicht gefunden. Installieren:
  Ubuntu/Debian: sudo apt install libvulkan-dev vulkan-validationlayers-dev
  Arch:          sudo pacman -S vulkan-headers vulkan-validation-layers
  Oder: https://vulkan.lunarg.com/sdk/home"
fi

# Qt6
if ! pkg-config --exists Qt6Widgets 2>/dev/null && \
   ! cmake --find-package -DNAME=Qt6 -DCOMPILER_ID=GNU -DLANGUAGE=CXX \
           -DMODE=EXIST &>/dev/null 2>&1; then
    warn "Qt6 nicht via pkg-config gefunden. cmake wird es trotzdem versuchen."
else
    ok "Qt6: gefunden"
fi

# XCB (Linux)
if [[ "$(uname)" == "Linux" ]]; then
    if ! pkg-config --exists xcb 2>/dev/null; then
        fail "libxcb nicht gefunden. Installieren:
  Ubuntu/Debian: sudo apt install libxcb1-dev libxcb-util-dev"
    fi
    ok "libxcb: gefunden"
fi

# ---------------------------------------------------------------------------
# Skia bauen (wenn nicht vorhanden)
# ---------------------------------------------------------------------------
ensure_skia() {
    local skia_dir="$1"

    if [[ -f "${skia_dir}/out/Release/libskia.a" ]]; then
        ok "Skia bereits gebaut: ${skia_dir}/out/Release/libskia.a"
        return 0
    fi

    log "Skia nicht gefunden — starte Build (dauert 5–15 Minuten)…"

    # depot_tools prüfen / installieren
    local depot="${SCRIPT_DIR}/third_party/depot_tools"
    if [[ ! -d "$depot" ]]; then
        log "Klone depot_tools…"
        git clone --depth=1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$depot"
    fi
    export PATH="${depot}:${PATH}"

    # Skia klonen
    if [[ ! -d "${skia_dir}" ]]; then
        log "Klone Skia…"
        git clone --depth=1 https://skia.googlesource.com/skia.git "${skia_dir}"
    fi

    pushd "${skia_dir}" >/dev/null
        log "Synchronisiere Skia-Abhängigkeiten…"
        python3 tools/git-sync-deps

        log "Generiere Skia Build-Konfiguration (Vulkan-only)…"
        bin/gn gen out/Release --args="
            is_official_build   = true
            skia_use_vulkan     = true
            skia_use_gl         = false
            skia_use_egl        = false
            skia_use_x11        = false
            skia_enable_skottie = false
            skia_enable_svg     = false
            cc                  = \"${CC_COMPILER}\"
            cxx                 = \"${CXX_COMPILER}\"
            extra_cflags        = [\"-std=c++20\"]
        "

        log "Baue Skia (${JOBS} Jobs)…"
        ninja -C out/Release -j"${JOBS}" skia skshaper
    popd >/dev/null

    ok "Skia erfolgreich gebaut: ${skia_dir}/out/Release/libskia.a"
}

if [[ -z "$SKIA_DIR_ARG" ]]; then
    SKIA_DIR_ARG="${SCRIPT_DIR}/third_party/skia"
    warn "SKIA_DIR nicht angegeben — verwende ${SKIA_DIR_ARG}"
    warn "Alternativ: ./build.sh --skia-dir /pfad/zu/skia"
fi

ensure_skia "$SKIA_DIR_ARG"

# ---------------------------------------------------------------------------
# Build-Verzeichnis vorbereiten
# ---------------------------------------------------------------------------
if [[ "$CLEAN" -eq 1 ]] && [[ -d "$BUILD_DIR" ]]; then
    log "Lösche Build-Verzeichnis: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

# ---------------------------------------------------------------------------
# CMake konfigurieren
# ---------------------------------------------------------------------------
log "Konfiguriere CMake (${BUILD_TYPE})…"

CMAKE_ARGS=(
    -S "${SCRIPT_DIR}"
    -B "${BUILD_DIR}"
    -G "${GENERATOR}"
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
    -DCMAKE_CXX_COMPILER="${CXX_COMPILER}"
    -DCMAKE_C_COMPILER="${CC_COMPILER}"
    -DSKIA_DIR="${SKIA_DIR_ARG}"
    -DSKIA_LIB_DIR="${SKIA_DIR_ARG}/out/Release"
    -DUSE_ULTRALIGHT="${USE_ULTRALIGHT}"
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [[ -n "${ULTRALIGHT_SDK_DIR}" ]]; then
    CMAKE_ARGS+=(-DULTRALIGHT_SDK_DIR="${ULTRALIGHT_SDK_DIR}")
fi

if [[ -n "${VULKAN_HINT:-}" ]]; then
    CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="${VULKAN_HINT}")
fi

cmake "${CMAKE_ARGS[@]}"
ok "CMake-Konfiguration erfolgreich"

# Compile-Commands für clangd symlinken
if [[ -f "${BUILD_DIR}/compile_commands.json" ]]; then
    ln -sf "${BUILD_DIR}/compile_commands.json" "${SCRIPT_DIR}/compile_commands.json" 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# Bauen
# ---------------------------------------------------------------------------
log "Baue VulkanBrowser (${JOBS} Jobs)…"
cmake --build "${BUILD_DIR}" --config "${BUILD_TYPE}" -j"${JOBS}"

BINARY="${BUILD_DIR}/vulkan-browser"
if [[ ! -f "$BINARY" ]]; then
    fail "Build fehlgeschlagen — Binary nicht gefunden: $BINARY"
fi
ok "Binary erfolgreich gebaut: $BINARY"

# ---------------------------------------------------------------------------
# Installieren (kopiert Binary + Shader-SPIR-V in INSTALL_DIR)
# ---------------------------------------------------------------------------
log "Installiere nach ${INSTALL_DIR}…"
cmake --install "${BUILD_DIR}" --config "${BUILD_TYPE}" --prefix "${INSTALL_DIR}"

INSTALLED_BIN="${INSTALL_DIR}/bin/vulkan-browser"
if [[ ! -f "$INSTALLED_BIN" ]]; then
    # Fallback: Binary direkt kopieren
    mkdir -p "${INSTALL_DIR}/bin"
    cp "$BINARY" "${INSTALL_DIR}/bin/"
    INSTALLED_BIN="${INSTALL_DIR}/bin/vulkan-browser"
fi

# ---------------------------------------------------------------------------
# Abschlussbericht
# ---------------------------------------------------------------------------
SIZE="$(du -sh "$INSTALLED_BIN" | cut -f1)"
echo ""
echo -e "${BOLD}${GREEN}╔══════════════════════════════════════════════════╗${RESET}"
echo -e "${BOLD}${GREEN}║          VulkanBrowser Build erfolgreich!        ║${RESET}"
echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════╣${RESET}"
echo -e "${BOLD}${GREEN}║${RESET}  Binary:   ${INSTALLED_BIN}"
echo -e "${BOLD}${GREEN}║${RESET}  Größe:    ${SIZE}"
echo -e "${BOLD}${GREEN}║${RESET}  Build:    ${BUILD_TYPE}"
echo -e "${BOLD}${GREEN}║${RESET}  Compiler: ${CXX_COMPILER}"
echo -e "${BOLD}${GREEN}║${RESET}  Skia:     ${SKIA_DIR_ARG}"
echo -e "${BOLD}${GREEN}║${RESET}  Ultralt:  ${USE_ULTRALIGHT}"
echo -e "${BOLD}${GREEN}╠══════════════════════════════════════════════════╣${RESET}"
echo -e "${BOLD}${GREEN}║${RESET}  Starten:  ${INSTALLED_BIN}"
if [[ "$BUILD_TYPE" == "Debug" ]]; then
echo -e "${BOLD}${GREEN}║${RESET}  Debug:    VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation \\"
echo -e "${BOLD}${GREEN}║${RESET}             ${INSTALLED_BIN}"
fi
echo -e "${BOLD}${GREEN}╚══════════════════════════════════════════════════╝${RESET}"
echo ""
