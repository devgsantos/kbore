# Kboré

Kboré is a user-experience-first IPTV and VOD player for Nintendo Switch, with
full Joy-Con, Pro Controller, and handheld touchscreen navigation. Add an M3U
URL or Xtream account, browse live TV, movies, series, and radio, and play
content supplied by your own provider.

Kboré does not include playlists, channels, movies, series, servers, or
streaming content. Users are responsible for the sources and credentials they
add to the app.

Key Switch features include:

- controller and touchscreen navigation that share the same focus and selection;
- direct tap support for dashboard items, playlists, Settings, and Parental
  Control;
- per-column touch scrolling, with drag detection to prevent accidental opens;
- touch playback controls for pause/resume, seeking, timeline dragging, and
  returning to the dashboard;
- a left-edge swipe-right gesture to exit playback;
- Nintendo Switch software keyboard input for playlist data, Search, and PINs.

## User Guide

### Install and launch

Download `kbore.nro` and place it on the SD card:

```text
sdmc:/switch/kbore/kbore.nro
```

Launch Kboré from the Homebrew Menu. The app creates and maintains its settings,
playlist manifests, favorites, EPG pages, and other cache files under:

```text
sdmc:/switch/kbore/
```

Kboré is designed for both handheld and docked use. Joy-Con and Pro Controller
navigation remain available in both modes; the touchscreen is available in
handheld mode.

### Add your first playlist

1. Open **Playlists** with the `+` button or by touching the active playlist
   selector at the top of the dashboard.
2. Choose **Add M3U URL** or **Add Xtream**.
3. Enter the requested name, URL, server, username, and password with the
   Nintendo Switch software keyboard.
4. Wait while Kboré downloads and prepares the manifest.
5. The dashboard opens automatically when the playlist is ready.

M3U sources accept a URL. Importing a local M3U file is not currently supported.
Xtream sources require the server URL and account credentials supplied by the
provider.

Saved playlists appear in the Playlists screen. Switching between them reuses
the per-playlist cache when it is still valid, avoiding a full download on every
switch.

### Dashboard

The dashboard is organized into three browsing columns:

1. **Root / Stream Types** — Search, Favorites, Live TV, Movies, Series, Radio,
   and other roots supplied by the playlist.
2. **Categories** — groups or folders inside the selected root.
3. **Next / Items** — channels, movies, shows, seasons, episodes, or deeper
   folders.

The highlighted item is also the controller focus. Touching an item updates the
same selection, so users can alternate between touch and controller navigation.
When playback ends, Kboré returns to the related playlist node and selection.

Live channels show available now/next EPG information. Movies and series use
VOD-oriented information instead of live-only EPG text. When the parser/provider
offers deeper movie or series details, Kboré requests them on demand and caches
the result; otherwise it falls back to information already present in the
manifest.

### Controller controls

#### Dashboard

| Control | Action |
| --- | --- |
| D-pad | Move between columns and items |
| `A` | Open a root/folder, load a category, or play the selected item |
| `B` | Return to the previous column or folder |
| `X` | Switch the item panel between list and grid view |
| `Y` | Add or remove the selected playable item from Favorites |
| `L` / `R` | Move backward or forward by 10 items |
| `+` | Open Playlists |

#### Playback

| Control | Action |
| --- | --- |
| `A` | Show controls, then pause or resume |
| `B` | Stop playback and return to the dashboard |
| D-pad left/right | Seek backward/forward 10 seconds in VOD |
| `L` / `R` | Seek backward/forward 30 seconds in VOD |

The first playback command wakes a hidden overlay without accidentally applying
the command. Press the command again while the overlay is visible. `B` remains
the immediate controller exit.

### Touchscreen controls

#### Browsing

- Tap a root, category, folder, or item to select and open it directly.
- Drag vertically inside a dashboard column to scroll that column.
- Tap the heart at the right side of a playable item to toggle Favorite without
  opening it.
