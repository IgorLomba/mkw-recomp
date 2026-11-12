# i18n

The texts of the in-game menu (F1), the start screen, the setup page and the performance overlay, as gettext files.
The game's own texts are not here: the game ships them in 16 languages and picks one with `lang` in `mkw.ini`.

| File | Contents |
|---|---|
| `mkw.pot` | The English: each entry's `msgctxt` is its id in `runtime/gfx/nvn/host/ui_text.h`, its `msgid` the English text |
| `ja.po`, `fr.po`, `de.po`, `it.po`, `es.po`, `nl.po`, `pt.po`, `pt-BR.po`, `ru.po`, `zh-Hans.po`, `zh-Hant.po`, `ko.po` | One translation per menu language |

`tools/build/build.py` compiles them into the runtime (`tools/i18n/catalog.py`). An empty or fuzzy entry shows the
English. To fix a translation, edit its `msgstr` (Poedit or any text editor) and open a pull request; keep every
`%s`, `%d`, `%.1f`... in the same order (`tests/test_ui_text.py` checks it). A new or changed English text goes into
`mkw.pot` and then `uv run tools/i18n/catalog.py update`.
