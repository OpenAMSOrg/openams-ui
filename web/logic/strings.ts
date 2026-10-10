// The composed strings (numbers and names inside a sentence): the port of the
// vs_* functions in src/view/strings.c. A sentence with a number or a name in it
// is ONE template id with {named} placeholders, and format() below - the port
// of vs_format() - is the only thing that puts one together. The fixed strings live in
// design/strings/<lang>.json and reach here as strings.gen.ts; every
// user-visible word is composed in these two places only (PRINCIPLES.md 5).
//
// The tables are per language, English is the master and the fallback, and
// setLanguage() picks one - the same rules as the C core's view_set_language(),
// which the differential tests hold identical.
//
// Each function takes the size of the C buffer its text lands in and writes
// through the same character-boundary writer the C does (cut, span by span),
// so a long language is cut in the same place in both cores.

import { LANG_CODES, LANG_NAMES, STR_en, TABLES } from './strings.gen.ts';
import type { StrId } from './strings.gen.ts';
import { fmtFixed, utf8Len } from './cstr.ts';

export { STR_en, LANG_CODES, LANG_NAMES, SETTING_KEYS } from './strings.gen.ts';
export type { StrId };

/* ------------------------------------------------------------- language */

/** The language every string is looked up in. Module state, not model state:
 *  the wasm module holds one core, and a fresh TypeScript core shares it too,
 *  so the two cores can only be compared one language at a time (the
 *  differential harness switches both together). */
let current = LANG_CODES[0] as string;

/** The fixed text for `id` in the current language (view_str(VS_<id>)); an id
 *  the current language does not translate falls back to English's. */
export const str = (id: StrId): string =>
  TABLES[current]?.[id] ?? STR_en[id];

/** The action mapper's terse reasons are user sentences in the table; this
 *  is the same lookup under the C's name. */
export const viewStr = str;

/** Every language built in, by code and by its own name (the names are never
 *  translated: the list shows each language in its own script). */
export const languages = (): { code: string; label: string }[] =>
  LANG_CODES.map((code, i) => ({ code, label: LANG_NAMES[i] }));

/** The code of the language in use. */
export const language = (): string => current;

/** Show the view's text in `code`. False (and the current language kept) when
 *  no such language is built in. Nothing is cached: build the view again. */
export function setLanguage(code: string): boolean {
  if (!LANG_CODES.includes(code as typeof LANG_CODES[number])) return false;
  current = code;
  return true;
}

/* ------------------------------------------------------------- templates */

/** One argument of a template: a piece of text, or a number (printed as "%d"),
 *  or a fixed-point number of 2 or 3 decimals. */
export type Arg = string | number | { value: number; decimals: 2 | 3 };

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/** The C's `vs_writer_t`: bytes into a `char[n]`, one character at a time, a
 *  character that would not fit whole is dropped (never split), and a later
 *  span that does fit is still written. */
class Writer {
  private bytes: number[] = [];
  private readonly n: number;
  constructor(n: number) { this.n = n; }
  span(text: string): void {
    if (this.n === 0) return;
    const b = encoder.encode(text);
    let i = 0;
    while (i < b.length) {
      let clen = utf8Len(b[i]) || 1;
      if (i + clen > b.length) clen = b.length - i;
      if (this.bytes.length + clen + 1 > this.n) break;
      for (let k = 0; k < clen; k++) this.bytes.push(b[i + k]);
      i += clen;
    }
  }
  toString(): string {
    return decoder.decode(Uint8Array.from(this.bytes));
  }
}

/** The current language's `id` with every {placeholder} filled from `args`,
 *  written into a buffer of `n` bytes like vs_format() (strings.c). A
 *  placeholder no argument names is left standing as "{name}"; a '{' with no
 *  '}' ends the placeholders and the rest is literal. */
export function format(id: StrId, args: Record<string, Arg>, n: number): string {
  const w = new Writer(n);
  const t = str(id);
  let lit = 0;
  let i = 0;
  while (i < t.length) {
    if (t[i] !== '{') { i++; continue; }
    w.span(t.slice(lit, i));
    const close = t.indexOf('}', i + 1);
    if (close < 0) {
      w.span(t.slice(lit));
      return w.toString();
    }
    const name = t.slice(i + 1, close);
    const a = Object.prototype.hasOwnProperty.call(args, name) ? args[name] : undefined;
    if (a === undefined) w.span(t.slice(i, close + 1));
    else if (typeof a === 'string') w.span(a);
    else if (typeof a === 'number') w.span(String(Math.trunc(a)));
    else w.span(fmtFixed(a.value, a.decimals));
    i = close + 1;
    lit = i;
  }
  w.span(t.slice(lit));
  return w.toString();
}

/** A plain fixed string or piece of text cut into `n` bytes (the C's vs_copy). */
export function copy(text: string, n: number): string {
  const w = new Writer(n);
  w.span(text);
  return w.toString();
}

/* --------------------------------------------------------------- formats */

