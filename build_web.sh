#!/bin/bash
set -e

# Ship of Harkinian - Emscripten/WASM Build Script
# Requires: emsdk activated (emcc/emcmake/emmake in PATH)
#
# Usage:
#   ./build_web.sh [Release|Debug]   - Configure and build
#   ./build_web.sh clean             - Remove build directory and start fresh

BUILD_DIR="build-web"
BUILD_TYPE="${1:-Release}"

# Handle clean command
if [ "$1" = "clean" ]; then
    echo "Cleaning ${BUILD_DIR}..."
    if [ -d "${BUILD_DIR}" ]; then
        rm -rf "${BUILD_DIR}"
        echo "Build directory removed."
    else
        echo "Build directory does not exist."
    fi
    exit 0
fi

# Verify emscripten is available
if ! command -v emcmake &> /dev/null; then
    echo "Error: emcmake not found. Please activate emsdk first:"
    echo "  source /path/to/emsdk/emsdk_env.sh"
    exit 1
fi

echo "========================================"
echo "  Ship of Harkinian - Web Build"
echo "  Build Type: ${BUILD_TYPE}"
echo "========================================"

# Initialize submodules if needed
if [ ! -f "libultraship/CMakeLists.txt" ]; then
    echo "Initializing submodules..."
    git submodule update --init --recursive
fi

# Clean stale build if cmake cache has wrong system name
if [ -f "${BUILD_DIR}/CMakeCache.txt" ]; then
    CACHED_SYSTEM=$(grep "CMAKE_SYSTEM_NAME:STRING=" "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null | cut -d= -f2)
    if [ -n "$CACHED_SYSTEM" ] && [ "$CACHED_SYSTEM" != "Emscripten" ]; then
        echo "Stale build directory detected (was: ${CACHED_SYSTEM}). Cleaning..."
        rm -rf "${BUILD_DIR}"
    fi
fi

# Create build directory
mkdir -p "${BUILD_DIR}"

# Configure with emcmake
echo "Configuring with Emscripten..."
emcmake cmake -B "${BUILD_DIR}" -S . -G Ninja \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DBUILD_REMOTE_CONTROL=OFF \
    -DUSE_OPENGLES=ON \
    -DINCLUDE_MPQ_SUPPORT=ON \
    -DGFX_DEBUG_DISASSEMBLER=OFF \
    -DSUPPRESS_WARNINGS=ON

# Build
echo "Building..."
emmake cmake --build "${BUILD_DIR}" --target soh -j$(nproc) 2>&1

echo "========================================"
echo "  Build Complete!"
echo "  Output: ${BUILD_DIR}/soh/"
echo "  Files:  soh.html, soh.js, soh.wasm, soh.data"
echo ""
echo "  To serve locally:"
echo "    cd ${BUILD_DIR}/soh && python3 -m http.server 8080"
echo "========================================"
