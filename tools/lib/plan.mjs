// What the generator writes, and where, for each host it knows.
//
// Each host is a row of the brief: a panel folder, an entry file, a host
// wrapper directory and a hook. `web/logic` and `web/vue` are the source trees
// that move; a row re-aims the specifiers that reach one of them and leaves
// the rest alone.

import { existsSync, readFileSync, readdirSync } from 'node:fs'
import { posix } from 'node:path'

import { rewriteImports, scssUseAsImport, stripGeneratedBanner } from './rewrites.mjs'

/**
 * One row per host the panel is generated into.
 *
 *   panel     where the panel folder goes in the target checkout; the
 *             generator owns it, and prunes what it no longer writes
 *   entry     the host wrapper's own file, named by the host's conventions
 *   hostDir   the wrapper's directory in this repo, which moves beside the
 *             panel (adapter.ts and its test land in the panel folder)
 *   hook      the patch the host needs in its own files
 *   format    the target checkout's own formatter, run from the target
 *             checkout, so the fork's tooling decides the formatting
 *   scss      the SCSS rule rewrite this host's bundler needs, if any
 *   imports   the specifier rows this host needs; the defaults aim
 *             web/vue/src, web/vue and web/logic at the panel folder
 */
export const HOSTS = {
  mainsail: {
    label: 'mainsail',
    panel: 'src/components/panels/Openams',
    entry: 'src/components/panels/OpenamsPanel.vue',
    entryFile: 'OpenamsPanel.vue',
    hostDir: 'web/hosts/mainsail',
    hook: 'web/hosts/mainsail/hook.patch',
    format: 'prettier',
    scss: null
  },
  fluidd: {
    label: 'fluidd',
    panel: 'src/components/widgets/openams',
    entry: 'src/components/widgets/openams/OpenamsCard.vue',
    entryFile: 'OpenamsCard.vue',
    hostDir: 'web/hosts/fluidd',
    hook: 'web/hosts/fluidd/hook.patch',
    // Fluidd ships no prettier; its own eslint is what decides the style.
    format: 'eslint',
    // Fluidd's vite prepends its own @import to every SCSS block.
    scss: 'use-as-import'
  }
}

export const hostNames = () => Object.keys(HOSTS)

/** The rows one host wrapper needs: its own directory lands in the panel
 *  folder, so the wrapper reaches the components and the logic beside it. */
const hostMap = (host) => [
  { prefix: 'web/vue/src', dest: host.panel },
  { prefix: 'web/vue', dest: host.panel },
  { prefix: host.hostDir, dest: host.panel },
  { prefix: 'web/logic', dest: `${host.panel}/logic` }
]

// web/vue's vite alias for the logic (its index), as it lands in the fork.
const aliases = (host) => ({ '@logic': `${host.panel}/logic/index` })

const fileOf = (dir) => (p) => p.slice(dir.length + 1)

/** Every file under `dir`, as paths relative to the repo root, sorted. */
function walk(root, dir) {
  if (!existsSync(posix.join(root, dir))) return []
  const out = []
  const rec = (rel) => {
    for (const entry of readdirSync(posix.join(root, rel), { withFileTypes: true }).sort((a, b) =>
      a.name < b.name ? -1 : a.name > b.name ? 1 : 0
    )) {
      const child = rel ? `${rel}/${entry.name}` : entry.name
      if (entry.isDirectory()) rec(child)
      else if (entry.isFile()) out.push(child)
    }
  }
  rec(dir)
  return out
}

const isTest = (p) => p.split('/').includes('test') || /\.(test|spec)\.ts$/.test(p)

/** web/logic: every module, but not the differential tests that use node:fs. */
function logicSources(root, host) {
  return walk(root, 'web/logic')
    .filter((p) => p.endsWith('.ts') && !isTest(p))
    .map((p) => ({ src: p, dest: `${host.panel}/logic/${fileOf('web/logic')(p)}` }))
}

/** web/vue/src: every .vue, .ts, .scss and .css, tests aside. The tokens
 *  (tokens.scss, tokens.css) are among them: web/vue generates them from
 *  design/tokens.json and they are copied as they are. */
function vueSources(root, host) {
  return walk(root, 'web/vue/src')
    .filter((p) => /\.(vue|ts|scss|css)$/.test(p) && !isTest(p))
    .map((p) => ({ src: p, dest: `${host.panel}/${fileOf('web/vue/src')(p)}` }))
}

/** One host's wrapper directory: the entry file at the host's own path, then
 *  the adapter (and its test) beside the logic in the panel folder. */
function hostSources(root, host) {
  const dir = host.hostDir
  const out = []
  const entry = `${dir}/${host.entryFile}`
  if (existsSync(posix.join(root, entry))) out.push({ src: entry, dest: host.entry, map: hostMap(host) })
  for (const p of walk(root, dir)) {
    if (p.split('/').length === 4 && p.endsWith('.ts')) {
      out.push({ src: p, dest: `${host.panel}/${p.split('/')[3]}`, map: hostMap(host) })
    }
  }
  return out
}

/**
 * The whole output set as [{ dest, text }] sorted by destination, plus the
 * sources that were not there to copy and the hook patch, if any.
 */
export function buildPlan(root, host = HOSTS.mainsail) {
  const skipped = []
  const files = []
  const add = (sources, label) => {
    if (sources.length === 0) skipped.push(label)
    for (const { src, dest, map = hostMap(host) } of sources) {
      const raw = readFileSync(posix.join(root, src), 'utf8')
      const aimed = rewriteImports(raw, { srcDir: posix.dirname(src), destFile: dest, map, aliases: aliases(host) })
      const text = stripGeneratedBanner(host.scss === 'use-as-import' ? scssUseAsImport(aimed) : aimed)
      files.push({ dest, text })
    }
  }
  add(logicSources(root, host), 'web/logic (no .ts modules found)')
  add(vueSources(root, host), 'web/vue/src (no components or modules found)')
  add(hostSources(root, host), `${host.hostDir} (no panel wrapper found)`)

  // The tokens are web/vue's own generated files; without them the panel's
  // styles cannot resolve, so their absence is said, not papered over.
  for (const t of ['tokens.scss', 'tokens.css']) {
    if (!existsSync(posix.join(root, 'web/vue/src', t))) skipped.push(`web/vue/src/${t} (run: node web/vue/tools/gen_tokens.mjs)`)
  }

  // Two sources wanting one destination is a bug in a source tree, not
  // something to resolve by ordering: say so instead of losing a file.
  const seen = new Set()
  for (const f of files) {
    if (seen.has(f.dest)) throw new Error(`two sources generate ${f.dest}`)
    seen.add(f.dest)
  }

  files.sort((a, b) => (a.dest < b.dest ? -1 : a.dest > b.dest ? 1 : 0))
  const hook = posix.join(root, host.hook)
  return { host, files, skipped, hook: existsSync(hook) ? hook : null }
}
