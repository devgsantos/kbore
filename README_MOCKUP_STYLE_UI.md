# NSTV Mockup Style Dashboard

This build replaces the old ASCII wireframe dashboard with an ANSI-styled dashboard that follows the JS mockup structure:

- Header with NSTV/IPTV Player identity
- Stream Types, Categories and Channels columns
- Highlighted selected row
- Footer/status section with page, loaded/total and provider/category information
- Channel image indication with the `[IMG]` badge and logo URL in the details area
- UTF-8 safer cropping remains in place for accents and category icons

Note: this version still uses the libnx console renderer. It removes the wireframe look, but real image thumbnails/logos require a graphical renderer such as SDL2/deko3d and texture loading.