- Tap the active playlist selector to open Playlists.
- Tap the settings icon in the upper-right corner to open Settings.
- In Playlists, Settings, and Parental Control, drag vertically to scroll.

A drag cancels item activation, preventing a channel or folder from opening
when the intention was to scroll.

#### Playback

- Tap once to reveal hidden playback controls.
- Tap the center of the video or the visible **Pause/Play** control to pause or
  resume.
- Tap **-30**, **-10**, **+10**, or **+30** when playing seekable VOD.
- Tap or drag the timeline to seek directly.
- Tap **Back**, or start at the left edge and swipe right, to stop playback and
  return to the dashboard.

The exit swipe must begin near the left edge and move horizontally to the right.
Touch activity also resets an enabled docked playback sleep timer.

### Search and Favorites

Select **Search** in the first column and use the software keyboard to enter a
query. Search results can be opened like normal playlist items. After playback,
Kboré clears the transient results and restores the matching location in the
playlist tree when it can identify it.

Select **Favorites** to browse items saved for the active playlist. Use `Y` or
tap the heart to add/remove playable content. Favorites are stored separately
for each saved playlist.

### Settings

Settings are saved in `config.json`. Available options include:

- **EPG time offset** — per-playlist correction for provider EPG data that is
  early or late. The Switch device timezone is applied automatically first.
- **Playback sleep behavior** — choose when playback should prevent system
  sleep.
- **Docked sleep timer** — optional bedtime timer during TV/docked playback.
- **Battery sleep warning** and **warning lead time**.
- **Parental Control**.
- **Manifest refresh** — controls when a cached playlist manifest is considered
  stale.
- **EPG refresh** — controls when cached EPG pages are considered stale. EPG is
  still loaded on demand to avoid unnecessary parser/server traffic.
- **Language** — stores English, Portuguese, or Spanish preference.

With the controller, use up/down to select, left/right to change, `L`/`R` for
larger steps where available, `Y` to reset an option, `A` to open an action, and
`B` to return. With touch, tap the left or right side of a setting to decrement
or increment it, and drag vertically to scroll.

The language preference is persisted, but some older dashboard and playback
strings remain in English while translation coverage is expanded.

### Parental Control

The default numeric PIN is `0000`. Change it before relying on parental rules.
PIN entry uses the Nintendo Switch software keyboard.

Rules are configured per category/root:

- **Unlock** — content is shown and opens normally.
- **Lock** — the category remains visible but requires the PIN before content is
  listed or played.
- **Hide** — the category/root is removed from normal navigation.

After three incorrect attempts, access is denied and the dashboard reports that
the user does not have permission to watch the content. Unlocking a protected
rule from Settings also requires the current PIN.

Use left/right to change stream-type tabs, up/down to choose a category, `A` to
cycle its rule, `Y` to change the PIN, and `B` to return. The tabs, category
rows, PIN action, and Back action are also touch-enabled.

### Cache, EPG, and refreshing content

Kboré stores manifests, child pages, channel pages, VOD details, and EPG data so
navigation stays responsive and switching playlists does not always contact the
parser again. Manifest and EPG refresh intervals can be changed in Settings.

If a provider changed its playlist but Kboré continues showing stale content,
remove only that playlist's related files from `manifests/` and `cache/`, then
open/import the playlist again. Avoid deleting the entire Kboré directory unless
you also intend to remove settings and saved playlist information.

### User troubleshooting

- **Playlist does not load:** verify the source URL/account and network access.
- **EPG is shifted:** adjust the active playlist's EPG time offset in Settings.
- **Text displays as boxes:** verify the packaged fonts are present and avoid
  unsupported provider icon glyphs.
- **Touch does not respond:** touchscreen input is available only in handheld
  mode; use Joy-Con or Pro Controller while docked.
- **A swipe does not leave playback:** start within the left edge of the screen
  and swipe right horizontally, or use the visible Back control / `B` button.
