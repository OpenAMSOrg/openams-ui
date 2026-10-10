// web/vue/tools/gen_tokens.mjs, under node:test: the generated tokens.css must
// be nothing but well-formed custom properties.
//
// A "$comment" note in design/tokens.json once reached the host theme's block
// as `--oams-$comment: ... (Mainsail's Vuetify palette) ...`. That is invalid
// CSS, and the apostrophe inside the parenthesis made the browser's error
// recovery swallow every declaration after it: in Mainsail the panel lost its
// surfaces, text tints and status colors.

import { test } from 'node:test'
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const REPO = resolve(dirname(fileURLToPath(import.meta.url)), '../..')
const VUE = resolve(REPO, 'web/vue')

/** Each `selector { ... }` block as [selector, [declaration lines]]. */
function blocks(css) {
    const out = []
    const re = /([^{}]+)\{([^{}]*)\}/g
    let m
    while ((m = re.exec(css.replace(/\/\*[\s\S]*?\*\//g, ''))) !== null) {
        const lines = m[2].split('\n').map((l) => l.trim()).filter(Boolean)
        out.push([m[1].trim(), lines])
    }
    return out
}

/** True when quotes pair up and parentheses close, outside and inside quotes. */
function balanced(value) {
    let depth = 0
    let quote = null
    for (const ch of value) {
        if (quote) {
            if (ch === quote) quote = null
        } else if (ch === '"' || ch === "'") {
            quote = ch
        } else if (ch === '(') {
            depth++
        } else if (ch === ')') {
            if (--depth < 0) return false
        }
    }
    return depth === 0 && quote === null
}

test('tokens.css holds only well-formed --oams-* declarations', () => {
    execFileSync(process.execPath, ['tools/gen_tokens.mjs'], { cwd: VUE, stdio: 'pipe' })
    const css = readFileSync(resolve(VUE, 'src/tokens.css'), 'utf8')
    const found = blocks(css)
    assert.deepEqual(found.map(([selector]) => selector),
        ['.oams-panel', '.oams-panel[data-theme="dark"]', '.oams-panel[data-theme="light"]'])
    for (const [selector, lines] of found) {
        for (const line of lines) {
            const m = /^(--oams-[a-z0-9-]+):\s*(.+);$/.exec(line)
            assert.ok(m, `${selector}: not a custom property: ${line}`)
            assert.ok(balanced(m[2]), `${selector}: unbalanced value: ${line}`)
        }
    }
})

test('the host theme defines its surfaces and status colors', () => {
    execFileSync(process.execPath, ['tools/gen_tokens.mjs'], { cwd: VUE, stdio: 'pipe' })
    const [, host] = blocks(readFileSync(resolve(VUE, 'src/tokens.css'), 'utf8'))[0]
    const names = host.map((line) => line.split(':')[0])
    for (const name of ['--oams-surface-2', '--oams-line', '--oams-text-muted', '--oams-warn',
        '--oams-error', '--oams-accent', '--oams-ok']) {
        assert.ok(names.includes(name), `the host block lacks ${name}`)
    }
})
