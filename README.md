# Album Mosaic Plugin for Fooyin

Fooyin plugin to display an infinite scrolling mosaic of album covers.

## Features

- **Infinite Grid**: Cyclic scrolling through all albums in your Fooyin collection
- **Automatic Cover Detection**:
  - Search for external cover files (cover.jpg, folder.jpg, etc.)
  - Asynchronous embedded cover extraction (MP3 ID3v2, FLAC, MP4/M4A)
  - Search in parent directories for multi-disc albums
- **LRU Cache**: Intelligent cache of 200 covers for optimal performance
- **Tooltip**: Displays album name and artist on hover
- **Album Playback**: Click on a cover to play the entire album
- **Elegant Placeholder**: Gradient background with musical icon for albums without covers
- **Adaptive Grid**: Square cells automatically sized to fit the screen

## Prerequisites

- Fooyin installed with development headers
- CMake (>= 3.14)
- Qt6
- TagLib (for embedded cover extraction)
- C++23 compiler

## Installation

### 1. Clone the repository

```bash
git clone https://github.com/dewiweb/fooyin-album-mosaic-plugin.git
cd fooyin-album-mosaic-plugin
```

### 2. Build the plugin

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### 3. Install the plugin

```bash
mkdir -p ~/.local/lib/fooyin/plugins
cp build/fyplugin_albummosaicplugin.so ~/.local/lib/fooyin/plugins/
```

### 4. Restart Fooyin

The plugin should appear in the list of available widgets in Fooyin.

## Usage

1. Open Fooyin
2. Go to layout preferences
3. Add the "Album Mosaic" widget to your layout
4. The mosaic will display with covers from your collection
5. Use mouse wheel to scroll infinitely through your albums
6. Click on a cover to play the album

## Configuration

The plugin automatically uses the Fooyin database located at:
`~/.local/share/fooyin/fooyin.db`

No manual configuration is required.

## Architecture

### Plugin Structure

- `albummosaicplugin.h/cpp`: Plugin entry point
- `albummosaicwidget.h/cpp`: Main widget displaying the mosaic
- `CMakeLists.txt`: Build configuration
- `metadata.json`: Plugin metadata

### Internal Operation

- **Metadata Loading**: Queries Fooyin database to retrieve albums
- **Cover Detection**: Prioritizes external files, then asynchronously extracts embedded covers
- **LRU Cache**: 200-cover cache with Least Recently Used policy
- **Asynchronous Extraction**: Uses QtConcurrent to extract embedded covers without blocking the UI
- **Infinite Grid**: Uses modulo operator to create cyclic scrolling

## Customization

You can modify constants in `albummosaicwidget.h`:
- `MAX_CACHE_SIZE`: Cache size (default: 200)
- Timer intervals in the constructor

## Troubleshooting

### Covers not displaying
- Ensure your audio files have covers (external files or embedded)
- Check Fooyin logs for debug messages

### Fooyin crashes on startup
- Temporarily uninstall the plugin to verify if the issue comes from the plugin
- Ensure TagLib is installed

### Images changing too rapidly
- The LRU cache automatically manages memory
- Increase `MAX_CACHE_SIZE` if necessary

## License

GNU General Public License v3.0

## Acknowledgments

- Fooyin for its excellent plugin system
- TagLib for audio metadata extraction
