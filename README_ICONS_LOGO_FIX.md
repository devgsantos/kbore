# NSTV UI fixes: icons, logo fallback, focus borders

This package includes the requested UI fixes:

- The channel/movie thumbnail area now prioritizes a real image from the `logo` field.
- PNG and PPM images can be decoded and drawn. If the image URL is JPEG/WebP/SVG or fails to decode, a generic media icon is shown instead of a numeric acronym.
- Numeric codes are no longer shown as the logo fallback.
- The inner circular corner artifacts were removed from rounded rectangles and focus borders.
- Header icons are now drawn as vector icons for stream types/layers, categories, channels and settings.
- Footer/status font sizes remain reduced.

Important:
- No third-party font file is bundled.
- The renderer still uses the built-in bitmap text renderer. To use true Open Sans, add a TTF renderer and load a font file from the SD card, for example `sdmc:/switch/nstv/fonts/OpenSans-Regular.ttf`.
- Remote JPEG/WebP logos still need either a full image decoder library or an API image proxy that returns PNG/PPM.
