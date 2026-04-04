# Album Mosaic Plugin for Fooyin

Fooyin plugin to display an infinite scrolling mosaic of album covers.

## Features

- **Infinite Grid**: Cyclic scrolling through all albums in your Fooyin collection
- **CoverProvider Integration**: Uses Fooyin's CoverProvider for all cover loading (external files, embedded covers, parent directory search)
- **Performance**: Fluid rendering using CoverProvider's static cache
- **Album Playback**: Click on a cover to play the entire album
- **Elegant Placeholder**: Gradient background with musical icon for albums without covers
- **Adaptive Grid**: Square cells automatically sized to fit the screen

## Prerequisites

- Fooyin installed with development headers
- CMake (>= 3.14)
- Qt6
- C++ compiler

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

## Architecture

### Plugin Structure

- `albummosaicplugin.h/cpp`: Plugin entry point
- `albummosaicwidget.h/cpp`: Main widget displaying the mosaic
- `CMakeLists.txt`: Build configuration
- `metadata.json`: Plugin metadata

### Internal Operation

- **Metadata Loading**: Uses Fooyin's MusicLibrary to retrieve album information
- **Cover Loading**: Uses Fooyin's CoverProvider for all cover detection and loading (external files, embedded covers, parent directory search)
- **Cache**: Leverages CoverProvider's static cache shared across all Fooyin widgets
- **Signal-Based Updates**: Connects to CoverProvider::coverAdded signal to repaint when covers are loaded
- **Infinite Grid**: Uses modulo operator to create cyclic scrolling

## License

GNU General Public License v3.0

## Acknowledgments

- Fooyin for its excellent plugin system and CoverProvider API
