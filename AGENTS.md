# Album Mosaic Plugin — Development Guide

## Project Overview

Fooyin plugin displaying an infinite scrolling mosaic of album covers with 3D flip animation, genre/artist filtering, and click-to-play.

- **Repository**: https://github.com/dewiweb/fooyin-album-mosaic-plugin
- **Language**: C++ (Qt6)
- **Build system**: CMake + Ninja
- **License**: GPL-3.0

## Fooyin Installations on This System

| Target | Binary | Version | Headers | Plugin dir |
|--------|--------|---------|---------|------------|
| `dev` | `fooyin` (symlink in `~/.local/bin`) | 0.12.6 (from source) | `/home/dewi/newhome/fooyin-dev/include/fooyin/` | `~/.local/lib/fooyin/plugins/` |
| `deb` | `/usr/bin/fooyin` | 0.12.6 (deb package) | uses dev headers | `~/.local/lib/fooyin/plugins/` |
| `flatpak` | `flatpak run org.fooyin.fooyin` | 0.12.6 (flathub, Qt 6.11.1) | inside sandbox | `~/.local/lib/fooyin/plugins/` (needs override) |
| `old` | `/usr/local/bin/fooyin` | 0.10.3 (from source, backup) | `/usr/local/include/fooyin/` | `~/.local/lib/fooyin/plugins/` |

The `dev` target is the default — `fooyin` in PATH now points to `/home/dewi/newhome/fooyin-dev/bin/fooyin` via a symlink in `~/.local/bin/`. The old 0.10.3 in `/usr/local/bin/fooyin` is still present but shadowed by the symlink.

## Build Workflow

### Prerequisites

Fooyin 0.12.6 must be built from source with headers (one-time setup):

```bash
cd /home/dewi/newhome/Github/fooyin
git checkout build-0.12.6   # branch on tag v0.12.6
cmake -S . -B build -G Ninja \
    -DCMAKE_INSTALL_PREFIX=/home/dewi/newhome/fooyin-dev \
    -DCMAKE_BUILD_TYPE=Release \
    -DINSTALL_HEADERS=ON
cmake --build build --parallel $(nproc)
cmake --install build
```

### Build the Plugin

```bash
# Build against dev fooyin (0.12.6 from source) — default
./scripts/build.sh --install

# Clean rebuild
./scripts/build.sh --clean --install

# Build for flatpak (requires org.kde.Sdk/6.11, or use --copy for quick install)
./scripts/build-flatpak.sh --install
./scripts/build-flatpak.sh --install --copy  # quick copy + patchelf (may fail: Qt 6.4→6.11)
```

The `--install` flag **copies** the `.so` to `~/.local/lib/fooyin/plugins/` and patches the RUNPATH to include both native and flatpak library paths. A copy (not symlink) is used because the flatpak sandbox can't follow symlinks to `/home/dewi/newhome/`.

**Flatpak note**: The flatpak fooyin uses Qt 6.11.1 while the system has Qt 6.4.2. A plugin built with Qt 6.4.2 may not load in the flatpak due to ABI incompatibility. For a proper flatpak build, install `org.kde.Sdk/x86_64/6.11` and run `./scripts/build-flatpak.sh --install` (builds inside the SDK sandbox). The `--copy` mode copies the dev build and patches the RUNPATH, but may fail to load. The flatpak also needs a filesystem override (handled automatically by the script):
```bash
flatpak override --user org.fooyin.fooyin --filesystem=~/.local/lib/fooyin
```

### Test the Plugin

```bash
# Launch dev fooyin (0.12.6 from source) — default
./scripts/test.sh

# Launch deb fooyin
./scripts/test.sh --target=deb

# Launch flatpak fooyin
./scripts/test.sh --target=flatpak

# Launch old fooyin 0.10.3
./scripts/test.sh --target=old

# Run under gdb for debugging
./scripts/test.sh --debug

# Show debug logs
./scripts/test.sh --logs
```

## Architecture

### File Structure

- `albummosaicplugin.h/cpp` — Plugin entry point (registers widget, creates CoverProvider, declares settings)
- `albummosaicwidget.h/cpp` — Main widget (rendering, scroll, flip animation, click handling, context menu, filters)
- `albummosaicsettingsdialog.h/cpp` — Settings dialog (flip, columns, filters)
- `metadata.json` — Plugin metadata for Fooyin
- `CMakeLists.txt` — Build configuration
- `scripts/` — Build and test automation scripts

### Plugin Settings

Global defaults in `AlbumMosaic/*`; per-widget config is serialized in the layout via `saveLayoutData`/`loadLayoutData` and wins over these defaults.

- `AlbumMosaic/EnableAnim` (bool) — Enable/disable cover-swap animation
- `AlbumMosaic/AnimType` (string) — `Flip3D` (default), `Crossfade`, `Slide`, `Zoom`, `PageCurl`, `Random`
- `AlbumMosaic/AnimSpeed` (string) — `Fast`, `Medium` (default), `Slow`
- `AlbumMosaic/AnimScope` (string) — `Single` (default), `Multiple`, `Wave`
- `AlbumMosaic/ColumnCount` (int) — Number of columns in the grid
- `AlbumMosaic/SortMode` (string) — `Random` (default), `Year`, `YearDesc`, `Rating`, `PlayCount`, `Recent`
- `AlbumMosaic/BgColor` (string) — Background color (hex)
- `AlbumMosaic/GenreFilter` (string) — Filter albums by genre
- `AlbumMosaic/ArtistFilter` (string) — Filter albums by artist

### Key APIs Used

- `Fooyin::MusicLibrary` — Album metadata loading
- `Fooyin::CoverProvider` — Cover loading (external files, embedded, parent directory)
- `Fooyin::SettingsManager` — Plugin settings
- `Fooyin::FyWidget` — Base widget class
- `Fooyin::PlayerController` — Playback control

## Disk Space Notes

- `/` (sda3): **12G free** — system only, do NOT build here
- `/home/dewi/newhome` (sdb1): **184G free** — all dev work goes here
- `/home/dewi/extended` (sdc1): **27G free** — nearly full, avoid

## Common Tasks

### After modifying plugin source

```bash
./scripts/build.sh --install && ./scripts/test.sh
```

### After updating Fooyin source

```bash
cd /home/dewi/newhome/Github/fooyin
git pull
cmake --build build --parallel $(nproc)
cmake --install build
```

### Verify plugin loads

```bash
./scripts/test.sh --logs 2>&1 | grep -i 'mosaic\|plugin'
```
