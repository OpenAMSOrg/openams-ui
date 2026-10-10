import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawnSync } from 'node:child_process';
import { collectCharacters, coverageAt, geistSizes, parseCmaps } from '../check_glyphs.mjs';

const FONT = `
static const uint16_t unicode_list_1[] = { 0x0, 0x7, 0x1f63 };
static const lv_font_fmt_txt_cmap_t cmaps[] = {
  { .range_start = 32, .range_length = 3, .unicode_list = NULL, .list_length = 0 },
  { .range_start = 176, .range_length = 8559,
    .unicode_list = unicode_list_1, .list_length = 3 }
};`;

test('cmaps cover dense ranges and only listed sparse offsets', () => {
  assert.deepEqual([...parseCmaps(FONT)], [32, 33, 34, 176, 183, 8211]);
  assert.equal(parseCmaps(FONT).has(177), false);
});

test('placeholders and group metadata are skipped; Unicode is collected by code point', () => {
  const document = { strings: [{ group: 'ignored' }, { id: 'label', text: 'A{unit}{name}😀A' }] };
  assert.deepEqual([...collectCharacters(document)], [65, 0x1f600]);
});

test('id-keyed tables support string values and text objects', () => {
  assert.deepEqual([...collectCharacters({ first: 'B{unit}', second: { text: 'éB' } })], [66, 233]);
  assert.deepEqual([...collectCharacters({ strings: { first: 'B{unit}' } })], [66]);
});

test('each Geist size is checked on its own, with the CJK face of the same size', context => {
  const directory = dirname(fileURLToPath(import.meta.url));
  const root = mkdtempSync(join(directory, 'glyphs-'));
  context.after(() => rmSync(root, { recursive: true, force: true }));
  const strings = join(root, 'design/strings');
  const fonts = join(root, 'src/ui/fonts');
  mkdirSync(strings, { recursive: true });
  mkdirSync(fonts, { recursive: true });
  const setLanguage = (code, text) => writeFileSync(join(strings, `${code}.json`), JSON.stringify({ strings: [{ id: 'label', text }] }));
  const font = (...points) => `static const lv_font_fmt_txt_cmap_t cmaps[] = {
${points.map(point => `    { .range_start = ${point}, .range_length = 1, .unicode_list = NULL, .list_length = 0 }`).join(',\n')}
  };`;
  // Two sizes: A is in both, B only in the 12, D only in the 20, and every CJK
  // face carries the snowman. A checker that took the union of all faces would
  // pass on B and D; a per-size one will not.
  writeFileSync(join(fonts, 'ui_font_geist_12.c'), font(65, 66));
  writeFileSync(join(fonts, 'ui_font_geist_20.c'), font(65, 68));
  for (const key of ['ja', 'ko', 'zh']) {
    for (const size of [12, 20]) writeFileSync(join(fonts, `ui_font_cjk_${key}_${size}.c`), font(0x2603));
  }
  // Never counted: Plex, and the faces that are not Geist.
  writeFileSync(join(fonts, 'ui_font_plex_18.c'), font(0x2603));
  for (const ignored of ['icons_18', 'sev_18', 'inter_12']) {
    writeFileSync(join(fonts, `ui_font_${ignored}.c`), font(0x2603));
  }
  setLanguage('en', 'A{unit}');
  setLanguage('qps', '☃');
  setLanguage('de', 'A');
  for (const code of ['ja', 'ko', 'zh-Hans']) setLanguage(code, '☃');
  setLanguage('ru', '☃');

  assert.deepEqual(geistSizes(fonts), [12, 20]);
  const covered = (size, language) => [...coverageAt(fonts, size, language)].sort((a, b) => a - b);
  assert.deepEqual(covered(12, 'en'), [65, 66]);
  assert.deepEqual(covered(20, 'en'), [65, 68]);
  // A CJK language adds its own face at the same size; a Latin one is Geist
  // alone, and ru - which is not CJK - is short of the snowman at both sizes.
  assert.deepEqual(covered(20, 'ja'), [65, 68, 0x2603]);
  assert.deepEqual(covered(20, 'de'), [65, 68]);
  assert.equal(coverageAt(fonts, 20, 'ru').has(0x2603), false);
  // A CJK face that was never generated is a hard error, not a silent pass.
  rmSync(join(fonts, 'ui_font_cjk_zh_20.c'));
  assert.throws(() => coverageAt(fonts, 20, 'zh-Hans'), /ui_font_cjk_zh_20\.c/);
  writeFileSync(join(fonts, 'ui_font_cjk_zh_20.c'), font(0x2603));

  const run = (...args) => spawnSync(process.execPath, [join(directory, '../check_glyphs.mjs'), ...args], { cwd: root, encoding: 'utf8' });
  // Every CJK language is covered; ru is not, and the empty allow-list fails it.
  const checked = run('--check');
  assert.equal(checked.status, 1);
  assert.match(checked.stderr, /ru: 1 missing characters/);
  assert.doesNotMatch(checked.stderr, /en: |de: |ja: |ko: |zh-Hans: /);

  // B is only in the 12 px Geist face and C in neither, so en is short of C at
  // 12 and of B and C at 20 - two characters, which the union of every face
  // would have hidden.
  setLanguage('en', 'ABC{unit}');
  const perSize = run('--check');
  assert.equal(perSize.status, 1);
  assert.match(perSize.stderr, /en: 2 missing characters/);
  const report = run('--report');
  assert.equal(report.status, 0);
  assert.match(report.stdout, /en: 3 characters, 2 missing: U\+0042 B, U\+0043 C/);
  assert.doesNotMatch(report.stdout, /qps/);
  // Plex is no longer counted, so the snowman in en is still missing, and B is
  // still short at 20.
  setLanguage('en', 'AB☃{unit}');
  assert.match(run('--report').stdout, /en: 3 characters, 2 missing: U\+0042 B, U\+2603 ☃/);
  assert.equal(run('--check').status, 1);

  const list = run('--list', 'en');
  assert.equal(list.status, 0);
  assert.equal(list.stdout, 'U+0041\nU+0042\nU+2603\n');
  assert.equal(run('--list', 'qps').status, 1);
});
