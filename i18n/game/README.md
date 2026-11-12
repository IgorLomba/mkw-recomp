# i18n/game

The game's own texts in languages it does not ship, over one it does: `ca`, `gl` and `eu` over its Spain Spanish.
Each `<lang>.tsv` has one `label<TAB>text` line per translated text; labels are the game's (`MsgMain/Menu/2203`), and
only our translation is here, never Nintendo's text. A label left out stays in Spanish.

`tools/i18n/game_texts.py` lists what is left (`todo ca`, from your own copy of the game, with its control tags such
as `{0E ...}` to keep as they are) and builds the files the game reads (`build ca`); the setup builds them for the
player into `lang/<lang>/`, and choosing the language in Settings > Game shows them.

| File | Language |
|---|---|
| `ca.tsv` | Catalan |
| `gl.tsv` | Galician |