- **A movie cannot seek:** seeking is shown only when the stream reports a valid
  duration and the player identifies it as seekable VOD.

---

## Homebrew Development and Player Build

This repository contains the native Kboré homebrew app, including the app UI,
playlist/cache handling, and the native media player pipeline backed by a custom
FFmpeg build with `nvtegra` patches.

Current player state:

- Native UI, controller input, stream cache, and stream listing are working.
- Custom FFmpeg builds locally and exports the SDK into `external/player-sdk`.
- The `nvtegra` FFmpeg backend is present and detected by the native probe.
- `AVHWDeviceContext` creation succeeds.
- Video opens through NVTEGRA hardware decode.
- Audio runs through SDL Audio.
- The Switch-native player can present supported NVTEGRA frames through the
  Deko3D direct-import renderer.
- Software frame transfer/rendering remains available as a fallback when a
  stream cannot use the direct native path.

Some low-level toolchain package names, filesystem mount points, build defines,
and source files still contain target-specific or legacy identifiers because the
homebrew SDK and existing code use those exact names.

---

## Repository Layout

Expected project root:

```bash
~/kbore
```

Relevant paths:

```text
kbore/
├── Makefile
├── Makefile.switch
├── include/
├── source/
├── scripts/
│   ├── build-ffmpeg-nx.sh
│   ├── check-ffmpeg-nx.sh
│   ├── patch-ffmpeg-nx-switch.sh
│   ├── apply-ffmpeg-nvtegra-0001-manual.sh
│   ├── apply-ffmpeg-nvtegra-0002-manual.sh
│   └── apply-ffmpeg-nvtegra-from-0003-safe.sh
├── patches/
│   └── ffmpeg-nvtegra/
├── external/
│   ├── ffmpeg-nx/
│   ├── mpv-nx/
│   └── player-sdk/
├── romfs/
└── data/
```

`external/ffmpeg-nx` and `external/mpv-nx` are Git submodules that point to the
Kboré forks:

```text
https://github.com/devgsantos/ffmpeg-nx.git
https://github.com/devgsantos/mpv-nx.git
```

`external/player-sdk` is generated by the FFmpeg build script.

---

## Clone and Submodules

For a fresh checkout:

```bash
git clone --recurse-submodules https://github.com/devgsantos/kbore.git
cd kbore
```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive
```

To refresh the external forks to the commits recorded by this repository:

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

To intentionally update a submodule to a newer fork commit:

```bash
cd external/ffmpeg-nx
git fetch origin
git checkout feat/updated-readme
git pull --ff-only
cd ../..
git add external/ffmpeg-nx
```

Repeat the same flow for `external/mpv-nx` when needed.

---

## Development Environment

The homebrew build uses devkitPro, devkitA64, libnx, SDL2, curl, mbedTLS, zlib,
bzip2, xz/lzma, deko3d, and the static FFmpeg SDK generated from the fork.

Expected toolchain paths:

```bash
/opt/devkitpro
/opt/devkitpro/devkitA64
/opt/devkitpro/libnx
/opt/devkitpro/portlibs/switch
```

Set the environment:

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITA64=$DEVKITPRO/devkitA64
export PATH=$DEVKITA64/bin:$PATH
```

Add the same lines to `~/.bashrc` or `~/.zshrc` if you want them loaded for each
terminal session.

Verify the compiler:

```bash
echo $DEVKITPRO
echo $DEVKITA64
which aarch64-none-elf-gcc
aarch64-none-elf-gcc --version
```

Install the base packages:

```bash
sudo dkp-pacman -Syu
sudo dkp-pacman -S switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-curl switch-mbedtls switch-zlib switch-bzip2 switch-xz switch-deko3d switch-mesa
```

Package names can vary by devkitPro repository state. If a package name is not
found, search the package database:

```bash
dkp-pacman -Ss switch | grep -Ei 'sdl2|curl|mbedtls|zlib|bzip2|xz|lzma|deko3d|mesa'
```

