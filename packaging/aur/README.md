# fooyin-plugin-albummosaic

AUR package for the [Album Mosaic Plugin for Fooyin](https://github.com/dewiweb/fooyin-album-mosaic-plugin).

## Installation

```bash
# Using an AUR helper (yay, paru, etc.)
yay -S fooyin-plugin-albummosaic

# Or manually:
git clone https://aur.archlinux.org/fooyin-plugin-albummosaic.git
cd fooyin-plugin-albummosaic
makepkg -si
```

## Requirements

- `fooyin` (0.12.6 or newer — official `extra/fooyin` package, or `fooyin-git` from AUR)
- `qt6-base`
- `cmake`, `ninja` (build only)

## Usage

After installation, restart Fooyin and add the "Album Mosaic" widget to your layout.

See the [upstream README](https://github.com/dewiweb/fooyin-album-mosaic-plugin#readme) for full documentation.
