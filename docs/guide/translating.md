# Translating

## What it is and why

Every word the display and the web panel show comes from one string table per
language, so a translation covers the touchscreen, the simulator and the
Mainsail and Fluidd panel at once. This page says where the tables are, how
they fit together, and how to check a change.

## The files

They are in `design/strings/`:

| File | What it holds |
|------|---------------|
| `en.json` | the master: every string id, in order, in English |
| `de.json`, `es.json`, `fr.json`, `it.json`, `ja.json`, `ko.json`, `pl.json`, `pt-BR.json`, `ru.json`, `zh-Hans.json` | the translations |
| `glossary.json` | one fixed translation per language for every recurring term; read it first |
| `qps.json` | a pseudo-language for layout tests, not a translation |

Each entry is `{"id": "...", "text": "..."}`. `en.json` also has
`{"group": "..."}` lines: section headings that make the master easier to
read. The other files do not need them. A translation is matched to English
by its id, so its order and headings do not matter, and an id a translation
leaves out shows in English.

## Rules

- Keep every `{placeholder}` of the English text, spelled the same; move it
  where the sentence needs it. `{spool} removed from group` becomes
  `{spool} aus der Gruppe entfernt`.
- Use the glossary's word for every recurring term (spool, unit, group,
  toolhead and so on), the same in every string.
- Where a box is small (a tile tag, a chip), the glossary's short form exists
  for it; keep to the English length there.

## Check a change

From the repository root:

```sh
node tools/gen_strings.mjs           # regenerate the tables from the JSON
node tools/gen_strings.mjs --check   # are the generated tables current?
node tools/check_glyphs.mjs --check  # does the device font carry every character?
```

`gen_strings.mjs` writes `src/view/strings_*.gen.h` for the device and
`web/logic/strings.gen.ts` for the panel; commit them with the JSON.
`check_glyphs.mjs` matters for Japanese, Korean and Chinese: the device
carries only the characters its fonts were built with, and it names any
character that is missing.

To see a language on screen, run the simulator with `--lang <code>`
([development.md](development.md)), for example
`./build/openams-ui-sim --lang de`.

## Where next

- [development.md](development.md): build the simulator and run the tests.
- [using.md](using.md): the screens the strings appear on.
