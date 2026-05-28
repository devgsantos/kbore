# NSTV Native - Local cache and dashboard preload update

Changes included in this package:

- Keeps the existing native Switch input implementation untouched.
- Saves the active manifest in `active-manifest.json`.
- Adds local channel page cache under:
  - Switch: `sdmc:/switch/nstv/cache/`
  - Host: `./cache/`
- When loading a category, the app checks cache first.
- If a page is not cached, it calls the Parser/Xtream API and stores the page locally.
- Channel pagination now preloads the next page when the selected item is within the last 5 loaded items, instead of waiting for the absolute end.
- Dashboard rendering was adjusted to a three-column layout inspired by the previous JS dashboard:
  - Stream Types
  - Categories
  - Channels

Build:

```bash
make host
make switch
```
