// Seeded randomness for the differential tests. One generator, threaded through
// the whole generator, so a seed reproduces a sequence exactly.

/** mulberry32: a 32-bit state PRNG returning floats in [0, 1). */
export function mulberry32(seed: number): () => number {
  let a = seed >>> 0;
  return function next(): number {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** An integer in [lo, hi], both included. */
export function int(rng: () => number, lo: number, hi: number): number {
  return lo + Math.floor(rng() * (hi - lo + 1));
}

/** One element of @p arr. Empty input is a bug, not a random choice. */
export function pick<T>(rng: () => number, arr: readonly T[]): T {
  if (arr.length === 0) throw new RangeError('rng.pick: empty array');
  return arr[Math.floor(rng() * arr.length)];
}

/** True with probability @p p. */
export function chance(rng: () => number, p: number): boolean {
  return rng() < p;
}

/** @p n distinct elements of @p arr, in their own order. */
export function sample<T>(rng: () => number, arr: readonly T[], n: number): T[] {
  const rest = arr.slice();
  const out: T[] = [];
  while (out.length < n && rest.length > 0) out.push(rest.splice(int(rng, 0, rest.length - 1), 1)[0]);
  return out;
}
