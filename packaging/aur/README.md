# fooyin-album-mosaic-plugin

AUR package for the [Album Mosaic Plugin for Fooyin](https://github.com/dewiweb/fooyin-album-mosaic-plugin).

## Installation

```bash
# Using an AUR helper (yay, paru, etc.)
yay -S fooyin-album-mosaic-plugin

# Or manually:
git clone https://aur.archlinux.org/fooyin-album-mosaic-plugin.git
cd fooyin-album-mosaic-plugin
makepkg -si
```

## Requirements

- `fooyin` (0.12.6 or newer, from AUR: `fooyin` or `fooyin-git`)
- `qt6-base`
- `cmake`, `ninja` (build only)

## Usage

After installation, restart Fooyin and add the "Album Mosaic" widget to your layout.

See the [upstream README](https://github.com/dewiweb/fooyin-album-mosaic-plugin#readme) for full documentation.
