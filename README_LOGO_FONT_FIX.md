# NSTV graphical UI adjustments

Changes in this package:

- Channel/movie logo area now prioritizes the `logo` URL from the API.
- A lightweight PNG decoder/cache was added for real PNG logos.
- If the logo is missing, unsupported, or fails to download, the UI falls back to a smaller acronym placeholder.
- The first footer/status panel font sizes were reduced further.
- Rounded focus rectangles were fixed to avoid the old circular artifacts on the corners.
- Text clipping remains bounded so items do not overflow the right edge.

Notes:

- PNG logos are supported by the built-in lightweight decoder.
- JPEG/WebP logos currently fall back to the acronym placeholder. To support all remote image formats, the next step is adding a full image decoder library or an image proxy that converts logos to PNG/PPM.
- No font files are bundled. The current renderer uses the built-in native bitmap text renderer; it is ready for a future TTF renderer/font-loader if you want true Open Sans rendering.
