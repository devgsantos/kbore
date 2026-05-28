# NSTV text/EPG/border cleanup

Changes:

1. Category/channel text normalization
   - Decorative icons, emoji and variation selectors are stripped from display text.
   - Example: "♦️ CENTRAL DA COPA 2026 ⚽" becomes "CENTRAL DA COPA 2026".
   - Common Latin accents are transliterated to ASCII to avoid broken glyphs on fallback font.

2. Channel subtitle
   - The second line under each channel no longer shows the category name.
   - It now shows: "EPG indisponivel".

3. White corner/outline cleanup
   - Removed white translucent borders from panels, cards, badges, logo boxes and footers.
   - Replaced them with darker blue-gray borders to avoid the white contour pattern.

Notes:
- Real EPG loading can later replace the fixed "EPG indisponivel" text.
- This patch keeps SDL_ttf + Roboto as priority and bitmap fallback as safety.
