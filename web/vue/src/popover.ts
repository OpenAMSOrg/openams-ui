// Where a popover sits: under the control that opened it, flipped when there
// is no room, and always on screen (docs/design/UNIFIED_UI.md 7: a popover
// uses the `popover` attribute, which gives the top layer, the light dismiss,
// Escape and "one at a time" for free; only the placement is ours).
//
// The placement is measured rather than anchored in CSS: `position-area` puts
// a popover in the containing block's corner in Chromium today, and a menu that
// opens 50 px to the left of the thing you pressed is worse than one that
// follows a scroll.

let seq = 0;

/** The gap between a popover and its source, and the margin it keeps from the
 *  edge of the screen. */
const GAP = 6;
const EDGE = 8;

/** A popover is never narrower than this (see ActionPopover's own styles), so
 *  it is what the first placement may assume. */
const MIN_WIDTH = 190;

/** One popover's id, which its invoker's `popovertarget` names. It is unique
 *  per page, so two panels mounted at once never claim the same one. */
export const newAnchor = (): string => `oams-pop-${++seq}`;

/** The control that last opened a popover, by popover id: a popover may have
 *  more than one invoker (a tile's menu opens from the tile and from its
 *  ring's row), and it sits under the one that was used. */
const used = new Map<string, HTMLElement>();

/** Every control that opens `id`. */
export function findInvokers(id: string): HTMLElement[] {
    return Array.from(document.querySelectorAll<HTMLElement>(`[popovertarget="${id}"]`));
}

/** The control that opens `id`: the one last used, else the first. */
export function findInvoker(id: string): HTMLElement | null {
    const last = used.get(id);
    if (last?.isConnected && last.getAttribute('popovertarget') === id) return last;
    return findInvokers(id)[0] ?? null;
}

/** Close the popover `node` sits in, if any: a dialog is the top layer, and a
 *  popover left open behind it would be left open after it closes (principle
 *  10: never nested). */
export function closePopover(node: Element | null): void {
    const pop = node?.closest('[popover]') as HTMLElement | null;
    if (pop?.matches(':popover-open')) pop.hidePopover();
}

/** Put an open popover just under the control that opened it: above it when
 *  there is no room below, and never off the edge of the screen. */
export function place(pop: HTMLElement, invoker: HTMLElement | null): void {
    if (!invoker) return;
    const { width, height } = pop.getBoundingClientRect();
    at(pop, invoker.getBoundingClientRect(), width, height);
}

/** The same, before the popover has a box of its own: enough to keep it near
 *  its source, and on the screen, while it opens. */
export function prePlace(pop: HTMLElement, invoker: HTMLElement | null): void {
    if (!invoker) return;
    at(pop, invoker.getBoundingClientRect(), MIN_WIDTH, 0, false);
}

function at(pop: HTMLElement, r: DOMRect, width: number, height: number, limit = true): void {
    const below = r.bottom + GAP;
    const above = r.top - GAP - height;
    // Below, unless it would run off the bottom and there is room above.
    const down = below + height <= window.innerHeight - EDGE || above < EDGE;
    const top = down ? below : above;
    const room = down ? window.innerHeight - below - EDGE : r.top - GAP - EDGE;
    const left = Math.min(Math.max(r.left, EDGE), Math.max(EDGE, window.innerWidth - width - EDGE));
    pop.style.left = `${Math.round(left)}px`;
    pop.style.top = `${Math.round(top)}px`;
    // The first placement must not cap the height: the popover is measured
    // after it, and a cap taken from a guess would be the guess.
    if (limit) pop.style.maxHeight = `${Math.max(120, Math.round(room))}px`;
}

/** Keep every open popover with its source while the page moves. One listener
 *  for the whole page: a panel can have hundreds of popovers, all but one of
 *  them closed. */
let watching = false;
export function followAnchors(): void {
    if (watching || typeof window === 'undefined') return;
    watching = true;
    // Remember which invoker was pressed (a keyboard press is a click too).
    document.addEventListener('click', (e) => {
        const el = (e.target as Element | null)?.closest<HTMLElement>('[popovertarget]');
        const id = el?.getAttribute('popovertarget');
        if (el && id) used.set(id, el);
    }, true);
    const placeAll = () => {
        for (const pop of document.querySelectorAll<HTMLElement>('[popover]:popover-open')) {
            place(pop, findInvoker(pop.id));
        }
    };
    window.addEventListener('scroll', placeAll, true);
    window.addEventListener('resize', placeAll);
}
