// The fixture fork's own eslint: one fixable rule, so the test can tell "the
// fork formatted this" from "the emitter's text survived". A real fork's
// config is a few hundred rules; what the generator needs from it is the same
// either way.

import { parseVue } from './script-parser.mjs'

const noSemicolons = {
  meta: { fixable: 'code' },
  create (context) {
    const source = context.sourceCode
    return {
      Program (node) {
        const token = source.getTokens(node).find((t) => t.type === 'Punctuator' && t.value === ';')
        if (token) context.report({ node: token, message: 'no semicolons', fix: (f) => f.remove(token) })
      }
    }
  }
}

const rules = {
  plugins: { fork: { rules: { noSemicolons } } },
  rules: { 'fork/noSemicolons': 'error' }
}

export default [
  { files: ['**/*.ts'], ...rules },
  { files: ['**/*.vue'], languageOptions: { parser: { parse: parseVue } }, ...rules }
]
