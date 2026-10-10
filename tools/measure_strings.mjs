// Not part of the build: the length budget for the composed view buffers.
// Substitutes worst-case arguments into every whole-sentence template of every
// language and prints the longest composition each field has to hold, so
// VIEW_*_LEN in src/view/view.h and the ALERT_TEXT size in the model are sized
// from the pseudo-locale rather than from English.
//
//   node tools/measure_strings.mjs

import { readFileSync, readdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const dir = resolve(root, 'design/strings');

/** The longest argument the view can hand a template: a host name is cut by
 *  the model (8 bytes for a unit, 16 for a toolhead id), a number by its own
 *  field. These are the wide ends, so the budget is the worst case. */
const ARG = {
  tool: 'fps1', unit: 'oams2', n: '8888', g: '32767', pct: '100', rh: '100', temp: '-100',
  hours: '24', mark: '\u221e', vendor: 'Polymaker', material: 'PolyLite', have: '32767',
  need: '32767', spool: 'oams2 Spool 8',
};

function fill(text, args) {
  return text.replace(/\{([^{}]*)\}/g, (whole, name) =>
    (name in args ? args[name] : whole));
}

const langs = readdirSync(dir).filter((f) => f.endsWith('.json')).map((f) => f.slice(0, -5)).sort();
for (const code of langs) {
  const doc = JSON.parse(readFileSync(resolve(dir, `${code}.json`), 'utf8'));
  const rows = [];
  for (const e of doc.strings) {
    if (!e.id || !/\{[^{}]*\}/.test(e.text)) continue;
    rows.push([e.id, Buffer.byteLength(fill(e.text, ARG)), Buffer.byteLength(e.text)]);
  }
  if (!rows.length) continue;
  console.log(`\n== ${code} (composed templates, worst-case args) ==`);
  for (const [id, need, tmpl] of rows) {
    console.log(`${id.padEnd(24)} needs ${String(need).padStart(4)} bytes  (template ${tmpl})`);
  }
}
