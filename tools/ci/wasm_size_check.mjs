#!/usr/bin/env node
// Fail when the openams-core .wasm is over its budget (docs/design/UNIFIED_UI.md 8):
// at most 256 KB raw and 112 KB gzipped (1 KB = 1024 bytes). Prints both numbers.
//
//   node tools/ci/wasm_size_check.mjs web/core/dist/openams-core.wasm
import { readFileSync } from 'node:fs';
import { gzipSync } from 'node:zlib';

const KB = 1024;
// The wasm core is no longer shipped in a panel (the Mainsail and Fluidd panels
// run the TypeScript logic); it is the test oracle the logic is checked
// against, and it carries the string tables of all eleven languages. The
// budget guards against runaway growth, not a download size.
const RAW_LIMIT = 256 * KB;
const GZIP_LIMIT = 112 * KB;

const path = process.argv[2];
if (!path) {
  console.error('usage: node tools/ci/wasm_size_check.mjs <path to the .wasm>');
  process.exit(2);
}

let bytes;
try {
  bytes = readFileSync(path);
} catch (err) {
  console.error('wasm_size_check: cannot read ' + path + ': ' + err.message);
  process.exit(2);
}

const raw = bytes.length;
const gz = gzipSync(bytes, { level: 9 }).length;
const kb = (n) => (n / KB).toFixed(1);

console.log(`raw:  ${raw} bytes (${kb(raw)} KB), limit ${RAW_LIMIT} (${RAW_LIMIT / KB} KB)`);
console.log(`gzip: ${gz} bytes (${kb(gz)} KB), limit ${GZIP_LIMIT} (${GZIP_LIMIT / KB} KB)`);

let failed = false;
if (raw > RAW_LIMIT) {
  console.error(`FAIL: raw size ${raw} exceeds ${RAW_LIMIT}`);
  failed = true;
}
if (gz > GZIP_LIMIT) {
  console.error(`FAIL: gzipped size ${gz} exceeds ${GZIP_LIMIT}`);
  failed = true;
}
if (failed) process.exit(1);
console.log('OK: within budget');
