#!/usr/bin/env node
// Generates the OpenAMS panel into a host fork, as ordinary source
// (docs/design/UNIFIED_UI.md 11). The fork never edits these by hand; this
// writes them, and --check says whether the fork still matches.
//
//   node tools/gen_host.mjs --host mainsail --out <mainsail checkout>
//   node tools/gen_host.mjs --host fluidd   --out <fluidd checkout>
//   node tools/gen_host.mjs --host <h> --out <checkout> --check
//   node tools/gen_host.mjs --host <h> --out <dir> --no-hook  (a scratch directory that is no checkout)
//   node tools/gen_host.mjs --list

import { spawnSync } from 'node:child_process'
import { existsSync, statSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

import { applyHook, checkTree, writeTree } from './lib/apply.mjs'
import { HOSTS, buildPlan, hostNames } from './lib/plan.mjs'

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..')
const args = process.argv.slice(2)
const has = (name) => args.includes(name)
const valueOf = (name) => {
  const i = args.indexOf(name)
  return i === -1 ? undefined : args[i + 1]
}
const known = ['--host', '--out', '--check', '--no-hook', '--list']

const usage = `usage: gen_host.mjs --host <${hostNames().join('|')}> --out <checkout> [--check] | --list [--host <host>]`

const hostName = valueOf('--host')
if (hostName && !HOSTS[hostName]) {
  console.error(`unknown host: ${hostName}\n${usage}`)
  process.exit(2)
}
if (args.some((a) => a.startsWith('--') && !known.includes(a))) {
  console.error(usage)
  process.exit(2)
}
if (!has('--list') && !hostName) {
  console.error(usage)
  process.exit(2)
}

// --list on its own names the hosts the generator knows; with --host it names
// the files that host's run writes, relative to the --out root.
if (has('--list') && !hostName) {
  for (const name of hostNames()) console.log(name)
  process.exit(0)
}
const host = HOSTS[hostName]

// The tokens are generated files (web/vue/src/tokens.*, gitignored): make them
// fresh every run, so a fresh checkout needs no manual step and a stale copy
// (design/tokens.json changed since the last web build) never reaches the fork.
const tokenTool = resolve(root, 'web/vue/tools/gen_tokens.mjs')
if (existsSync(tokenTool)) {
  const r = spawnSync(process.execPath, [tokenTool], { encoding: 'utf8' })
  if (r.status !== 0) {
    console.error(`tokens: ${r.stdout}${r.stderr}`)
    process.exit(1)
  }
}

let plan
try {
  plan = buildPlan(root, host)
} catch (e) {
  console.error(e.message)
  process.exit(1)
}
if (has('--no-hook')) plan.hook = null
for (const s of plan.skipped) console.error(`skipped ${s}`)

if (has('--list')) {
  for (const f of plan.files) console.log(f.dest)
  process.exit(0)
}

const out = valueOf('--out')
if (!out || out.startsWith('--') || args.some((a) => a.startsWith('--') && !known.includes(a))) {
  console.error(usage)
  process.exit(2)
}
if (!existsSync(out) || !statSync(out).isDirectory()) {
  console.error(`--out is not a directory: ${out}`)
  process.exit(2)
}
// The target's own tools (prettier, eslint) want an absolute cwd.
const target = resolve(out)

if (!has('--check')) {
  let formatted
  try {
    formatted = await writeTree(target, plan)
  } catch (e) {
    console.error(e.message)
    process.exit(1)
  }
  if (plan.hook) {
    const r = applyHook(target, plan.hook, false)
    if (r !== true) {
      console.error(r.error)
      process.exit(1)
    }
  }
  console.log(`wrote ${plan.files.length} files to ${out}`)
  if (!formatted) console.error(`note: no ${host.format} in the target; wrote the emitter output unformatted`)
  process.exit(0)
}

const problems = await checkTree(target, plan)
if (plan.hook) {
  const r = applyHook(target, plan.hook, true)
  if (r !== true) problems.push(typeof r === 'object' ? r.error : `not applied: ${host.hook}`)
}
if (problems.length) {
  for (const p of problems) console.error(p)
  console.error(`${host.label}: ${problems.length} file(s) differ; run: node tools/gen_host.mjs --host ${host.label} --out ${out}`)
  process.exit(1)
}
console.log(`${host.label}: ${plan.files.length} file(s) match`)
