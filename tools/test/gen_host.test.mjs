// tools/gen_host.mjs, under node:test: the host table, one fixture run per
// host, and the shim CI still calls.
//
// The generator finds its sources relative to its own tools/ directory, so a
// fixture run copies tools/ into a throwaway openams-ui root and runs it
// there, and each host gets a throwaway checkout to write into. The trees are
// ours rather than copies of the real forks, which are GPL and not ours to
// vendor into this repo.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import { execFileSync, spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, relative, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

import { scssUseAsImport } from '../lib/rewrites.mjs'

const REPO = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), 'fixtures')
const MS_PANEL = 'src/components/panels/Openams'
const FD_PANEL = 'src/components/widgets/openams'
const TMP = []
process.on('exit', () => {
  for (const dir of TMP) rmSync(dir, { recursive: true, force: true })
})

function temp(prefix) {
  const dir = mkdtempSync(join(tmpdir(), prefix))
  TMP.push(dir)
  return dir
}

function git(cwd, ...args) {
  return execFileSync('git', args, { cwd, encoding: 'utf8' })
}

/** A throwaway openams-ui root: our tools and tokens, the fixture sources. */
function sourceRoot() {
  const root = temp('oams-src-')
  cpSync(join(REPO, 'tools'), join(root, 'tools'), { recursive: true })
  cpSync(join(REPO, 'design'), join(root, 'design'), { recursive: true })
  return root
}

/** A throwaway checkout of a host, committed, so a hook can be applied to it. */
function checkout(fixture) {
  const out = temp(`oams-${fixture}-`)
  git(out, 'init', '-q')
  cpSync(join(FIXTURES, `${fixture}-tree`), out, { recursive: true })
  git(out, 'add', '-A')
  git(out, '-c', 'user.email=t@example.com', '-c', 'user.name=t', 'commit', '-qm', 'base')
  return out
}

function run(root, ...args) {
  const r = spawnSync(process.execPath, [join(root, 'tools/gen_host.mjs'), ...args], { encoding: 'utf8' })
  return { status: r.status, out: `${r.stdout}${r.stderr}` }
}

function runMainsail(root, ...args) {
  const r = spawnSync(process.execPath, [join(root, 'tools/gen_mainsail.mjs'), ...args], { encoding: 'utf8' })
  return { status: r.status, out: `${r.stdout}${r.stderr}` }
}

const put = (root, rel, text) => {
  const abs = join(root, rel)
  mkdirSync(dirname(abs), { recursive: true })
  writeFileSync(abs, text)
}

const read = (root, rel) => readFileSync(join(root, rel), 'utf8')

/** Every file under `rel`, as `${relative path}\n${contents}` lines, sorted. */
function treeDigest(root, rel) {
  const base = join(root, rel)
  if (!existsSync(base)) return []
  const out = []
  const rec = (abs) => {
    for (const e of readdirSync(abs, { withFileTypes: true }).sort((a, b) => (a.name < b.name ? -1 : 1))) {
      const child = join(abs, e.name)
      if (e.isDirectory()) rec(child)
      else out.push(`${relative(base, child).split(sep).join('/')}\n${readFileSync(child, 'utf8')}`)
    }
  }
  rec(base)
  return out
}

const digest = (root, rel) => createHash('sha256').update(treeDigest(root, rel).join(' ')).digest('hex')

// ---------------------------------------------------------------- fixtures

// The emitters' own style, which the fork's tooling then rewrites: four spaces
// and semicolons, a `@use` for the tokens, and a generated banner to drop.
// The fixture keeps the syntax plain so the fixture's own parser can read it;
// what is under test is the generator, not TypeScript.
const LOGIC_INDEX = "export { createLogic } from './core.ts';\nexport { core } from './core.ts';\n"
const LOGIC_TYPES = 'export const VIEW = { units: [] };\nexport const ACTION = { kind: "local" };\n'
const LOGIC_CORE = 'export function createLogic() {\n    return { view: () => ({ units: [] }) };\n}\n'
const STORE = "import { VIEW } from '../../logic/index.ts'\n\nexport const units = VIEW.units;\n"
const TOKENS_SCSS = '$oams-radius-card: 22px;\n'
const TOKENS_CSS = ':host {\n  --oams-radius-card: 22px;\n}\n'

