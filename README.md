# NSTV Native C++

Native C++ migration of the NSTV prototype. It keeps the current three-column UI model:

- Stream Types
- Categories
- Channels

It uses your Parser API endpoints:

- `POST /api/parse-url` for M3U manifests/channels
- `POST /api/xtream/manifest` for Xtream manifests
- `POST /api/xtream/channels` for Xtream category pages

## Host test

```bash
cp config.example.json config.json
# edit config.json
make host
make run
```

Host controls:

- `W/S/A/D`: move
- `E`: select/load/play
- `X`: favorite
- `M`: Add Playlist
- `B`: back
- `Q`: quit

## Switch build

Requires devkitPro/libnx and curl portlibs.

```bash
make switch
```

Copy:

```text
nstv-native.nro -> /switch/nstv/nstv-native.nro
config.json     -> /switch/nstv/config.json
```

On Switch, use Joy-Con/D-pad:

- D-pad: move
- A: select/load/play
- B: back
- X: favorite
- +: Add Playlist
- -: quit

## Current scope

Functional:

- C++ app shell
- C++ dashboard UI based on the nx.js prototype
- M3U Parser API manifest loading
- Xtream manifest loading
- Category channel loading with pagination
- Favorite marking in-memory
- Player selection screen stub

Not implemented yet:

- Real video playback
- On-screen keyboard
- Persistent saved playlists/cache
- Graphical SDL/deko3d UI

This migration intentionally removes nx.js from the runtime path. The remaining TypeScript app is left in the repository only as historical/reference code.
# nstv
