// Overrides for the two fields the core's view model gives a form:
// `submit_label` and `require_change`. The core writes them for the edit form;
// a spec that needs other values (its own label, or none at all, to check the
// fallback) gets this wrapper, which changes nothing else.
import type { Core, View } from '@logic';

export interface FormExtrasFixture {
    submit_label?: string | null;
    require_change?: boolean;
}

/** `core`, with every form in its view carrying `extras`. */
export function withFormExtras(core: Core, extras: FormExtrasFixture): Core {
    return {
        apply: (kind, name, obj) => core.apply(kind, name, obj),
        reset: (keepAlerts) => core.reset(keepAlerts),
        action: (line, form) => core.action(line, form),
        setLanguage: (code) => core.setLanguage(code),
        setClock: (ms) => core.setClock(ms),
        subscribe: (listener) => core.subscribe(listener),
        view: (): View => stamp(core.view(), extras) as View,
    };
}

/** Every object in the tree that carries a form's `fields` is one, so the walk
 *  is by shape rather than by path: the model grows branches and the wrapper
 *  must not have to learn them. */
function stamp(node: unknown, extras: FormExtrasFixture): unknown {
    if (Array.isArray(node)) return node.map((n) => stamp(n, extras));
    if (node === null || typeof node !== 'object') return node;
    const out: Record<string, unknown> = {};
    for (const [key, value] of Object.entries(node)) out[key] = stamp(value, extras);
    if (Array.isArray(out.fields)) Object.assign(out, extras);
    return out;
}