const UNIT_CARD = `<template>
    <v-card class="unit-card">
        <span>{{ unit.name }}</span>
    </v-card>
</template>

<script lang="ts">
import { createLogic } from '../../../logic/index.ts'
import { VIEW } from '../../../logic/types.ts'
import { core } from '@logic'

export default {
    name: 'UnitCard',
    props: { unit: { type: Object, required: true } },
    computed: {
        view() {
            return createLogic().view() || core;
        },
    },
};
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

.unit-card {
    border-radius: $oams-radius-card;
}
</style>
`

const CARD = `<template>
    <collapsable-card :title="$t('app.openams.Headline')" :icon="icon" layout-path="dashboard.openams-card">
        <v-card-text>
            <openams-view :logic="adapter.logic" :language="language" theme="host" @request="onRequest" />
        </v-card-text>
    </collapsable-card>
</template>

<script lang="ts">
import { OpenamsAdapter, panelLanguage } from './adapter'
import { ACTION } from '../../logic/types.ts'
import OpenamsView from '../../vue/src/components/UnitCard.vue'

export default {
    name: 'OpenamsCard',
    components: { OpenamsView },
    data: () => ({ adapter: new OpenamsAdapter(), icon: 'm' }),
    computed: {
        language() {
            return panelLanguage('en');
        },
    },
    methods: {
        onRequest(result) {
            return ACTION || result;
        },
    },
};
</script>
`

const ADAPTER = `import { createLogic } from '../../logic/index.ts'

/** Feeds Moonraker objects to the logic and hands the view back. */
export function openamsAdapter() {
    const core = createLogic()
    return {
        apply(name, object) {
            core.apply('openams_ui', name, object);
        },
        view() {
            return core.view();
        },
    }
}
`
const ADAPTER_TEST = "import { describe, it } from 'vitest'\nimport { openamsAdapter } from './adapter'\n\ndescribe('adapter', () => {\n    it('builds', () => void openamsAdapter())\n})\n"

/** The fixture openams-ui: one component, one host wrapper, one adapter. */
function fixtureSources() {
  const root = sourceRoot()
  put(root, 'web/logic/index.ts', LOGIC_INDEX)
  put(root, 'web/logic/types.ts', LOGIC_TYPES)
  put(root, 'web/logic/core.ts', LOGIC_CORE)
  put(root, 'web/vue/src/components/UnitCard.vue', UNIT_CARD)
  put(root, 'web/vue/src/store.ts', STORE)
  put(root, 'web/vue/src/tokens.scss', TOKENS_SCSS)
  put(root, 'web/vue/src/tokens.css', TOKENS_CSS)
  put(root, 'web/hosts/fluidd/adapter.test.ts', ADAPTER_TEST)
  put(root, 'web/hosts/fluidd/OpenamsCard.vue', CARD)
  put(root, 'web/hosts/fluidd/adapter.ts', ADAPTER)
  // The Mainsail wrapper is the other host's, from the other host's directory.
  put(root, 'web/hosts/mainsail/OpenamsPanel.vue', '<template><div /></template>\n')
  put(root, 'web/hosts/mainsail/adapter.ts', ADAPTER)
  return root
}

/** A hook that registers the card: a real diff, made by editing the file. */
function fixtureHook(root, host, out, file, before, after) {
  const original = read(out, file)
  assert.ok(original.includes(before), 'the card list moved')
  writeFileSync(join(out, file), original.replace(before, after))
  const patch = git(out, 'diff', '--', file)
  writeFileSync(join(out, file), original)
  put(root, `web/hosts/${host}/hook.patch`, patch)
}

// ---- 1. the host table ----------------------------------------------------

test('--list names the hosts, and one host names its files', () => {
  const root = fixtureSources()

  const hosts = run(root, '--list')
  assert.equal(hosts.status, 0, hosts.out)
  assert.deepEqual(hosts.out.split('\n').filter(Boolean), ['mainsail', 'fluidd'])

  const listed = (host) => run(root, '--host', host, '--list').out.split('\n').filter((l) => l && !l.startsWith('skipped'))
  // Each host writes into its own panel folder, under its own conventions.
  assert.ok(listed('mainsail').every((f) => f.startsWith(MS_PANEL)))
  assert.ok(listed('fluidd').every((f) => f.startsWith(FD_PANEL)))
  assert.ok(listed('mainsail').includes('src/components/panels/OpenamsPanel.vue'))
  assert.ok(listed('fluidd').includes(`${FD_PANEL}/OpenamsCard.vue`))

  // The shim CI calls is the same generator with the Mainsail host.
  assert.equal(runMainsail(root, '--list').out, run(root, '--host', 'mainsail', '--list').out)

  const unknown = run(root, '--host', 'octoprint', '--out', root)
  assert.equal(unknown.status, 2)
  assert.match(unknown.out, /unknown host: octoprint/)
})

