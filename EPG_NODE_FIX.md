# Kboré EPG node-tree fix

This build keeps the dynamic node navigation intact and fixes the EPG trigger when the focused live item comes from the node tree instead of the legacy `loadedChannels` list.

## Fixed

- `loadSelectedEpg()` now resolves the focused live item from `selectedPreviewNode()` when the manifest uses `nodes`.
- `loadVisibleEpgForChannelList()` now queues EPG jobs for visible playable node items.
- `MediaNode` now carries EPG metadata: `tvgId`, `tvgName`, and `streamId`.
- `channelFromNode()` propagates those fields into `Channel`, allowing:
  - M3U/XMLTV matching by `tvgId` / `tvgName` / `name`;
  - Xtream short EPG matching by `streamId`.
- The node list now renders the EPG line for playable live items instead of the fixed `PLAYABLE` text.
- Pressing `A` on a focused parent can now enter/render an empty child list. Empty results are treated as a valid loaded state, not as a navigation failure.

## Expected logs

When moving focus over live node items or entering a folder with live items, logs should include:

```txt
[KBORE] queueing N EPG request(s) for visible live channels
[KBORE] loading EPG for channel='...' provider='...' key='...'
[KBORE] EPG request path=/api/epg/programs ...
```

For Xtream streams with `streamId`:

```txt
[KBORE] EPG request path=/api/xtream/epg/short ...
```

## Navigation behavior preserved

The patch does not replace the node-tree flow. It only changes the conditions that previously blocked:

1. EPG calls for focused node items.
2. Entering a loaded-but-empty child list after pressing `A`.