/** The ring: "?" when unknown (negative), else "NN%". */
export const fmtPct = (pct: number, n: number): string =>
  pct < 0 ? copy(str('UNKNOWN_MARK'), n) : format('PCT', { pct }, n);

/** "" when unknown, else "NN g". */
export const fmtGrams = (grams: number, n: number): string =>
  grams < 0 ? '' : format('GRAMS', { g: grams }, n);

/** "0.480"-style, 3 decimals; "" for an unknown (negative) reading. A bare
 *  number, so it is printed rather than composed. */
export const fmtPressure = (value: number, n: number): string =>
  value < 0 ? '' : copy(fmtFixed(value, 3), n);

/** "0.50"-style, 2 decimals: a tick label of the pressure scale. */
export const fmtScale = (value: number, n: number): string => copy(fmtFixed(value, 2), n);

/** "31% RH | 24°C", "31% RH", "24°C" or "" from whichever readings exist. */
export function fmtEnv(hasPct: boolean, pct: number, hasTemp: boolean, tempC: number, n: number): string {
  if (hasPct && hasTemp) return format('ENV_BOTH', { rh: pct, temp: tempC }, n);
  if (hasPct) return format('ENV_PCT', { rh: pct }, n);
  if (hasTemp) return format('ENV_TEMP', { temp: tempC }, n);
  return '';
}

/** "T1 needs 120 g; its spools have 80 g". */
export const fmtShortfall = (tool: number, need: number, have: number, n: number): string =>
  format('SHORTFALL', { tool, need, have }, n);

/** "65°C · 4h left": a unit heating or dehumidifying. */
export function fmtDryerActive(targetC: number, remainingMin: number, n: number): string {
  const hours = remainingMin > 0 ? Math.trunc((remainingMin + 59) / 60) : 0;
  return format('DRYER_ACTIVE', { temp: targetC, hours }, n);
}

/** "Spool N": a tile's sublabel (the bay, 1-based). */
export const fmtSpoolLabel = (bay: number, n: number): string => format('SPOOL_LABEL', { n: bay + 1 }, n);

/** "oams2 Spool 1": a spool named with its unit's config name. */
export const fmtUnitSpool = (unit: string, bay: number, n: number): string =>
  format('UNIT_SPOOL', { unit, n: bay + 1 }, n);

/** "oams2:2": a bay named by its unit's config name (language-neutral). */
export const fmtUnitBay = (unit: string, bay: number, n: number): string =>
  format('UNIT_BAY', { unit, n: bay + 1 }, n);

/** "T1 (oams2 Spool 2)". */
export const fmtToolSpool = (tool: string, unit: string, bay: number, n: number): string =>
  format('TOOL_SPOOL', { tool, unit, n: bay + 1 }, n);

/** An extruder's display name: underscores removed, each word capitalized
 *  ("dragon_burner" -> "DragonBurner", "extruder" -> "Extruder"). A name, not a
 *  sentence, so nothing here is translatable. */
export function fmtExtruderTitle(name: string, n: number): string {
  let out = '';
  let wordStart = true;
  for (const c of name) {
    if (c === '_') { wordStart = true; continue; }
    out += wordStart && c >= 'a' && c <= 'z' ? c.toUpperCase() : c;
    wordStart = false;
  }
  return copy(out, n);
}

/** "T0 ran out" or "T0 ran out; switching to <spool>". */
export const fmtRunout = (tool: string, spool: string, n: number): string =>
  spool ? format('RUNOUT', { tool, spool }, n) : format('RUNOUT_ALONE', { tool }, n);

/** "T0 ran out; no spare spool is left". */
export const fmtRunoutNoSpare = (tool: string, n: number): string =>
  format('RUNOUT_NO_SPARE', { tool }, n);

/** "<spool> reported an error": the message row's line for a bay in error that
 *  no lane fault covers. */
export const fmtSpoolError = (spool: string, n: number): string =>
  format('MESSAGE_SPOOL_ERROR', { spool }, n);

/** "Low filament: <spool> has <g> g left". */
export const fmtLowFilament = (spool: string, grams: number, n: number): string =>
  format('ALERT_LOW_FILAMENT', { spool, g: grams }, n);

/** A Spoolman spool's picker label: which parts are there picks the template,
 *  so the word order and the separator around the weight belong to the language. */
export function fmtSpoolOption(vendor: string, material: string, grams: number, n: number): string {
  const hasV = vendor !== '';
  const hasM = material !== '';
  const hasG = grams >= 0;
  let id: StrId;
  if (hasV && hasM) id = hasG ? 'SPOOL_OPTION_FULL' : 'SPOOL_OPTION_NO_GRAMS';
  else if (hasV) id = hasG ? 'SPOOL_OPTION_VENDOR_GRAMS' : 'SPOOL_OPTION_VENDOR';
  else if (hasM) id = hasG ? 'SPOOL_OPTION_MATERIAL_GRAMS' : 'SPOOL_OPTION_MATERIAL';
  else id = 'SPOOL_OPTION_GRAMS';
  return format(id, { vendor: hasV ? vendor : '', material: hasM ? material : '', g: grams }, n);
}