The app also expects host tools commonly available on a Linux development
machine:

```bash
git
make
bash
pkg-config
python3
grep
sed
```

---

## External Player Sources

The custom FFmpeg fork is the source of truth for the patched `nvtegra` backend.
Do not copy the full FFmpeg or mpv source trees into the main repository as
regular files.

The main repository should track only the submodule commit references:

```bash
git status --short
git submodule status
```

Expected submodule branches in this checkout:

```text
external/ffmpeg-nx -> feat/updated-readme
external/mpv-nx    -> main
```

If you change FFmpeg patches:

```bash
cd external/ffmpeg-nx
git status
git add configure libavcodec libavformat libavutil
git commit -m "Update Kboré NVTEGRA player patches"
git push origin feat/updated-readme
cd ../..
git add external/ffmpeg-nx
```

If you change mpv integration code:

```bash
cd external/mpv-nx
git status
git add .
git commit -m "Update Kboré mpv homebrew integration"
git push origin main
cd ../..
git add external/mpv-nx
```

---

## Build FFmpeg SDK

The app links against `external/player-sdk`, generated from `external/ffmpeg-nx`.
Rebuild it whenever the FFmpeg fork changes or when the SDK is missing.

From the repository root:

```bash
rm -rf external/player-sdk
./scripts/build-ffmpeg-nx.sh
./scripts/check-ffmpeg-nx.sh
```

The FFmpeg configure step must include:

```bash
--target-os=horizon
--enable-gpl
--enable-nvtegra
--enable-pic
```

The build flags must keep PIE/PIC enabled:

```bash
-fPIC
-fPIE
-specs=$DEVKITPRO/libnx/switch.specs
```

Recommended decoder/parser coverage:

```bash
--enable-parser=h264,hevc,aac,ac3,mpeg4video,mpegaudio,vp8,vp9
--enable-decoder=h264,hevc,aac,mp3,ac3,eac3,mpeg2video,mpeg4,vp8,vp9
```

Verify the generated SDK:

```bash
./scripts/check-ffmpeg-nx.sh
aarch64-none-elf-nm external/player-sdk/lib/libavcodec.a | grep -Ei 'nvtegra|h264_nvtegra|hevc_nvtegra'
aarch64-none-elf-nm external/player-sdk/lib/libavutil.a | grep -Ei 'nvtegra|hwcontext_nvtegra'
```

Expected symbols include:

```text
h264_nvtegra
hevc_nvtegra
vp8_nvtegra
vp9_nvtegra
hwcontext_nvtegra
```

---

## Apply FFmpeg Patches

The patched fork should normally already contain the required NVTEGRA changes.
Use the patch scripts only when rebuilding or replaying the patch set from a
clean FFmpeg base.

Patch files live in:

```text
patches/ffmpeg-nvtegra/
```

Confirm they are real patch files:

```bash
grep -n "diff --git" patches/ffmpeg-nvtegra/*.patch
```

Apply the patch helpers from the repository root:

```bash
./scripts/patch-ffmpeg-nx-switch.sh
./scripts/apply-ffmpeg-nvtegra-0001-manual.sh
./scripts/apply-ffmpeg-nvtegra-0002-manual.sh
./scripts/apply-ffmpeg-nvtegra-from-0003-safe.sh
```

Verify the FFmpeg tree:

```bash
cd external/ffmpeg-nx
grep -R "enable-nvtegra\|nvtegra\|AV_HWDEVICE_TYPE_NVTEGRA" configure libavcodec libavutil -n | head -80
cd ../..
```

---

## Build Kboré

Make sure `external/player-sdk` exists before building the app.

```bash
make clean
make switch
```

Expected outputs:

```text
kbore.elf
kbore.nro
kbore.nacp
```

`Makefile.switch` should include the generated SDK before portlibs:

