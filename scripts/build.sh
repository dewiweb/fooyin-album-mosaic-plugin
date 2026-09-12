#!/usr/bin/env bash
#
# Build the Album Mosaic plugin against a specific Fooyin installation.
#
# Usage:
#   ./scripts/build.sh              # Build against the dev prefix (default)
#   ./scripts/build.sh --target=dev # Build against /home/dewi/newhome/fooyin-dev (0.12.6)
#   ./scripts/build.sh --target=deb # Build against /usr (deb 0.12.6, needs headers)
#   ./scripts/build.sh --target=flatpak # Build inside flatpak SDK sandbox
#   ./scripts/build.sh --clean      # Remove build dir before building
#   ./scripts/build.sh --install    # Also install after build
#   ./scripts/build.sh --install --target=deb # Build + install for deb fooyin
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

# --- Defaults ---
TARGET="dev"
DO_CLEAN=false
DO_INSTALL=false

# --- Parse args ---
for arg in "$@"; do
    case "$arg" in
        --target=*) TARGET="${arg#--target=}" ;;
        --clean)    DO_CLEAN=true ;;
        --install)  DO_INSTALL=true ;;
        --help|-h)
            grep '^#' "$0" | sed 's/^# \?//'
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg" >&2
            exit 1
            ;;
    esac
done

# --- Target configuration ---
case "$TARGET" in
    dev)
        # Fooyin 0.12.6 built from source, installed in custom prefix
        FOOYIN_PREFIX="/home/dewi/newhome/fooyin-dev"
        FOOYIN_CMAKE_DIR="$FOOYIN_PREFIX/lib/cmake/fooyin"
        PLUGIN_INSTALL_DIR="$HOME/.local/lib/fooyin/plugins"
        ;;
    deb)
        # Fooyin 0.12.6 from deb package (uses /usr/local headers if available)
        FOOYIN_PREFIX="/usr"
        FOOYIN_CMAKE_DIR="/usr/local/lib/cmake/fooyin"
        PLUGIN_INSTALL_DIR="$HOME/.local/lib/fooyin/plugins"
        ;;
    flatpak)
        # Flatpak build — handled by build-flatpak.sh
        exec "$SCRIPT_DIR/build-flatpak.sh" "${@:--install}"
        ;;
    *)
        echo "Unknown target: $TARGET (use: dev, deb, flatpak)" >&2
        exit 1
        ;;
esac

echo "=== Album Mosaic Plugin Build ==="
echo "  Target:       $TARGET"
echo "  Fooyin prefix: $FOOYIN_PREFIX"
echo "  CMake dir:     $FOOYIN_CMAKE_DIR"
echo "  Install dir:   $PLUGIN_INSTALL_DIR"
echo ""

if [ ! -f "$FOOYIN_CMAKE_DIR/FooyinConfig.cmake" ]; then
    echo "ERROR: Fooyin CMake config not found at $FOOYIN_CMAKE_DIR/FooyinConfig.cmake" >&2
    echo "       Make sure Fooyin is built with INSTALL_HEADERS=ON for this target." >&2
    exit 1
fi

# --- Clean ---
if $DO_CLEAN && [ -d "$BUILD_DIR" ]; then
    echo "=== Cleaning build dir ==="
    rm -rf "$BUILD_DIR"
fi

# --- Configure ---
echo "=== Configuring (CMake) ==="
cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFooyin_DIR="$FOOYIN_CMAKE_DIR"

# --- Build ---
echo "=== Building ==="
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo ""
echo "=== Build complete ==="
echo "  Plugin: $BUILD_DIR/fyplugin_albummosaicplugin.so"

# --- Install ---
if $DO_INSTALL; then
    echo ""
    echo "=== Installing to $PLUGIN_INSTALL_DIR ==="
    mkdir -p "$PLUGIN_INSTALL_DIR"

    # Copy the .so (not symlink — flatpak can't follow symlinks to /home/dewi/newhome)
    cp "$BUILD_DIR/fyplugin_albummosaicplugin.so" \
       "$PLUGIN_INSTALL_DIR/fyplugin_albummosaicplugin.so"

    echo "  Installed (copy): $PLUGIN_INSTALL_DIR/fyplugin_albummosaicplugin.so"
    echo "  -> $BUILD_DIR/fyplugin_albummosaicplugin.so"

    # Also patch RUNPATH for flatpak compatibility (flatpak fooyin libs at /app/lib/fooyin)
    # Native fooyin will use its own library paths, flatpak will use /app/lib/fooyin
    if command -v patchelf >/dev/null 2>&1; then
        # Set RUNPATH to include both native and flatpak paths
        patchelf --set-rpath '/home/dewi/newhome/fooyin-dev/lib/fooyin:/app/lib/fooyin' \
                 "$PLUGIN_INSTALL_DIR/fyplugin_albummosaicplugin.so" 2>/dev/null || true
    fi
fi
