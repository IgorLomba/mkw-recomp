# tools/i18n

| File | What | Usage |
|---|---|---|
| `catalog.py` | The menu's text catalogs (`i18n/mkw.pot`, `i18n/<lang>.po`): merges the English template into every translation (changed English marked fuzzy), shows how far each language is, and generates the C arrays `build.py` compiles | `uv run tools/i18n/catalog.py update\|status\|gen DIR` |
