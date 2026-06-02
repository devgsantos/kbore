# Parser API Lazy Nodes Agent Brief

## Goal

Change the parser API contract for NSTV/Kbore so the Nintendo Switch app never has to download and parse the full playlist tree on first import.

The app is performance constrained. Optimize the API for the app, even if that means more API-side indexing/cache work.

Current problem:

- The app freezes/stalls on `Parsing manifest tree` when the first manifest contains a huge recursive `nodes` tree.
- Some nested series structures are incomplete. Example path:
  `PL > TV Shows > Provider > Show > Episodes`
- In that case, `Show` is displayed with count `0`, and the app's `NEXT / ITEMS` list is empty.

The API must stop returning huge full-depth trees and must return complete children for the folder that the app asks to open.

## Required Design

Implement a lazy node API.

The initial manifest endpoint must return only a small, shallow tree. Deeper folders/items must be loaded by a separate children endpoint.

Keep existing endpoints available if needed, but add lazy behavior for the app:

- `POST /api/nodes/parse-url`
- `POST /api/nodes/xtream/manifest`
- `POST /api/nodes/children`

## Initial Manifest Contract

For `POST /api/nodes/parse-url` and `POST /api/nodes/xtream/manifest`, return a lightweight manifest:

```json
{
  "ok": true,
  "manifest": {
    "id": "stable-playlist-id-or-fingerprint",
    "name": "Playlist name",
    "provider": "m3u",
    "totalItems": 123456,
    "nodes": [
      {
        "id": "series",
        "title": "TV Shows",
        "type": "series",
        "kind": "folder",
        "playable": false,
        "totalItems": 34000,
        "childCount": 12,
        "hasChildren": true,
        "children": [
          {
            "id": "series/provider/netflix",
            "title": "Netflix",
            "type": "series",
            "kind": "folder",
            "playable": false,
            "totalItems": 9000,
            "childCount": 600,
            "hasChildren": true,
            "children": []
          }
        ]
      }
    ]
  }
}
```

Rules:

- Do not return the full recursive tree in the initial manifest.
- Return only root nodes and optionally one shallow child layer.
- Every folder node must include:
  - `id`
  - `title`
  - `type`
  - `kind: "folder"`
  - `playable: false`
  - `totalItems`
  - `childCount`
  - `hasChildren`
  - `children`, usually `[]` for unloaded deeper folders
- Every playable node must include:
  - `id`
  - `title`
  - `type`
  - `kind: "item"`
  - `playable: true`
  - `url` or `playbackUrl`
  - optional `logo`
- `id` must be stable across requests for the same playlist.
- Prefer gzip responses.

## Children Endpoint

Add:

`POST /api/nodes/children`

Request:

```json
{
  "url": "playlist-or-xtream-source-url",
  "provider": "m3u",
  "nodeId": "series/provider/netflix",
  "type": "series",
  "page": 1,
  "pageSize": 100
}
```

Response:

```json
{
  "ok": true,
  "nodeId": "series/provider/netflix",
  "type": "series",
  "page": 1,
  "pageSize": 100,
  "totalItems": 600,
  "totalPages": 6,
  "hasNextPage": true,
  "items": [
    {
      "id": "series/provider/netflix/show/breaking-bad",
      "title": "Breaking Bad",
      "type": "series",
      "kind": "folder",
      "playable": false,
      "totalItems": 62,
      "childCount": 62,
      "hasChildren": true,
      "children": []
    }
  ]
}
```

The endpoint may also return `children` instead of `items`, but use `items` as the preferred field.

## TV Show Episode Requirement

This is mandatory.

When the app asks for the children of a show node, the API must return the episodes.

Example request:

```json
{
  "url": "playlist-or-xtream-source-url",
  "provider": "m3u",
  "nodeId": "series/provider/netflix/show/breaking-bad",
  "type": "series",
  "page": 1,
  "pageSize": 100
}
```

Required response:

