// Writing, checking and patching the generated tree.
//
// The formatter is the target checkout's own, run from the target checkout, so
// the fork's tooling decides the formatting: prettier where the fork has it
// (Mainsail), eslint --fix where the fork styles with eslint (Fluidd). When
// the fork has neither the files are left exactly as the emitters wrote them;
// nothing here ever installs anything.

import { spawnSync } from 'node:child_process'
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { posix } from 'node:path'
import { pathToFileURL } from 'node:url'

const PRETTIER_PKG = ['node_modules/prettier/bin/prettier.cjs', 'node_modules/prettier/bin-prettier.js']
const ESLINT_PKG = 'node_modules/eslint/lib/api.js'

/** The fork's prettier, if it has one. `null` means "leave the text alone". */
export function findPrettier(out) {
  for (const rel of PRETTIER_PKG) {
    if (existsSync(posix.join(out, rel))) return posix.join(out, rel)
  }
  const shim = posix.join(out, 'node_modules/.bin/prettier')
  return existsSync(shim) ? shim : null
}

function run(cmd, args, opts) {
  const r = spawnSync(cmd, args, { encoding: 'utf8', ...opts })
  return { status: r.status, out: `${r.stdout || ''}${r.stderr || ''}` }
}

/** prettier for one file, by stdin, so --check can predict the bytes. */
function formatOne(bin, out, abs, text) {
  const r = run(process.execPath, [bin, '--stdin-filepath', abs], { cwd: out, input: text })
  if (r.status !== 0) throw new Error(`prettier failed on ${abs}:\n${r.out}`)
  return r.out
}

/** The fork's eslint, loaded from the fork's own node_modules, once per run:
 *  lintText with fix gives the bytes eslint --write would leave behind, and
 *  it is idempotent, so --check can predict them file by file. */
async function findEslint(out) {
  const abs = posix.join(out, ESLINT_PKG)
  if (!existsSync(abs)) return null
  const { ESLint } = await import(pathToFileURL(abs).href)
  return new ESLint({ cwd: out, fix: true })
}

async function eslintOne(eslint, out, dest, text) {
  if (!/\.(ts|vue)$/.test(dest)) return text
  const [result] = await eslint.lintText(text, { filePath: posix.join(out, dest) })
  const fatal = (result.messages || []).filter((m) => m.fatal)
  if (fatal.length) throw new Error(`eslint failed on ${dest}: ${fatal[0].message}`)
  // No output means eslint had nothing to fix.
  return result.output === undefined ? text : result.output
}

/** The fork's formatter for one host, built once per run so --check can
 *  predict --write file by file. `null` means "leave the text alone". */
async function formatter(out, host) {
  if (host.format === 'prettier') {
    const bin = findPrettier(out)
    return bin ? { fix: (dest, text) => formatOne(bin, out, posix.join(out, dest), text) } : null
  }
  if (host.format === 'eslint') {
    const abs = posix.join(out, ESLINT_PKG)
    if (!existsSync(abs)) return null
    const { ESLint } = await import(pathToFileURL(abs).href)
    const eslint = new ESLint({ cwd: out, fix: true })
    return { fix: (dest, text) => eslintOne(eslint, out, dest, text) }
  }
  return null
}

/** The bytes one output file should have, the way the fork formats them. */
export async function formatFile(out, host, dest, text) {
  const fmt = await formatter(out, host)
  return fmt ? fmt.fix(dest, text) : text
}

export async function writeTree(out, plan) {
  const { host, files } = plan
  // The panel folder belongs to the generator: what it no longer writes (a file
  // that moved or was deleted upstream) goes, so the fork cannot keep a stale one.
  const want = new Set(files.map((f) => f.dest))
  for (const p of filesUnder(out, host.panel)) if (!want.has(p)) rmSync(posix.join(out, p))
  for (const f of files) {
    const abs = posix.join(out, f.dest)
    mkdirSync(posix.dirname(abs), { recursive: true })
    writeFileSync(abs, f.text)
  }
  const fmt = await formatter(out, host)
  if (!fmt) return false
  for (const f of files) {
    const text = await fmt.fix(f.dest, f.text)
    if (text !== f.text) writeFileSync(posix.join(out, f.dest), text)
  }
  return true
}

function filesUnder(out, rel) {
  const abs = posix.join(out, rel)
  if (!existsSync(abs)) return []
  const found = []
  const rec = (r) => {
    for (const e of readdirSync(posix.join(out, r), { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
      const child = `${r}/${e.name}`
      if (e.isDirectory()) rec(child)
      else found.push(child)
    }
  }
  rec(rel)
  return found
}

/** Every problem between the fork and what the plan says it should hold. */
export async function checkTree(out, plan) {
  const { host, files } = plan
  const problems = []
  const fmt = await formatter(out, host)
  const want = new Set(files.map((f) => f.dest))
  for (const f of files) {
    const abs = posix.join(out, f.dest)
    if (!existsSync(abs)) {
      problems.push(`missing: ${f.dest}`)
    } else if (readFileSync(abs, 'utf8') !== (fmt ? await fmt.fix(f.dest, f.text) : f.text)) {
      problems.push(`differs: ${f.dest}`)
    }
  }
  const found = filesUnder(out, host.panel)
  if (existsSync(posix.join(out, host.entry))) found.push(host.entry)
  for (const p of found) if (!want.has(p)) problems.push(`extra: ${p}`)
  return problems
}

/**
 * The hook, in both directions. `git apply` on the way out, `git apply
 * --reverse --check` to ask whether it is already there. A second run over an
 * already-patched tree is a no-op rather than a failure.
 */
export function applyHook(out, hook, check) {
  const git = (...args) => run('git', ['-C', out, 'apply', ...args, hook])
  if (check) {
    const already = git('--reverse', '--check')
    if (already.status === 0) return true
    const notYet = git('--check')
    if (notYet.status === 0) return false
    return { error: `git apply --check refused ${hook} in ${out}:\n${notYet.out}` }
  }
  if (git('--reverse', '--check').status === 0) return true
  const r = git()
  if (r.status !== 0) return { error: `git apply failed for ${hook} in ${out}:\n${r.out}` }
  return true
}
