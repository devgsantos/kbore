# Kboré App - EPG Consumption Implementation

This build adds client-side consumption of the parser EPG endpoints and renders EPG information in the application UI.

## What changed

### Models

`include/nstv/models.hpp`

Added EPG-aware fields to `Channel`:

- `tvgId`
- `tvgName`
- `streamId`

Added:

- `EpgProgram`
- `EpgPage`

### Parser API client

`include/nstv/parser_api_client.hpp`  
`source/parser_api_client.cpp`

Added:

```cpp
EpgPage loadEpgPrograms(
  const std::string &sourceUrl,
  Provider provider,
  const Channel &channel,
  int page = 1,
  int pageSize = 12,
  const std::string &manualEpgUrl = ""
) const;
```

Endpoint selection:

- M3U: `POST /api/epg/programs`
- Xtream with `streamId`: `POST /api/xtream/epg/short`
- Xtream without `streamId`: `POST /api/xtream/epg/programs`

The app sends the best available matching fields:

- `channelId` / `tvgId`
- `channelName`
- `streamId`
- optional `epgUrl` for M3U manual override

### Config

`include/nstv/config.hpp`  
`source/config.cpp`

Added optional M3U EPG URL:

```json
{
  "m3u_url": "https://example.com/list.m3u",
  "epg_url": "https://example.com/epg.xml.gz"
}
```

When adding an M3U playlist in the app, the user is now asked for an optional EPG URL. Leaving it empty allows the parser to discover EPG from the M3U header.

### Storage/cache

`include/nstv/storage.hpp`  
`source/storage.cpp`

Added EPG page cache per playlist/channel:

- `epgCacheKey(channel)`
- `epgCachePath(playlistId, channel)`
- `saveEpgPage(...)`
- `loadEpgPage(...)`

Cache key priority:

1. `tvgId`
2. `streamId`
3. `id`
4. `name`

### UI rendering

`source/app.cpp`

Added EPG loading and display:

- EPG is loaded on demand for the selected live channel.
- The channel list subtitle now shows the currently loaded EPG item for visited/selected channels.
- The bottom information panel shows `NOW` and `NEXT` for the selected channel.
- The player overlay also shows the selected channel's `NOW` and `NEXT` EPG line.

EPG is not loaded for movies, series, or radio.

## Defensive behavior

If the EPG request fails, the app does not block playback and does not invalidate the channel list. It displays:

```txt
EPG unavailable
```

The main channel/category flow continues normally.

## Build validation performed

The modified C++ files were syntax-checked with:

```bash
g++ -std=c++17 -Iinclude -c source/config.cpp
g++ -std=c++17 -Iinclude -c source/parser_api_client.cpp
g++ -std=c++17 -Iinclude -c source/storage.cpp
g++ -std=c++17 -Iinclude -c source/app.cpp
```
