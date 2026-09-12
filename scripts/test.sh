#!/usr/bin/env bash
#
# Launch Fooyin for testing the Album Mosaic plugin.
#
# Usage:
#   ./scripts/test.sh              # Launch dev fooyin (0.12.6 from source)
#   ./scripts/test.sh --target=dev # Launch dev fooyin (default)
#   ./scripts/test.sh --target=deb # Launch deb fooyin (/usr/bin/fooyin)
#   ./scripts/test.sh --target=flatpak # Launch flatpak fooyin
#   ./scripts/test.sh --target=old  # Launch old fooyin 0.10.3 (/usr/local/bin)
#   ./scripts/test.sh --debug      # Run under gdb
#   ./scripts/test.sh --logs       # Show fooyin debug output (QT_LOGGING)
#
set -euo pipefail

TARGET="dev"
DEBUG=false
SHOW_LOGS=false

for arg in "$@"; do
    case "$arg" in
        --target=*) TARGET="${arg#--target=}" ;;
        --debug)    DEBUG=true ;;
        --logs)     SHOW_LOGS=true ;;
        --help|-h)
            grep '^#' "$0" | sed 's/^# \?//'
            exit 0
            ;;
    esac
done

# --- Determine fooyin binary and env ---
case "$TARGET" in
    dev)
        # Custom prefix build (0.12.6 from source)
        FOOYIN_BIN="/home/dewi/newhome/fooyin-dev/bin/fooyin"
        export LD_LIBRARY_PATH="/home/dewi/newhome/fooyin-dev/lib/fooyin:${LD_LIBRARY_PATH:-}"
        export QT_PLUGIN_PATH="/home/dewi/newhome/fooyin-dev/lib/qt6/plugins:${QT_PLUGIN_PATH:-}"
        ;;
    deb)
        FOOYIN_BIN="/usr/bin/fooyin"
        ;;
    flatpak)
        FOOYIN_BIN="flatpak"
        FLATPAK_ARGS="run org.fooyin.fooyin"
        ;;
    old)
        FOOYIN_BIN="/usr/local/bin/fooyin"
        ;;
    *)
        echo "Unknown target: $TARGET (use: dev, deb, flatpak, old)" >&2
        exit 1
        ;;
esac

# --- Verify binary exists ---
if [ "$TARGET" = "flatpak" ]; then
    if ! flatpak info org.fooyin.fooyin >/dev/null 2>&1; then
        echo "ERROR: Flatpak fooyin not installed." >&2
        exit 1
    fi
elif [ ! -x "$FOOYIN_BIN" ]; then
    echo "ERROR: Fooyin binary not found at $FOOYIN_BIN" >&2
    exit 1
fi

# --- Logging ---
if $SHOW_LOGS; then
    export QT_LOGGING_RULES="fooyin.debug=true"
    export FOOYIN_LOG_LEVEL="debug"
fi

echo "=== Launching Fooyin ($TARGET) ==="
if [ "$TARGET" = "flatpak" ]; then
    echo "  Binary: flatpak run org.fooyin.fooyin"
else
    echo "  Binary: $FOOYIN_BIN"
fi
echo "  Plugin: ~/.local/lib/fooyin/plugins/fyplugin_albummosaicplugin.so"
echo ""

# --- Launch ---
if $DEBUG; then
    if [ "$TARGET" = "flatpak" ]; then
        flatpak run --devel --command=gdb org.fooyin.fooyin \
            --args /app/bin/fooyin
    else
        gdb "$FOOYIN_BIN"
    fi
else
    if [ "$TARGET" = "flatpak" ]; then
        exec flatpak run org.fooyin.fooyin
    else
        exec "$FOOYIN_BIN"
    fi
fi