```makefile
PLAYER_SDK := $(APP_ROOT)/external/player-sdk
PLAYER_SDK_INC := $(PLAYER_SDK)/include
PLAYER_SDK_LIB := $(PLAYER_SDK)/lib

export INCLUDE := -I$(PLAYER_SDK_INC) ...
export LIBPATHS := -L$(PLAYER_SDK_LIB) ...
```

The player build currently enables the native hardware path in `Makefile.switch`.

---

## Runtime Files

Place the generated app under the homebrew app folder on the SD card:

```text
sdmc:/switch/kbore/kbore.nro
```

Suggested SD layout:

```text
sdmc:/switch/kbore/
├── kbore.nro
├── config.json
├── fonts/
│   └── OpenSans-Regular.ttf
├── manifests/
└── cache/
```

Example `config.json`:

```json
{
  "parserApiBaseUrl": "https://your-parser-api.example",
  "apiKey": "your-api-key",
  "active": "my-m3u-list",
  "pageSize": 20,
  "preloadThreshold": 8,
  "useUnicodeIcons": false,
  "playlists": [
    {
      "id": "my-m3u-list",
      "name": "My M3U List",
      "type": "m3u",
      "active": true,
      "m3u_url": "http://host:port/get.php?username=USER&password=PASS&type=m3u_plus&output=ts",
      "epg_url": "http://host:port/xmltv.php?username=USER&password=PASS"
    },
    {
      "id": "my-xtream-list",
      "name": "My Xtream List",
      "type": "xtream",
      "server_url": "http://host:port",
      "username": "USER",
      "password": "PASS"
    }
  ],
  "defaultXtreamUrl": "http://server:port",
  "defaultPlaylistUrl": "http://host:port/playlist.m3u"
}
```

`defaultXtreamUrl` and `defaultPlaylistUrl` are legacy compatibility fields.
New saved lists should live in `playlists`, and the selected list is stored in
`active` / `active_playlist_id`.

If text renders as boxes, verify the font file exists:

```text
sdmc:/switch/kbore/fonts/OpenSans-Regular.ttf
```

For playlist names, channel names, and categories with unsupported icon glyphs,
normalize or remove the icons before rendering.

---

## Playlist Loading and Cache

Kboré loads M3U and Xtream sources through the configured parser API. The app
expects the parser to return a lightweight initial manifest and to load deeper
folders through child-page requests instead of returning a full recursive tree
on first import.

The important parser endpoints are:

```text
POST /api/nodes/parse-url
POST /api/nodes/xtream/manifest
POST /api/nodes/children
POST /api/xtream/epg/short
```

The app stores cached data by playlist id so switching saved lists does not
force a full re-download every time.

Current cache paths:

```text
sdmc:/switch/kbore/manifests/<playlist-id>_manifest.json
sdmc:/switch/kbore/manifests/<playlist-id>_manifest.json.gz
sdmc:/switch/kbore/cache/<playlist-id>_<provider>_<type>_<category>_page_<n>.json
sdmc:/switch/kbore/cache/<playlist-id>_node_<node-id>_page_<n>.json
sdmc:/switch/kbore/cache/<playlist-id>_epg_<channel-key>.json
```

Legacy manifest files are still read for compatibility:

```text
sdmc:/switch/kbore/manifests/active-manifest.json
sdmc:/switch/kbore/active-manifest.json
sdmc:/switch/kbore/manifest-<playlist-id>.json
sdmc:/switch/kbore/cache/<playlist-id>_manifest.json
sdmc:/switch/kbore/cache/<playlist-id>_manifest.json.gz
```

If an existing list keeps loading stale data, remove only that playlist's cached
manifest and page files from `manifests/` and `cache/`, then import it again.

---

## Player Pipeline

Preferred Switch video path:

```text
NativeDemuxer
-> custom FFmpeg + nvtegra
-> AVHWDeviceContext nvtegra
-> NativeDecoder::openVideoHardware()
-> AVFrame pix_fmt=nvtegra
-> Deko3D direct frame import
-> native presentation
```

