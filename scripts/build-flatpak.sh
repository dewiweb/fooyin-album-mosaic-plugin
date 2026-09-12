#!/usr/bin/env bash
#
# Build and install the Album Mosaic plugin for the Flatpak version of Fooyin.
#
# The Flatpak fooyin uses Qt 6.11 (from the KDE SDK 6.11 runtime), while the
# system Qt is 6.4.2. A plugin built with Qt 6.4.2 will NOT load in the flatpak
# due to Qt ABI incompatibility. This script offers two modes:
#
# Mode 1 (default): Build inside the flatpak SDK sandbox (requires org.kde.Sdk/6.11)
#   ./scripts/build-flatpak.sh --install
#
# Mode 2 (--copy): Copy the dev build .so and patch RUNPATH (quick, may fail)
#   ./scripts/build-flatpak.sh --install --copy
#
# After install, the flatpak override is set to grant filesystem access to the
# plugin directory:
#   flatpak override --user org.fooyin.fooyin --filesystem=~/.local/lib/fooyin
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build-flatpak"

DO_INSTALL=false
DO_COPY=false
for arg in "$@"; do
    case "$arg" in
        --install) DO_INSTALL=true ;;
        --copy)    DO_COPY=true ;;
    esac
done

FLATPAK_APP_ID="org.fooyin.fooyin"
FLATPAK_SDK="org.kde.Sdk/x86_64/6.11"
PLUGIN_DIR="$HOME/.local/lib/fooyin/plugins"
FLATPAK_APP_FILES="$(flatpak info --show-location "$FLATPAK_APP_ID" 2>/dev/null)/files"

echo "=== Album Mosaic Plugin — Flatpak Build ==="
echo "  App ID:     $FLATPAK_APP_ID"
echo "  SDK:        $FLATPAK_SDK"
echo "  Plugin dir: $PLUGIN_DIR"
echo ""

# --- Mode 2: Copy dev build .so and patch RUNPATH ---
if $DO_COPY; then
    echo "=== Copy mode: using dev build .so ==="
    echo "  WARNING: Plugin was built with Qt 6.4.2, flatpak uses Qt 6.11."
    echo "           This may fail to load due to ABI incompatibility."
    echo "           For a proper build, install org.kde.Sdk/6.11 and run without --copy."
    echo ""

    DEV_SO="$PROJECT_DIR/build/fyplugin_albummosaicplugin.so"
    if [ ! -f "$DEV_SO" ]; then
        echo "ERROR: Dev build not found at $DEV_SO" >&2
        echo "       Run ./scripts/build.sh first." >&2
        exit 1
    fi

    mkdir -p "$PLUGIN_DIR"
    cp "$DEV_SO" "$PLUGIN_DIR/fyplugin_albummosaicplugin.so"

    # Patch RUNPATH to point to flatpak's fooyin libraries
    if command -v patchelf >/dev/null 2>&1; then
        patchelf --set-rpath '/app/lib/fooyin' "$PLUGIN_DIR/fyplugin_albummosaicplugin.so"
        echo "  Patched RUNPATH → /app/lib/fooyin"
    else
        echo "  WARNING: patchelf not found, RUNPATH not patched" >&2
    fi

    echo "  Installed: $PLUGIN_DIR/fyplugin_albummosaicplugin.so"
    echo ""
    echo "=== Granting flatpak filesystem access ==="
    flatpak override --user "$FLATPAK_APP_ID" --filesystem=~/.local/lib/fooyin
    echo "  Override set: --filesystem=~/.local/lib/fooyin"
    echo ""
    echo "=== Done (copy mode) ==="
    echo "  Test with: ./scripts/test.sh --target=flatpak"
    exit 0
fi

# --- Mode 1: Build inside flatpak SDK sandbox ---
if ! flatpak info "$FLATPAK_SDK" >/dev/null 2>&1; then
    echo "ERROR: Flatpak SDK $FLATPAK_SDK is not installed." >&2
    echo "       Install it with: flatpak install flathub $FLATPAK_SDK" >&2
    echo "       Or use --copy mode for a quick (but possibly incompatible) install." >&2
    exit 1
fi

if [ ! -d "$FLATPAK_APP_FILES/lib/fooyin" ]; then
    echo "ERROR: Fooyin flatpak not found or incomplete." >&2
    exit 1
fi

# Use dev build headers (they're API-compatible with 0.12.6)
FOOYIN_DEV_PREFIX="/home/dewi/newhome/fooyin-dev"

# Clean build dir
if [ -d "$BUILD_DIR" ]; then
    rm -rf "$BUILD_DIR"
fi
mkdir -p "$BUILD_DIR"

echo "=== Configuring (CMake in flatpak SDK sandbox) ==="
# Grant access to project dir, build dir, and dev headers
flatpak run --filesystem="$PROJECT_DIR" --filesystem="$BUILD_DIR" \
    --filesystem="$FOOYIN_DEV_PREFIX" \
    "$FLATPAK_SDK" \
    cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DFooyin_DIR="$FOOYIN_DEV_PREFIX/lib/cmake/fooyin" \
        -DCMAKE_PREFIX_PATH="$FOOYIN_DEV_PREFIX"

echo "=== Building (in flatpak SDK sandbox) ==="
flatpak run --filesystem="$PROJECT_DIR" --filesystem="$BUILD_DIR" \
    --filesystem="$FOOYIN_DEV_PREFIX" \
    "$FLATPAK_SDK" \
    cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo ""
echo "=== Flatpak build complete ==="
echo "  Plugin: $BUILD_DIR/fyplugin_albummosaicplugin.so"

if $DO_INSTALL; then
    echo ""
    echo "=== Installing ==="
    mkdir -p "$PLUGIN_DIR"
    cp "$BUILD_DIR/fyplugin_albummosaicplugin.so" "$PLUGIN_DIR/"

    # Patch RUNPATH to point to flatpak's fooyin libraries
    if command -v patchelf >/dev/null 2>&1; then
        patchelf --set-rpath '/app/lib/fooyin' "$PLUGIN_DIR/fyplugin_albummosaicplugin.so"
        echo "  Patched RUNPATH → /app/lib/fooyin"
    fi

    echo "  Installed: $PLUGIN_DIR/fyplugin_albummosaicplugin.so"

    echo ""
    echo "=== Granting flatpak filesystem access ==="
    flatpak override --user "$FLATPAK_APP_ID" --filesystem=~/.local/lib/fooyin
    echo "  Override set: --filesystem=~/.local/lib/fooyin"
fi

echo ""
echo "=== Done ==="
echo "  Test with: ./scripts/test.sh --target=flatpak"
