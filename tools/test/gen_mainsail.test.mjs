// tools/gen_mainsail.mjs, under node:test.
//
// The generator finds its sources relative to its own tools/ directory, so a
// fixture run copies tools/ into a throwaway openams-ui root and runs it
// there. The real-logic test runs the repo's own copy against the repo.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import { execFileSync, spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { cpSync, existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join, relative, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'

import { rewriteImports } from '../lib/rewrites.mjs'

const REPO = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), 'fixtures')
const PANEL = 'src/components/panels/Openams'
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

/**
 * A throwaway Mainsail checkout: our own stand-in tree, committed. The tree
 * is ours rather than a copy of Mainsail's, which is GPL and not ours to
 * vendor into this repo.
 */
function mainsailRoot() {
  const out = temp('oams-mainsail-')
  git(out, 'init', '-q')
  cpSync(join(FIXTURES, 'mainsail-tree'), out, { recursive: true })
  git(out, 'add', '-A')
  git(out, '-c', 'user.email=t@example.com', '-c', 'user.name=t', 'commit', '-qm', 'base')
  return out
}

function run(root, ...args) {
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

const UNIT_CARD = `<template>
    <v-card class="unit-card">
        <span>{{ unit.name }}</span>
    </v-card>
</template>

<script lang="ts">
import { createLogic } from '../../../logic/index.ts'
import type { View } from '../../../logic/types.ts'
import type { Core } from '@logic'

export default {
    name: 'UnitCard',
    props: {
        unit: { type: Object, required: true },
    },
    computed: {
        view(): View {
            return createLogic().view()
        },
    },
}
</script>
`

const TILE = `<template>
    <div class="tile">{{ label }}</div>
</template>

<script lang="ts">
import { createLogic } from '../../../../logic/index.ts'

export default {
    name: 'TileTile',
    props: { label: { type: String, required: true } },
    data: () => ({ core: createLogic() }),
}
</script>
`

const PANEL_VUE = `<template>
    <panel :title="$t('Panels.OpenamsPanel.Headline')" :collapsible="true" card-class="openams-panel">
        <v-card-text>
            <unit-card v-for="unit in units" :key="unit.name" :unit="unit" />
        </v-card-text>
    </panel>
</template>

<script lang="ts">
import { createLogic } from '../../logic/index.ts'
import type { View } from '../../logic/types.ts'
import { units } from '../../vue/src/store.ts'
import UnitCard from '../../vue/src/components/UnitCard.vue'

export default {
    name: 'OpenamsPanel',
    components: { UnitCard },
    data: () => ({ core: createLogic(), units, view: null as View | null }),
}
</script>
`

const ADAPTER = `import { createLogic } from '../../logic/index.ts'
import type { View } from '../../logic/types.ts'
import { baseAdapter } from '@/store/adapters/base'

/** Feeds Moonraker objects to the logic and hands the view back. */
export function openamsAdapter() {
    const core = createLogic()
    return {
        apply(name: string, object: unknown) {
            core.apply('openams_ui', name, object)
        },
        view(): View {
            return core.view()
        },
        ...baseAdapter,
    }
}
`

const STORE = `import type { View } from '../../logic/index.ts'

/** The units the panel shows, in the order the device reports them. */
export const units: View['units'] = []
`

const THEME = `@use './panel.scss'

.openams-panel {
    gap: $oams-gap-card-gap;
}
`

const PANEL_SCSS = `.unit-card {
    gap: $oams-gap-card-gap;
}
`

const TOKENS_SCSS = `$oams-radius-card: 22px;\n`
const TOKENS_CSS = `:host {\n  --oams-radius-card: 22px;\n}\n`
const ADAPTER_TEST = `import { describe, it } from 'vitest'\nimport { openamsAdapter } from './adapter'\n\ndescribe('adapter', () => {\n    it('builds', () => void openamsAdapter())\n})\n`

/** The fixture openams-ui: two components, a host wrapper, an adapter. */
function fixtureSources() {
  const root = sourceRoot()
  put(root, 'web/logic/index.ts', "export { createLogic } from './core.ts';\n")
  put(root, 'web/logic/types.ts', 'export type View = { units: unknown[] };\n')
  put(root, 'web/logic/core.ts', 'export function createLogic() {\n    return { view: () => ({ units: [] }) };\n}\n')
  put(root, 'web/vue/src/components/UnitCard.vue', UNIT_CARD)
  put(root, 'web/vue/src/components/tile/TileTile.vue', TILE)
  put(root, 'web/vue/src/store.ts', STORE)
  put(root, 'web/vue/src/theme.scss', THEME)
  put(root, 'web/vue/src/panel.scss', PANEL_SCSS)
  // web/vue generates its own tokens; the generator copies them as they are.
  put(root, 'web/vue/src/tokens.scss', TOKENS_SCSS)
  put(root, 'web/vue/src/tokens.css', TOKENS_CSS)
  // Tests stay behind: they would need the tools of the tree they came from.
  put(root, 'web/vue/src/components/UnitCard.test.ts', "import { it } from 'vitest'\nit('x', () => {})\n")
  put(root, 'web/logic/test/diff.test.ts', "import { it } from 'node:test'\nit('x', () => {})\n")
  put(root, 'web/hosts/mainsail/adapter.test.ts', ADAPTER_TEST)
  put(root, 'web/hosts/mainsail/OpenamsPanel.vue', PANEL_VUE)
  put(root, 'web/hosts/mainsail/adapter.ts', ADAPTER)
  return root
}

/** A hook that registers the panel: a real diff, made by editing the file. */
function fixtureHook(root, mainsail) {
  const file = join(mainsail, 'src/store/variables.ts')
  const original = readFileSync(file, 'utf8')
  assert.ok(original.includes("    'mmu',\n"), 'the panel list moved')
  writeFileSync(file, original.replace("    'mmu',\n", "    'mmu',\n    'openams',\n"))
  const patch = git(mainsail, 'diff')
  writeFileSync(file, original)
  put(root, 'web/hosts/mainsail/hook.patch', patch)
}

// ---- 1. the fixture run ----------------------------------------------------

test('fixture run: paths, import rewrites, tokens, the hook, and --check', () => {
  const mainsail = mainsailRoot()
  const root = fixtureSources()
  fixtureHook(root, mainsail)

  const gen = run(root, '--out', mainsail)
  assert.equal(gen.status, 0, gen.out)

  // --list names what the run wrote, relative to the --out root.
  const listed = run(root, '--list').out.split('\n').filter((l) => l && !l.startsWith('skipped'))
  const expected = [
    `${PANEL}/adapter.test.ts`,
    `${PANEL}/adapter.ts`,
    `${PANEL}/components/UnitCard.vue`,
    `${PANEL}/components/tile/TileTile.vue`,
    `${PANEL}/logic/core.ts`,
    `${PANEL}/logic/index.ts`,
    `${PANEL}/logic/types.ts`,
    `${PANEL}/panel.scss`,
    `${PANEL}/store.ts`,
    `${PANEL}/theme.scss`,
    `${PANEL}/tokens.css`,
    `${PANEL}/tokens.scss`,
    'src/components/panels/OpenamsPanel.vue',
  ]
  assert.deepEqual(listed, expected)
  for (const rel of expected) assert.ok(existsSync(join(mainsail, rel)), `missing ${rel}`)

  // The logic row changes nothing but the extension.
  assert.equal(read(mainsail, `${PANEL}/logic/index.ts`), "export { createLogic } from './core';\n")
  // A component one level down reaches the logic with .., two levels with ../..
  const card = read(mainsail, `${PANEL}/components/UnitCard.vue`)
  assert.match(card, /from '\.\.\/logic\/index'/)
  assert.match(card, /from '\.\.\/logic\/types'/)
  // web/vue's vite alias for the logic reaches the generated logic index.
  assert.match(card, /from '\.\.\/logic\/index'/)
  assert.doesNotMatch(card, /@logic/)
  assert.match(read(mainsail, `${PANEL}/components/tile/TileTile.vue`), /from '\.\.\/\.\.\/logic\/index'/)
  // A top-level module in web/vue lands beside the logic, not under it.
  assert.match(read(mainsail, `${PANEL}/store.ts`), /from '\.\/logic\/index'/)
  // A scss that stays inside web/vue keeps its specifier.
  assert.match(read(mainsail, `${PANEL}/theme.scss`), /@use '\.\/panel\.scss'/)
  // The wrapper sits a level above the panel folder, so its paths go through it.
  const panelVue = read(mainsail, 'src/components/panels/OpenamsPanel.vue')
  assert.match(panelVue, /from '\.\/Openams\/logic\/index'/)
  assert.match(panelVue, /from '\.\/Openams\/logic\/types'/)
  assert.match(panelVue, /from '\.\/Openams\/store'/)
  assert.match(panelVue, /from '\.\/Openams\/components\/UnitCard\.vue'/)
  // The adapter lands beside the logic, and a host alias is none of our business.
  const adapter = read(mainsail, `${PANEL}/adapter.ts`)
  assert.match(adapter, /from '\.\/logic\/index'/)
  assert.match(adapter, /from '@\/store\/adapters\/base'/)
  // Nothing anywhere kept a .ts specifier.
  for (const rel of expected.filter((r) => r.endsWith('.ts') || r.endsWith('.vue'))) {
    assert.ok(!/\.ts'/.test(read(mainsail, rel)), `${rel} kept a .ts specifier`)
  }

  // The tokens are web/vue's own files, copied as they are.
  assert.equal(read(mainsail, `${PANEL}/tokens.scss`), TOKENS_SCSS)
  assert.equal(read(mainsail, `${PANEL}/tokens.css`), TOKENS_CSS)
  // The host's test travels with the adapter; the other trees' tests do not.
  assert.equal(read(mainsail, `${PANEL}/adapter.test.ts`), ADAPTER_TEST)
  assert.ok(!existsSync(join(mainsail, `${PANEL}/components/UnitCard.test.ts`)), 'a web/vue test was copied')
  assert.ok(!existsSync(join(mainsail, `${PANEL}/logic/test`)), 'a web/logic test was copied')

  assert.ok(read(mainsail, 'src/store/variables.ts').includes("    'openams',\n"), 'the hook was not applied')
  assert.equal(git(mainsail, 'apply', '--reverse', '--check', join(root, 'web/hosts/mainsail/hook.patch')).trim(), '')

  // --check is happy straight after a run...
  const clean = run(root, '--out', mainsail, '--check')
  assert.equal(clean.status, 0, clean.out)

  // ...and names a generated file that someone edited...
  const edited = join(mainsail, `${PANEL}/logic/core.ts`)
  const before = readFileSync(edited, 'utf8')
  writeFileSync(edited, `${before}export const edited = true\n`)
  const dirty = run(root, '--out', mainsail, '--check')
  assert.equal(dirty.status, 1)
  assert.match(dirty.out, /differs: src\/components\/panels\/Openams\/logic\/core\.ts/)
  writeFileSync(edited, before)
  assert.equal(run(root, '--out', mainsail, '--check').status, 0)

  // ...and a file nobody generated.
  put(mainsail, `${PANEL}/components/Stray.vue`, '<template><div /></template>\n')
  const stray = run(root, '--out', mainsail, '--check')
  assert.equal(stray.status, 1)
  assert.match(stray.out, /extra: src\/components\/panels\/Openams\/components\/Stray\.vue/)
  // A run prunes it: the panel folder is the generator's, and nothing stale stays.
  assert.equal(run(root, '--out', mainsail).status, 0)
  assert.ok(!existsSync(join(mainsail, `${PANEL}/components/Stray.vue`)), 'the stray file was kept')
  assert.equal(run(root, '--out', mainsail, '--check').status, 0)
})

// ---- 2. the real logic typechecks under Mainsail's compiler options --------

let TSC = null
/** typescript@5 in a throwaway directory, installed once per run. */
function typescript() {
  if (TSC) return TSC
  const dir = temp('oams-ts-')
  execFileSync('npm', ['install', '--no-save', '--no-audit', '--no-fund', '--loglevel=error', 'typescript@5'], {
    cwd: dir,
    stdio: 'ignore',
  })
  TSC = join(dir, 'node_modules/typescript/bin/tsc')
  return TSC
}

test('the real web/logic typechecks under the Mainsail compiler options', { timeout: 600000 }, () => {
  const out = temp('oams-real-')
  const gen = run(REPO, '--out', out, '--no-hook')
  assert.equal(gen.status, 0, gen.out)
  assert.ok(existsSync(join(out, `${PANEL}/logic/view.ts`)), 'web/logic was not generated')

  // The fork's strictness, target, module and resolution, and none of its path
  // aliases: the logic has to stand on its own inside the fork. The `lib` is
  // the fork's own (["esnext", ...]); the brief's four options alone would imply
  // the es2020 lib, which is stricter than the fork ever is.
  const co = JSON.parse(readFileSync(join(FIXTURES, 'mainsail-tree/tsconfig.json'), 'utf8')).compilerOptions
  const config = {
    compilerOptions: {
      strict: co.strict,
      target: co.target,
      module: co.module,
      moduleResolution: co.moduleResolution,
      lib: co.lib,
    },
    include: [`${PANEL}/logic/**/*.ts`],
  }
  put(out, 'tsconfig.openams.json', `${JSON.stringify(config, null, 4)}\n`)

  const r = spawnSync(process.execPath, [typescript(), '-p', join(out, 'tsconfig.openams.json'), '--noEmit'], {
    encoding: 'utf8',
  })
  assert.equal(r.status, 0, `tsc --noEmit failed:\n${r.stdout}${r.stderr}`)
})

// ---- 3. determinism --------------------------------------------------------

const digest = (root) => createHash('sha256').update(treeDigest(root, 'src').join(' ')).digest('hex')

test('two runs over the same inputs give identical trees', () => {
  for (const root of [REPO, fixtureSources()]) {
    const a = temp('oams-det-a-')
    const b = temp('oams-det-b-')
    assert.equal(run(root, '--out', a, '--no-hook').status, 0)
    assert.equal(run(root, '--out', b, '--no-hook').status, 0)
    assert.ok(treeDigest(a, 'src').length > 1, 'nothing was generated')
    assert.equal(digest(a), digest(b), 'two runs differ')
  }
})

// ---- the rewriter on its own ----------------------------------------------
test('rewrites a specifier that reaches web/logic from any row', () => {
  const map = [{ prefix: 'web/logic', dest: `${PANEL}/logic` }]
  // theme.scss sits at the panel root, so the logic is its child, not above it.
  const scss = "@use '../../../logic/colors.scss';\n@import './local';\n@use 'vuetify';\n"
  const out = rewriteImports(scss, { srcDir: 'web/vue/src/components', destFile: `${PANEL}/theme.scss`, map })
  assert.equal(out, "@use './logic/colors.scss';\n@import './local';\n@use 'vuetify';\n")
  // A comment that looks like an import is not one.
  const commented = "// import { x } from '../../logic/y.ts'\nimport { x } from './z.ts'\n"
  assert.equal(
    rewriteImports(commented, { srcDir: 'web/vue/src', destFile: `${PANEL}/a.ts`, map }),
    "// import { x } from '../../logic/y.ts'\nimport { x } from './z'\n"
  )
  // A bundler alias is re-aimed at the generated file it stands for.
  assert.equal(
    rewriteImports("import type { View } from '@logic'\nimport x from 'vue'\n", {
      srcDir: 'web/vue/src/components',
      destFile: `${PANEL}/components/A.vue`,
      map,
      aliases: { '@logic': `${PANEL}/logic/index` },
    }),
    "import type { View } from '../logic/index'\nimport x from 'vue'\n"
  )
  // The host wrapper is the one row that also reaches web/vue.
  const wrapper = rewriteImports("import u from '../../vue/src/store.ts'\n", {
    srcDir: 'web/hosts/mainsail',
    destFile: 'src/components/panels/OpenamsPanel.vue',
    map: [
      { prefix: 'web/vue/src', dest: PANEL },
      { prefix: 'web/vue', dest: PANEL },
      ...map,
    ],
  })
  assert.equal(wrapper, "import u from './Openams/store'\n")
})

// ---- the target's own prettier, and no banners ----------------------------

test('a source banner is dropped, and the fork\'s own prettier formats the rest', { timeout: 600000 }, () => {
  const mainsail = mainsailRoot()
  const root = sourceRoot()
  // Deliberately not in the fork's style: 2 spaces, semicolons, and a banner
  // naming a tool the fork has never heard of.
  put(root, 'web/logic/strings.gen.ts', '// Generated by tools/gen_strings.mjs. Do not edit.\n\nexport const A = 1;\n')
  put(root, 'web/logic/index.ts', "export { A } from './strings.gen.ts';\n")
  execFileSync('npm', ['install', '--no-save', '--no-audit', '--no-fund', '--loglevel=error', 'prettier@3'], {
    cwd: mainsail,
    stdio: 'ignore',
  })

  const gen = run(root, '--out', mainsail)
  assert.equal(gen.status, 0, gen.out)
  const formatted = read(mainsail, `${PANEL}/logic/strings.gen.ts`)
  assert.doesNotMatch(formatted, /generated|do not edit/i)
  assert.equal(formatted, 'export const A = 1\n')
  assert.equal(read(mainsail, `${PANEL}/logic/index.ts`), "export { A } from './strings.gen'\n")
  // --check has to predict what --write did, or the fork is always stale.
  const clean = run(root, '--out', mainsail, '--check')
  assert.equal(clean.status, 0, clean.out)
})
