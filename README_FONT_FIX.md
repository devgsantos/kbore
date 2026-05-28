# Font fix

This package fixes the white-square text fallback.

What changed:

- Bundled the fonts sent by the user inside `fonts/`.
- SDL_ttf now looks first for:
  - `Roboto-Regular.ttf`
  - `Roboto-Medium.ttf`
  - `Roboto_SemiCondensed-Regular.ttf`
  - `ConcertOne-Regular.ttf`
- On Switch, the same fonts must be available at:
  - `sdmc:/switch/nstv/fonts/Roboto-Regular.ttf`
- If SDL_ttf cannot open a font, the renderer no longer draws white blocks in place of letters.

Switch copy layout:

```text
/switch/nstv/nstv-native.nro
/switch/nstv/config.json
/switch/nstv/fonts/Roboto-Regular.ttf
/switch/nstv/fonts/Roboto-Medium.ttf
/switch/nstv/fonts/ConcertOne-Regular.ttf
```

Build dependencies still need SDL2_ttf correctly linked with HarfBuzz/Freetype on Switch.