// ---- 2. the Fluidd fixture run --------------------------------------------

test('fluidd fixture run: the card inside the panel folder, per-host rewrites, the hook and --check', () => {
  const out = checkout('fluidd')
  const root = fixtureSources()
  fixtureHook(
    root,
    'fluidd',
    out,
    'src/store/layout/state.ts',
    "{ id: 'afc-card', enabled: true, collapsed: false }\n",
    "{ id: 'afc-card', enabled: true, collapsed: false },\n        { id: 'openams-card', enabled: true, collapsed: false }\n"
  )

  const gen = run(root, '--host', 'fluidd', '--out', out)
  assert.equal(gen.status, 0, gen.out)
  assert.match(gen.out, /wrote \d+ files/)

  // The entry sits inside the panel folder, so it reaches the components and
  // the logic as siblings rather than through the folder above it.
  const card = read(out, `${FD_PANEL}/OpenamsCard.vue`)
  assert.match(card, /from '\.\/components\/UnitCard\.vue'/)
  assert.match(card, /from '\.\/logic\/types'/)
  assert.match(card, /from '\.\/adapter'/)
  // A component one level down reaches the logic with ..
  const unitCard = read(out, `${FD_PANEL}/components/UnitCard.vue`)
  assert.match(unitCard, /from '\.\.\/logic\/index'/)
  assert.match(unitCard, /from '\.\.\/logic\/types'/)
  // The adapter lands beside the logic, in the panel folder.
  assert.match(read(out, `${FD_PANEL}/adapter.ts`), /from '\.\/logic\/index'/)
  // The host's test travels with the adapter; the logic's own tests do not.
  assert.equal(read(out, `${FD_PANEL}/adapter.test.ts`), ADAPTER_TEST)

  // Fluidd's bundler prepends its own @import to every SCSS block, and sass
  // will not take a @use after it: this host's copy says @import.
  assert.match(unitCard, /@import '\.\.\/tokens';/)
  assert.doesNotMatch(unitCard, /@use/)
  // Nothing anywhere kept a .ts specifier.
  for (const f of [`${FD_PANEL}/OpenamsCard.vue`, `${FD_PANEL}/adapter.ts`, `${FD_PANEL}/components/UnitCard.vue`]) {
    assert.ok(!/\.ts'/.test(read(out, f)), `${f} kept a .ts specifier`)
  }

  assert.ok(read(out, 'src/store/layout/state.ts').includes("'openams-card'"), 'the hook was not applied')

  // --check is happy straight after a run...
  assert.equal(run(root, '--host', 'fluidd', '--out', out, '--check').status, 0)

  // ...and names a generated file that someone edited...
  const edited = join(out, `${FD_PANEL}/logic/core.ts`)
  const before = readFileSync(edited, 'utf8')
  writeFileSync(edited, `${before}export const edited = true\n`)
  const dirty = run(root, '--host', 'fluidd', '--out', out, '--check')
  assert.equal(dirty.status, 1)
  assert.match(dirty.out, /differs: src\/components\/widgets\/openams\/logic\/core\.ts/)
  writeFileSync(edited, before)
  assert.equal(run(root, '--host', 'fluidd', '--out', out, '--check').status, 0)

  // ...and a file nobody generated. A run prunes it.
  put(out, `${FD_PANEL}/components/Stray.vue`, '<template><div /></template>\n')
  const stray = run(root, '--host', 'fluidd', '--out', out, '--check')
  assert.equal(stray.status, 1)
  assert.match(stray.out, /extra: src\/components\/widgets\/openams\/components\/Stray\.vue/)
  assert.equal(run(root, '--host', 'fluidd', '--out', out).status, 0)
  assert.ok(!existsSync(join(out, `${FD_PANEL}/components/Stray.vue`)), 'the stray file was kept')
  assert.equal(run(root, '--host', 'fluidd', '--out', out, '--check').status, 0)

  // A second run over an already-patched checkout changes nothing.
  const again = run(root, '--host', 'fluidd', '--out', out)
  assert.equal(again.status, 0, again.out)
  assert.equal(read(out, 'src/store/layout/state.ts').match(/openams-card/g).length, 1, 'the hook was applied twice')
})

// ---- 3. each host's SCSS rule, and only that host's -----------------------

test("Mainsail keeps the panel's @use, and Fluidd gets the @import", () => {
  const root = fixtureSources()
  fixtureHook(root, 'mainsail', checkout('fluidd'), 'src/store/layout/state.ts', "{ id: 'afc-card', enabled: true, collapsed: false }\n", "{ id: 'afc-card', enabled: true, collapsed: false },\n        { id: 'openams-card', enabled: true, collapsed: false }\n")

  const ms = temp('oams-ms-scss-')
  const fd = temp('oams-fd-scss-')
  assert.equal(runMainsail(root, '--out', ms, '--no-hook').status, 0)
  assert.equal(run(root, '--host', 'fluidd', '--out', fd, '--no-hook').status, 0)

  assert.match(read(ms, `${MS_PANEL}/components/UnitCard.vue`), /@use '\.\.\/tokens' as \*;/)
  assert.match(read(fd, `${FD_PANEL}/components/UnitCard.vue`), /@import '\.\.\/tokens';/)

  // The rule itself, on its own: one form, and nothing else is touched.
  assert.equal(scssUseAsImport("@use './a' as *;\n.x { a: b }\n"), "@import './a';\n.x { a: b }\n")
  assert.equal(scssUseAsImport("@use './a';\n@use './b' as *;\n"), "@use './a';\n@import './b';\n")
})

// ---- 4. the fork's own formatter decides the formatting -------------------

test("the fork's own eslint formats the Fluidd tree, and --check predicts it", { timeout: 600000 }, () => {
  const out = checkout('fluidd')
  execFileSync('npm', ['install', '--no-save', '--no-audit', '--no-fund', '--loglevel=error', 'eslint@9'], {
    cwd: out,
    stdio: 'ignore',
  })
  const root = fixtureSources()
  // No hook: this is about the panel, and the fixture's hook is tested above.
  const gen = run(root, '--host', 'fluidd', '--out', out, '--no-hook')
  assert.equal(gen.status, 0, gen.out)

  // The emitters write four spaces and semicolons; the fork's eslint says no.
  const card = read(out, `${FD_PANEL}/components/UnitCard.vue`)
  const script = card.slice(card.indexOf('<script'), card.indexOf('</script>'))
  assert.doesNotMatch(script, /;/)
  // The rule takes one semicolon out at a time, by range: a range that is off
  // by a character eats a character of code instead of a semicolon.
  assert.match(card, /return createLogic\(\)\.view\(\) \|\| core\n/)
  // The template and the SCSS are not JavaScript, and are left alone.
  assert.match(card, /<span>\{\{ unit\.name \}\}<\/span>/)
  assert.match(card, /@import '\.\.\/tokens';/)
  // A file eslint does not lint keeps the emitter's bytes exactly.
  assert.equal(read(out, `${FD_PANEL}/tokens.scss`), TOKENS_SCSS)

  // --check has to predict what the run did, or the fork is always stale.
  assert.equal(run(root, '--host', 'fluidd', '--out', out, '--check').status, 0)
})

// ---- 5. determinism, per host ---------------------------------------------

test('two runs of one host over the same inputs give identical trees', () => {
  for (const host of ['mainsail', 'fluidd']) {
    const root = fixtureSources()
    const generate = (out) => (host === 'mainsail' ? runMainsail(root, '--out', out, '--no-hook') : run(root, '--host', host, '--out', out, '--no-hook'))
    const a = temp('oams-det-a-')
    const b = temp('oams-det-b-')

    assert.equal(generate(a).status, 0)
    assert.equal(generate(b).status, 0)
    assert.ok(treeDigest(a, 'src').length > 1, `${host}: nothing was generated`)
    assert.equal(digest(a, 'src'), digest(b, 'src'), `two ${host} runs differ`)
  }
})