```json
{
  "ok": true,
  "nodeId": "series/provider/netflix/show/breaking-bad",
  "type": "series",
  "page": 1,
  "pageSize": 100,
  "totalItems": 62,
  "totalPages": 1,
  "hasNextPage": false,
  "items": [
    {
      "id": "series/provider/netflix/show/breaking-bad/s01e01",
      "title": "S01E01 - Pilot",
      "type": "series",
      "kind": "item",
      "playable": true,
      "url": "http://example/episode.ts",
      "logo": "http://example/poster.jpg"
    }
  ]
}
```

Do not return a `Show` folder with:

- `totalItems: 0`
- `childCount: 0`
- `hasChildren: false`
- empty `children`

unless the source truly has no episodes. If episodes exist in the playlist, the show node must expose them through `/api/nodes/children`.

If seasons are available, either shape is acceptable:

1. `Show > Season > Episodes`
2. `Show > Episodes`

If using seasons, show nodes must return season folders, and season folders must return playable episode items.

## Counts

The app uses counts for badges and for deciding what looks empty.

For folder nodes:

- `totalItems` should be the total playable descendants under that folder, not just immediate children.
- `childCount` should be the number of immediate children returned by `/api/nodes/children`.
- If exact `totalItems` is expensive, return the best known count, but do not return `0` when known children exist.

For playable nodes:

- `totalItems` may be `1` or omitted.
- `childCount` should be `0`.
- `hasChildren` should be `false`.

## Node ID Rules

Node IDs must be stable and addressable by `/api/nodes/children`.

Recommended format:

```text
<type>/<provider-or-group-slug>/<folder-slug>/<item-slug-or-source-id>
```

For Xtream, prefer native IDs where available:

```text
series/category/<category_id>/series/<series_id>
series/category/<category_id>/series/<series_id>/season/<season_number>
series/category/<category_id>/series/<series_id>/episode/<episode_id>
```

For M3U, build deterministic IDs from normalized group/title/season/episode data plus a short hash of the URL when needed.

## Performance Requirements

- Initial manifest response target: under 1 MB compressed whenever possible.
- Initial manifest must not include all episodes for large playlists.
- Use gzip compression.
- Cache parsed/indexed playlist data server-side by source URL fingerprint when possible.
- `/api/nodes/children` should return paginated results.
- Default `pageSize`: 100.
- Maximum `pageSize`: 250.
- Children endpoint should avoid reparsing the full playlist on every request if server-side cache is available.

## Backward Compatibility

Keep these fields because the current app parser already understands them:

- `nodes`
- `children`
- `items`
- `id`
- `title`
- `name`
- `type`
- `streamType`
- `kind`
- `url`
- `playbackUrl`
- `logo`
- `totalItems`
- `totalChannels`
- `count`
- `playable`

Add these new fields:

- `hasChildren`
- `childCount`

The current app can be updated to use `hasChildren` and `childCount`, but keep `children: []` in folder nodes so older code still sees a valid node shape.

## App-Side Follow-Up Expected

After the API is ready, the app should be changed to:

- Treat folder nodes with `hasChildren: true` as expandable even when `children` is empty.
- Call `POST /api/nodes/children` when opening an unloaded folder.
- Cache children pages per playlist/node/page on SD.
- Display count using `totalItems`, then `totalChannels`, then `childCount`, then `children.size()`.

## Acceptance Tests

Use at least one large playlist where first import currently freezes.

Test 1: Initial manifest size

- Request `POST /api/nodes/xtream/manifest`.
- Confirm response is shallow.
- Confirm response does not include all episodes.
- Confirm compressed size is small enough for Switch use.

Test 2: TV show children

- Navigate source structure:
  `TV Shows > Provider > Show`
- Request `/api/nodes/children` for the `Show` node.
- Confirm playable episode items are returned.
- Confirm `totalItems` and `childCount` are not `0` when episodes exist.

Test 3: Pagination

- Request a folder with more than `pageSize` children.
- Confirm `totalPages` and `hasNextPage` are correct.
- Confirm page 2 returns the next distinct items.

Test 4: Stable IDs

- Request initial manifest twice for the same source.
- Confirm the same folders have the same IDs.
- Request `/api/nodes/children` using one of those IDs.
- Confirm the node resolves correctly.

Test 5: Playable URLs

- Request children for a leaf series season or show.
- Confirm episode nodes have `playable: true` and `url` or `playbackUrl`.

