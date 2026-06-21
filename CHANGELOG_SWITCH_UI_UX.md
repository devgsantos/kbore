# Kbore Switch UI/UX Changelog

## 2026-06-21

- Added Switch-native parental controls in Settings.
- Added persistent numeric parental PIN support with default `0000`.
- Added category rules for `Hide`, `Lock`, and `Unlock`, saved to `config.json`.
- Added PIN verification before changing parental rules, including unlocking rules.
- Added locked-category PIN prompts before opening protected categories/items.
- Added hidden-category filtering for regular category manifests and dynamic node manifests.
- Added denied-access messaging after three wrong PIN attempts.
- Added configurable manifest refresh interval using local cache freshness.
- Added configurable on-demand EPG cache refresh interval.
- Preserved on-demand EPG fetching to avoid parser-server overload.
- Preserved Switch device timezone EPG synchronization through existing libnx time APIs.
- Added language preference persistence for English, Portuguese, and Spanish.
- Improved movies/series item cards and selected-item footer details with VOD-specific metadata from the manifest.
- Added Switch adaptation notes for features that should not be copied directly from Android behavior.

## Build Verification

- Built successfully with:

```sh
make -f Makefile.switch
```

- Output:

```text
kbore.nro
```
