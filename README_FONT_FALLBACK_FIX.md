# Font fallback fix

This version restores a readable built-in bitmap fallback.

Why:
- If SDL_ttf cannot open the TTF on Switch, the previous package drew no text.
- This version keeps trying Roboto/OpenSans first, but if TTF fails, the UI still draws readable letters using the built-in fallback.

Recommended SD layout:

```text
/switch/nstv/nstv-native.nro
/switch/nstv/config.json
/switch/nstv/fonts/Roboto-Regular.ttf
/switch/nstv/fonts/Roboto-Medium.ttf
/switch/nstv/fonts/ConcertOne-Regular.ttf
```

If TTF loads successfully, it uses Roboto.
If TTF fails, it uses the built-in fallback instead of blank text or white squares.
