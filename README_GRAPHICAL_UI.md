# NSTV graphical dashboard

This version replaces the console/table dashboard with a native software-rendered 1280x720 dashboard.

Highlights:
- Header, three dashboard columns, info bar and control footer based on the NSTV mockup.
- Native rounded panels, gradients, selected-row glow, badges and type icons.
- Real playlist/category/channel data from the Parser/Xtream API.
- Channel thumbnail placeholders generated from channel name/logo URL; if a channel has a logo URL, the placeholder is deterministic and marked visually. Full remote PNG/JPEG decoding is intentionally isolated for a future `ImageCache`/decoder step.
- Host preview writes `nstv-frame.ppm` on each render.
- Switch build presents through libnx framebuffer instead of the console renderer.

Host preview:

```bash
make clean
make host
printf 'q\n' | ./build/nstv-native-host
# open nstv-frame.ppm
```

Switch build:

```bash
make clean
make switch
```

The native Joy-Con input path was not changed.
