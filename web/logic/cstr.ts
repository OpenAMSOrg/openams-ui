// Small helpers that give the TypeScript port the C core's exact behavior at
// its edges: fixed-size text buffers, C integer widths, and printf's "%.Nf".
// Nothing here is specific to OpenAMS.

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/** The bytes of the UTF-8 character that starts with `b` (1 for ASCII), 0 for
 *  a byte that cannot start one: the C writer's own rule. */
export const utf8Len = (b: number): number => {
  if (b < 0x80) return 1;
  if ((b & 0xe0) === 0xc0) return 2;
  if ((b & 0xf0) === 0xe0) return 3;
  if ((b & 0xf8) === 0xf0) return 4;
  return 0;
};

/**
 * What the C core's buffer writer (`vs_write` in src/view/strings.c) leaves in
 * a `char buf[size]`: at most size - 1 bytes, cut at a character boundary. A
 * character that would not fit whole is dropped rather than split, so the
 * result is always valid UTF-8 - a CJK glyph is three bytes, so a byte-wise
 * cut would lose half of one.
 */
export function cut(s: string, size: number): string {
  if (size <= 0) return '';
  // Each UTF-16 unit is at most 3 UTF-8 bytes, so a short string always fits.
  if (s.length * 3 < size) return s;
  const bytes = encoder.encode(s);
  if (bytes.length < size) return s;
  const limit = size - 1;
  let o = 0;
  while (o < bytes.length) {
    const clen = utf8Len(bytes[o]) || 1;              // not UTF-8: a byte is a byte
    if (o + clen > limit) break;
    o += clen;
  }
  return decoder.decode(bytes.subarray(0, o));
}

/** C's `(int)double`: truncation toward zero. */
export const toInt = (v: number): number => Math.trunc(v);

/** C's `(int8_t)` and `(int16_t)` casts: two's-complement wrap-around. */
export const i8 = (v: number): number => (Math.trunc(v) << 24) >> 24;
export const i16 = (v: number): number => (Math.trunc(v) << 16) >> 16;

/** C's `(float)double`, kept as the double the wasm side would print. */
export const f32 = Math.fround;

/**
 * printf("%.<digits>f", value) for a finite value: exact decimal rounding with
 * ties to even, which is what glibc and musl do (and JavaScript's toFixed does
 * not: it rounds an exact tie up). `value` is a float32 or an integer, so
 * value * 10^digits is exact in a double and a tie is detected exactly.
 */
export function fmtFixed(value: number, digits: number): string {
  const scale = 10 ** digits;
  const scaled = Math.abs(value) * scale;
  let n = Math.floor(scaled);
  const frac = scaled - n;
  if (frac > 0.5 || (frac === 0.5 && n % 2 === 1)) n += 1;
  const text = String(n).padStart(digits + 1, '0');
  const body = digits === 0 ? text : `${text.slice(0, -digits)}.${text.slice(-digits)}`;
  return value < 0 || Object.is(value, -0) ? `-${body}` : body;
}

/** Whether two parsed-JSON values are equal the way cJSON_Compare says. */
export function jsonEqual(a: unknown, b: unknown): boolean {
  if (a === b) return true;
  if (typeof a !== typeof b || a === null || b === null) return false;
  if (Array.isArray(a) || Array.isArray(b)) {
    if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
    return a.every((v, i) => jsonEqual(v, b[i]));
  }
  if (typeof a === 'object') {
    const oa = a as Record<string, unknown>;
    const ob = b as Record<string, unknown>;
    const ka = Object.keys(oa);
    if (ka.length !== Object.keys(ob).length) return false;
    return ka.every((k) => k in ob && jsonEqual(oa[k], ob[k]));
  }
  return false;
}

/**
 * The double the C core's JSON printer (cJSON) shows for `d`: a whole number
 * exactly, otherwise "%1.15g" when that reads back within one epsilon of `d`
 * (so a float32 like 0.74 prints as 0.740000009536743), else the exact
 * "%1.17g". The wasm side is parsed by JSON.parse, so this is the value the
 * tests compare against.
 */
export function cjsonNumber(d: number): number {
  if (Number.isInteger(d) && Math.abs(d) < 2 ** 31) return d;
  const short = Number(d.toPrecision(15));
  const max = Math.max(Math.abs(short), Math.abs(d));
  return Math.abs(short - d) <= max * Number.EPSILON ? short : Number(d.toPrecision(17));
}