Fallback video path:

```text
Decoded frame
-> av_hwframe_transfer_data()
-> YUVFrame / software frame
-> application renderer
```

Current audio path:

```text
NativeDemuxer
-> audio packets
-> FFmpeg audio decoder
-> SwrContext
-> S16 stereo 48 kHz
-> SDL_QueueAudio()
```

Direct import is preferred, but availability still depends on the codec, frame
layout, decoder output, and stream. The fallback path keeps playback compatible
when direct import is unavailable.

---

## Native Probe Expectations

After the FFmpeg SDK is built with NVTEGRA support, the native probe should show:

```text
configs=1
pix_fmt=nvtegra
device=nvtegra
methods=none
usableDeviceConfig=yes
createdDevice=yes
selected=#0
```

The `nvtegra` backend can report `methods=none`. The probe should still accept
the config when `device=nvtegra` and `pix_fmt=nvtegra` are present.

Useful log filters:

```text
[KBORE][NVTEGRA]
[KBORE][DEKO3D]
```

On Switch, runtime diagnostics are appended to:

```text
sdmc:/switch/kbore/kbore.log
```

---

## Common Build Issues

### `TCP_MAXSEG undeclared`

Run:

```bash
./scripts/patch-ffmpeg-nx-switch.sh
```

### `read-only segment has dynamic relocations`

The FFmpeg static libraries were likely built without PIE/PIC. Rebuild the SDK
with:

```bash
--enable-pic
-fPIC
-fPIE
```

For temporary validation only, test without assembly optimizations:

```bash
--disable-asm
--disable-neon
```

### `configs=0`

The app is not using an FFmpeg SDK with the NVTEGRA backend. Rebuild and verify:

```bash
rm -rf external/player-sdk
./scripts/build-ffmpeg-nx.sh
./scripts/check-ffmpeg-nx.sh
```

Also confirm `Makefile.switch` uses `external/player-sdk` before portlibs:

```bash
grep -n "PLAYER_SDK\|LIBPATHS\|INCLUDE\|LIBS" Makefile.switch
```

### `usableDeviceConfig=no` with `pix_fmt=nvtegra`

The probe must accept `nvtegra` even when `methods=none`.

### `could not initialize SwrContext: Invalid argument`

Likely audio stream metadata is missing or unexpected. Check the audio fallback
helpers:

```text
safeAudioSampleRate()
safeAudioSampleFormat()
safeInputLayout()
makeDefaultLayout()
```

Also confirm the FFmpeg build includes the required audio decoders and parsers.

---

## Quick Commands

Rebuild the FFmpeg SDK:

```bash
rm -rf external/player-sdk
./scripts/build-ffmpeg-nx.sh
./scripts/check-ffmpeg-nx.sh
```

Rebuild Kboré:

```bash
make clean
make switch
```

Check submodules:

```bash
git submodule status
git -C external/ffmpeg-nx status --short --branch
git -C external/mpv-nx status --short --branch
```

Check generated symbols:

```bash
aarch64-none-elf-nm external/player-sdk/lib/libavcodec.a | grep -Ei 'nvtegra|h264_nvtegra|hevc_nvtegra'
aarch64-none-elf-nm external/player-sdk/lib/libavutil.a | grep -Ei 'nvtegra|hwcontext_nvtegra'
```

---

## License Note

The NVTEGRA FFmpeg backend requires:

```bash
--enable-gpl
```

If Kboré is distributed with binaries linked against that FFmpeg build, treat the
distribution as GPL-compatible and provide the corresponding source according to
the applicable FFmpeg license terms.


Kboré does not provide IPTV lists, channels, movies, series, servers, or streaming content. The app is only a player. All playlists, sources, and content are provided by the user and are the user’s responsibility. Kboré is not affiliated with, sponsored by, or endorsed by any IPTV provider or streaming server.
