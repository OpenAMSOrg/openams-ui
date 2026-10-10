#!/usr/bin/env node
// Generates the OpenAMS panel into a Mainsail fork. A shim for gen_host.mjs
// --host mainsail, which is where the work moved: CI and the docs name this
// file, so it keeps working.
//
//   node tools/gen_mainsail.mjs --out <mainsail checkout>
//   node tools/gen_mainsail.mjs --out <mainsail checkout> --check
//   node tools/gen_mainsail.mjs --out <dir> --no-hook   (a scratch directory that is no Mainsail checkout)
//   node tools/gen_mainsail.mjs --list

import { spawnSync } from 'node:child_process'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const r = spawnSync(process.execPath, [resolve(here, 'gen_host.mjs'), '--host', 'mainsail', ...process.argv.slice(2)], {
  stdio: 'inherit'
})

process.exit(r.status === null ? 1 : r.status)
