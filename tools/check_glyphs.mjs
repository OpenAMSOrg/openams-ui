import { existsSync, readFileSync, readdirSync } from 'node:fs';
import { join } from 'node:path';

const LANGUAGES = ['en', 'de', 'es', 'fr', 'it', 'pt-BR', 'pl', 'ru', 'ja', 'ko', 'zh-Hans'];
// Every language's glyphs are in the device fonts now: Geist carries the Latin
// and Cyrillic additions, and ja/ko/zh-Hans have a CJK face each. An empty
// allow-list is the point of the check - a language that loses its glyphs
// fails it.
const ALLOW_LIST = new Set();
// A language whose characters are too many for Geist, and the face that draws
// them. The face is named for its country, not for the language code.
const CJK_FACES = { ja: 'ja', ko: 'ko', 'zh-Hans': 'zh' };

export function parseCmaps(source) {
  const clean = source.replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g, '');
  const lists = new Map();
  for (const match of clean.matchAll(/uint16_t\s+(\w+)\s*\[\s*\]\s*=\s*\{([^}]*)\}/g)) {
    lists.set(match[1], match[2].split(',').map(value => value.trim()).filter(Boolean).map(Number));
  }
  const cmaps = clean.match(/\bcmaps\s*\[\s*\]\s*=\s*\{([\s\S]*?)\}\s*;/);
  if (!cmaps) throw new Error('No cmaps array found');
  const characters = new Set();
  for (const entry of cmaps[1].matchAll(/\{([^{}]*)\}/g)) {
    const field = name => entry[1].match(new RegExp(`\\.${name}\\s*=\\s*(\\w+)`))?.[1];
    const start = Number(field('range_start'));
    const length = Number(field('range_length'));
    const list = field('unicode_list');
    if (!Number.isInteger(start) || !Number.isInteger(length) || !list) {
      throw new Error('Invalid cmap entry');
    }
    if (list === 'NULL') {
      for (let offset = 0; offset < length; offset++) characters.add(start + offset);
    } else {
      const offsets = lists.get(list);
      const count = Number(field('list_length'));
      if (!offsets || !Number.isInteger(count) || count > offsets.length) {
        throw new Error(`Invalid unicode list: ${list}`);
      }
      for (const offset of offsets.slice(0, count)) characters.add(start + offset);
    }
  }
  return characters;
}

export function collectCharacters(document) {
  const strings = document.strings ?? document;
  const entries = Array.isArray(strings) ? strings : Object.values(strings);
  const characters = new Set();
  for (const entry of entries) {
    const text = typeof entry === 'string' ? entry : entry?.text;
    if (typeof text !== 'string') continue;
    for (const character of text.replace(/\{[^{}]+\}/g, '')) {
      characters.add(character.codePointAt(0));
    }
  }
  return characters;
}

const label = point => `U+${point.toString(16).toUpperCase().padStart(4, '0')}`;
const sorted = points => [...points].sort((first, second) => first - second);

/** The Geist sizes the fonts directory holds, taken from the files themselves
 *  so a new size is checked as soon as tools/gen_fonts.py generates one. */
export function geistSizes(fontsDirectory) {
  return readdirSync(fontsDirectory)
    .map(file => /^ui_font_geist_(\d+)\.c$/.exec(file)?.[1])
    .filter(Boolean)
    .map(Number)
    .sort((first, second) => first - second);
}

/** What one size draws: that size's Geist face, plus the CJK face of
 *  @p language at the same size when it has one. Each size is checked on its
 *  own, because the sizes are the sizes the UI actually draws with - a glyph
 *  in the 20 px face is no use in a 12 px label. */
export function coverageAt(fontsDirectory, size, language) {
  const faces = [`ui_font_geist_${size}`];
  const key = CJK_FACES[language];
  if (key) faces.push(`ui_font_cjk_${key}_${size}`);
  const covered = new Set();
  for (const face of faces) {
    const path = join(fontsDirectory, `${face}.c`);
    if (!existsSync(path)) throw new Error(`No such font: ${face}.c`);
    for (const point of parseCmaps(readFileSync(path, 'utf8'))) covered.add(point);
  }
  return covered;
}

export function main(args = process.argv.slice(2), root = process.cwd()) {
  const [mode, code] = args;
  if (!['--report', '--check', '--list'].includes(mode)
      || args.length !== (mode === '--list' ? 2 : 1)
      || (mode === '--list' && !LANGUAGES.includes(code))) {
    throw new Error('Usage: check_glyphs.mjs --report | --check | --list <code>');
  }
  // Resolve input paths from the working directory so fixtures use the same CLI.
  const stringsDirectory = join(root, 'design/strings');
  const readCharacters = language => collectCharacters(
    JSON.parse(readFileSync(join(stringsDirectory, `${language}.json`), 'utf8')),
  );
  if (mode === '--list') {
    for (const point of sorted(readCharacters(code))) console.log(label(point));
    return 0;
  }
  // The 480x320 set and each other resolution's (src/ui/fonts/<resolution>/,
  // tools/gen_fonts.py --set): a language must be covered by every set, or one
  // build draws blanks.
  const baseDirectory = join(root, 'src/ui/fonts');
  const fontsDirectories = [baseDirectory, ...readdirSync(baseDirectory, { withFileTypes: true })
    .filter(entry => entry.isDirectory())
    .map(entry => join(baseDirectory, entry.name))];
  for (const fontsDirectory of fontsDirectories) {
    if (!geistSizes(fontsDirectory).length) throw new Error(`No Geist faces in ${fontsDirectory}`);
  }
  const files = new Set(readdirSync(stringsDirectory));
  let failed = false;
  for (const language of LANGUAGES.filter(language => files.has(`${language}.json`))) {
    const characters = readCharacters(language);
    const missing = new Set();
    for (const fontsDirectory of fontsDirectories) {
      for (const size of geistSizes(fontsDirectory)) {
        const covered = coverageAt(fontsDirectory, size, language);
        for (const point of characters) if (!covered.has(point)) missing.add(point);
      }
    }
    if (mode === '--report') {
      const points = sorted(missing);
      const sample = points.slice(0, 20).map(point => `${label(point)} ${String.fromCodePoint(point)}`);
      console.log(`${language}: ${characters.size} characters, ${points.length} missing${sample.length ? `: ${sample.join(', ')}` : ''}`);
    } else if (missing.size && !ALLOW_LIST.has(language)) {
      console.error(`${language}: ${missing.size} missing characters`);
      failed = true;
    }
  }
  return failed ? 1 : 0;
}

if (import.meta.main) {
  process.stdout.on('error', error => {
    if (error.code === 'EPIPE') process.exit(0);
    throw error;
  });
  try {
    process.exitCode = main();
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}
