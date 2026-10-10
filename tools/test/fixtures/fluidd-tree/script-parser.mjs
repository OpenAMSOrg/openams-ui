// A Vue SFC's template and style blocks are not JavaScript, so they are blanked
// out (keeping the newlines, so every offset and line number still holds) and
// the script block is parsed on its own. A real fork uses vue-eslint-parser;
// this is the same idea in twenty lines, and it is what lets the fixture lint
// a .vue file without a plugin.
//
// Blanking has to keep the length exactly, tags included: ESLint applies a
// fix by range against the text it was given, so a range that is nine
// characters out removes the wrong nine characters, silently.

import { parse } from 'espree'

const blank = (text) => text.replace(/[^\n]/g, ' ')
const SCRIPT = /<script[^>]*>([\s\S]*?)<\/script>/g

export function parseVue (code, options) {
  const parts = []
  let last = 0
  let m

  SCRIPT.lastIndex = 0
  while ((m = SCRIPT.exec(code)) !== null) {
    const open = m[0].indexOf('>') + 1
    const close = m[0].lastIndexOf('</')

    parts.push(blank(code.slice(last, m.index)), blank(m[0].slice(0, open)), m[1], blank(m[0].slice(close)))
    last = m.index + m[0].length
  }
  parts.push(blank(code.slice(last)))

  const text = parts.join('')

  if (text.length !== code.length) throw new Error('blanking a .vue file changed its length')

  return parse(text, options)
}
