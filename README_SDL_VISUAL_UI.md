# NSTV SDL visual UI

This version replaces the limiting framebuffer/bitmap renderer with an SDL2 renderer intended to match the visual mockup much more closely.

## What changed

- Uses SDL2 for real graphical rendering instead of the console/wireframe layer.
- Uses SDL2_ttf for real TTF text rendering.
- Looks for an Open Sans compatible font without bundling font files:
  - Switch: `sdmc:/switch/nstv/fonts/OpenSans-Regular.ttf`
  - Host: `./fonts/OpenSans-Regular.ttf`
  - Host fallback: DejaVu Sans / Liberation Sans when installed.
- Uses SDL2_image in the image cache to decode remote logos from the JSON `logo` field.
- Supports PNG/JPG/WebP depending on the installed SDL2_image build.
- Removes the numeric/acronym logo fallback; missing/failed logos show a generic media icon instead.
- Replaces the header icons with vector-drawn icons matching the provided SVG references.
- Fixes rounded card/focus borders so the previous circular artifacts do not appear.

## Host dependencies

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y libsdl2-dev libsdl2-ttf-dev libsdl2-image-dev libcurl4-openssl-dev zlib1g-dev
```

Build:

```bash
make clean
make host
```

## Switch dependencies

Install devkitPro packages similar to:

```bash
sudo dkp-pacman -S switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-curl switch-freetype switch-libpng switch-libjpeg-turbo switch-zlib
```

Depending on your devkitPro repository, package names may vary. If link errors mention `webp`, `jpeg`, `png`, `freetype`, or `bz2`, install the matching switch portlib package or adjust `LIBS` in `Makefile.switch` according to the libraries available under:

```bash
ls $DEVKITPRO/portlibs/switch/lib
```

Build:

```bash
make clean
make switch
```

## Font

To get the intended visual quality, place a TTF font at:

```text
sdmc:/switch/nstv/fonts/OpenSans-Regular.ttf
```

The project does not bundle font files.
