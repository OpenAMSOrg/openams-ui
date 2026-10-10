#!/usr/bin/env node
// Fails when a quoted, user-visible string literal is added to src/ui/ outside
// the strings table (design/strings/en.json): every word the device shows is a
// string id, so a translation covers the whole UI (docs/design/UNIFIED_UI.md 12).
//
//   node tools/check_literals.mjs           check src/ui/*.c (exit 1 on a finding)
//   node tools/check_literals.mjs --list    print every finding, exit 0
//
// A literal is user-visible when, with printf conversions and \x escapes
// removed, it still holds a word of two letters or more. Exceptions:
//   - a line that logs an action, includes a header, or compares/searches text
//     (actions_log, #include, strcmp, strncmp, strstr, LV_...);
//   - a line carrying the marker `lit-ok` in a comment (a wire token, a proper
//     noun, a file name: something that is not a sentence to translate);
//   - a literal listed in tools/literals_allow.txt (material and vendor names).
// Symbols ("∞", " · ", "%", "…") hold no word, so they never need listing.

import { readFileSync, readdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const dir = resolve(root, 'src/ui');
const allow = new Set(
  readFileSync(resolve(root, 'tools/literals_allow.txt'), 'utf8')
    .split('\n').map((l) => l.trim()).filter((l) => l && !l.startsWith('#')),
);
const SKIP_LINE = /actions_log\(|#include|\bstrcmp\(|\bstrncmp\(|\bstrstr\(|\bLV_[A-Z_]+|lit-ok/;

/** The string literals of one line of C (no multi-line literals here), with
 *  adjacent literals joined, and comments removed first. */
function literals(line) {
  const code = line.replace(/\/\*.*?\*\//g, '').replace(/\/\/.*$/, '')
    .replace(/VS_ARG_[TNF]\(\s*"[^"]*"/g, 'VS_ARG(');   // a template's placeholder name
  const out = [];
  let cur = null;
  const re = /"((?:[^"\\]|\\.)*)"/g;
  let m;
  let last = 0;
  while ((m = re.exec(code))) {
    const between = code.slice(last, m.index);
    if (cur !== null && /^\s*$/.test(between)) cur += m[1];
    else { if (cur !== null) out.push(cur); cur = m[1]; }
    last = re.lastIndex;
  }
  if (cur !== null) out.push(cur);
  return out;
}

const visible = (lit) =>
  /[A-Za-z]{2,}/.test(lit.replace(/%[-+ #0-9.*]*[a-zA-Z]/g, '').replace(/\\x[0-9A-Fa-f]{2}/g, '').replace(/\\./g, ''));

const findings = [];
for (const f of readdirSync(dir).filter((n) => n.endsWith('.c')).sort()) {
  const lines = readFileSync(resolve(dir, f), 'utf8').split('\n');
  let inBlock = false;
  lines.forEach((line, i) => {
    let text = line;
    if (inBlock) {                                   // inside a /* ... */ comment block
      if (!text.includes('*/')) return;
      text = text.slice(text.indexOf('*/') + 2);
      inBlock = false;
    }
    if (text.includes('/*') && !text.slice(text.indexOf('/*')).includes('*/')) {
      inBlock = true;
      text = text.slice(0, text.indexOf('/*'));
    }
    if (SKIP_LINE.test(line)) return;
    for (const lit of literals(text)) {
      if (visible(lit) && !allow.has(lit)) findings.push(`src/ui/${f}:${i + 1}: "${lit}"`);
    }
  });
}

if (process.argv.includes('--list')) {
  console.log(findings.join('\n'));
  console.log(`${findings.length} literal(s)`);
  process.exit(0);
}
if (findings.length) {
  console.error('User-visible string literals in src/ui/ (put them in design/strings/en.json,\n' +
    'or mark the line `lit-ok` / list the literal in tools/literals_allow.txt):');
  for (const f of findings) console.error('  ' + f);
  process.exit(1);
}
console.log('check_literals: src/ui/ has no user-visible literals outside the strings table');
