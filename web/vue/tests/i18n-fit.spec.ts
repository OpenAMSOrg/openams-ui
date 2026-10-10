import { expect, test, type Page } from '@playwright/test';
import { open, type PanelState } from './panel';

// Does a translation still fit the box the English was drawn in (PRINCIPLES.md
// 2: a state changes the content, never the geometry)?
//
// A translation never changes the tree - only the words in it - so the same
// element is found in every language by where it sits, and compared with the
// English element it stands for. Three things are a failure:
//
//   overflow  a box that holds the English word and not the translated one
//   wrap      an element taller than the English one: a line broke
//   clipped   text the box hides, which no height comparison would catch
//
// Overflow is judged against the English, never against zero. A status tag may
// be wider than the ring it sits on; when the English already spills, a
// translation that spills the same way is the same box, not a new fault. What
// must not happen is a fit the English had and the translation lost.

/** The ten languages the panel ships, by code. */
const LANGS = ['de', 'es', 'fr', 'it', 'pl', 'pt-BR', 'zh-Hans', 'ja', 'ru', 'ko'] as const;

/** The widths the panel ships on: a desktop card and a phone. */
const WIDTHS = [1200, 360] as const;

/** A text element, as the browser measures it. */
interface Box {
    /** Where it sits in the page, the one thing a translation never changes. */
    path: string;
    text: string;
    /** The room it gives its text (clientWidth/Height) ... */
    w: number;
    h: number;
    /** ... and the room the text needs (scrollWidth/Height). */
    needW: number;
    needH: number;
    /** How many lines the text is drawn on. A taller box is not a wrapped one:
     *  every script has its own line box, and a Japanese line is two pixels
     *  taller than an English one without wrapping at all. */
    lines: number;
    /** True when the box hides whatever does not fit. */
    clips: boolean;
}

/** One pixel: a subpixel box rounds differently in each property. */
const SLOP = 1;

const overflows = (b: Box): boolean => b.needW > b.w + SLOP;
const wrapped = (b: Box, than: Box): boolean => b.lines > than.lines;
const clipped = (b: Box): boolean => b.clips && b.needH > b.h + SLOP;

/** Every text element on the page, keyed by where it sits. An element is a
 *  text element when it holds a text node of its own, so a box that only
 *  holds other boxes is never measured twice, and one that is not drawn (a
 *  closed popover) is not measured at all. */
async function measure(page: Page): Promise<Map<string, Box>> {
    const rows = await page.evaluate(() => {
        const out: Record<string, Omit<Box, 'path'>> = {};
        const stage = document.querySelector('#stage');
        if (!stage) return out;
        for (const el of stage.querySelectorAll<HTMLElement>('*')) {
            const own = [...el.childNodes].some(
                (n) => n.nodeType === Node.TEXT_NODE && (n.textContent ?? '').trim() !== '',
            );
            if (!own) continue;
            const rect = el.getBoundingClientRect();
            if (rect.width < 1 || rect.height < 1) continue;
            const parts: string[] = [];
            for (let n: HTMLElement | null = el; n && n !== stage; n = n.parentElement) {
                parts.unshift(`${n.tagName.toLowerCase()}[${[...n.parentElement!.children].indexOf(n)}]`);
            }
            const style = getComputedStyle(el);
            // One client rect per line the text is actually drawn on.
            const range = document.createRange();
            range.selectNodeContents(el);
            out[parts.join('>')] = {
                text: (el.textContent ?? '').trim(),
                w: el.clientWidth,
                h: el.clientHeight,
                needW: el.scrollWidth,
                needH: el.scrollHeight,
                lines: range.getClientRects().length,
                clips: style.overflowY === 'hidden' || style.overflowY === 'clip',
            };
        }
        return out;
    });
    return new Map(Object.entries(rows).map(([path, box]) => [path, { path, ...box }]));
}

/** Open the gallery in one language and measure it. */
async function measureIn(
    page: Page,
    state: PanelState,
    width: number,
    code: string,
): Promise<Map<string, Box>> {
    await open(page, { state, width, lang: code, ...(state === 'player' ? { fixture: 'two_lanes' } : {}) });
    return measure(page);
}

/** What the translation lost against the English, one sentence per fault. */
function faults(en: Map<string, Box>, got: Map<string, Box>): string[] {
    const out: string[] = [];
    for (const [path, b] of got) {
        const was = en.get(path);
        // An element the English did not draw is a change in the tree, not a
        // fit: nothing to compare it with, and nothing this check is about.
        if (!was) continue;
        if (overflows(b) && !overflows(was)) {
            out.push(`${path}: "${b.text}" needs ${b.needW}px, has ${b.w}px (English "${was.text}" fits ${was.needW}px)`);
        }
        if (wrapped(b, was)) {
            out.push(`${path}: "${b.text}" runs onto ${b.lines} lines, the English "${was.text}" onto ${was.lines}`);
        }
        if (clipped(b)) {
            out.push(`${path}: "${b.text}" is clipped: ${b.needH}px of text in ${b.h}px`);
        }
    }
    return out;
}

/** How much of the page is in another language than it was. Units, materials
 *  and T-numbers are never translated, so this is a share, not a count: a
 *  language that fell back to English everywhere would draw the same boxes and
 *  pass every fit there is. */
function translated(en: Map<string, Box>, got: Map<string, Box>): number {
    let n = 0;
    for (const [path, b] of got) if (en.get(path)?.text !== b.text) n++;
    return en.size === 0 ? 0 : n / en.size;
}

/** A floor no complete translation can miss, and a broken language cannot
 *  reach: the ids that are never translated are a small part of what is drawn. */
const MOSTLY_TRANSLATED = 0.2;

for (const width of WIDTHS) {
    for (const state of ['sheet', 'player'] as const) {
        for (const code of LANGS) {
            test(`${state} at ${width}px fits in ${code}`, async ({ page }) => {
                const en = await measureIn(page, state, width, 'en');
                const got = await measureIn(page, state, width, code);
                // The language reached the page: a fit check that measured
                // English twice would pass every one of these.
                await expect(page.locator('html')).toHaveAttribute('lang', code);
                expect(translated(en, got), `almost nothing was translated into ${code}`).toBeGreaterThan(MOSTLY_TRANSLATED);
                expect(faults(en, got), `${state} at ${width}px in ${code}`).toEqual([]);
            });
        }
    }
}
