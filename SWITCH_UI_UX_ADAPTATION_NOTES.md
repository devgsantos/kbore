# Kbore Switch UI/UX Adaptation Notes

## Implemented for Switch

- Parental control is implemented as a Switch-native settings screen:
  - Default numeric PIN: `0000`.
  - PIN entry uses the Nintendo Switch software keyboard instead of Android remote/physical keyboard assumptions.
  - Category management uses stream-type tabs and joystick navigation.
  - Rules are saved in `config.json` as `parental_rules`.
  - `Hide` removes a category/root from navigation.
  - `Lock` asks for the PIN before listing or playing content.
  - After three wrong attempts, access is denied and the dashboard message shows: `You don't have permission to watch this content.`
  - `Unlock` also requires the PIN from the settings screen.

- Movies and series cards now show VOD-oriented details in list/grid rows and in the selected item footer instead of live-only EPG text.

- Manifest and EPG refresh settings are persisted:
  - `manifest_refresh_hours` controls when cached manifests become stale.
  - `epg_refresh_hours` controls when cached EPG pages become stale.
  - EPG remains on demand to avoid parser server overload.

- EPG time handling uses the Switch device timezone through libnx time APIs. The existing per-playlist EPG offset remains available for provider-specific corrections.

- Language preference is persisted as `ui_language` with English, Portuguese, and Spanish options.

## Switch-Specific Differences

- Android remote-control behavior was not copied directly. Switch navigation uses Joy-Con/Pro Controller buttons:
  - D-pad/left stick style navigation maps through the existing `Button` abstraction.
  - Text/PIN input uses the Switch software keyboard.
  - Settings use `A`, `B`, `X`, `Y`, `L`, and `R` semantics already used by the app.

- Full runtime translation of every existing hardcoded string is not complete yet. The language setting is stored and exposed now, but the app still has legacy English strings across older dashboard/playback areas. The recommended next step is to replace hardcoded UI strings incrementally with a small translation table, screen by screen, starting with Settings, Dashboard footer, EPG labels, and playback overlay.

- Deep provider metadata for movies/series, such as synopsis, cast, season lists, or TMDB-style details, is not fetched yet. The current Switch card uses locally available manifest fields so navigation stays fast and cache-friendly. The recommended next step is to add an on-demand details endpoint/client call for the selected VOD item and cache that response separately.
