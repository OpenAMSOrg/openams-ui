// The OpenAMS panel's feed: Mainsail's own store and socket in, the logic's
// apply() calls out. Deliberately free of Vue, so the mapping can be unit
// tested against a fixture-built store.

import { createLogic } from '../../logic'
import type { OpenamsLogic, View } from '../../logic'

/** What Mainsail's socket store re-emits for `notify_openams_spoolman_status`. */
export const OPENAMS_SPOOLMAN_STATUS = 'openams-spoolman-status'

/** A Klipper object name: `openams_ui oams1` is one store key, not a path. */
const OPENAMS_UI_PREFIX = 'openams_ui '

/** The store slices the adapter reads, kept structural so a test can build one. */
export interface OpenamsStoreState {
    printer: Record<string, unknown>
    server?: { spoolman?: { spools?: unknown } } | null
}

/** Mainsail's locales (the file names in its `src/locales`) and the logic's,
 *  which are not the same set: the logic's codes carry the script or the region
 *  Mainsail leaves to the host to work out. */
const PANEL_LANGUAGES = new Map<string, string>([
    ['en', 'en'],
    ['de', 'de'],
    ['es', 'es'],
    ['fr', 'fr'],
    ['it', 'it'],
    ['ja', 'ja'],
    ['ko', 'ko'],
    ['pl', 'pl'],
    ['ru', 'ru'],
    ['zh', 'zh-Hans'],
    ['pt', 'pt-BR'],
    // zh_TW is Traditional script and uk is not Russian: both answer in English.
    ['zh_TW', 'en'],
    ['uk', 'en'],
])

/** The language the panel is drawn in, from the host's own: English for a
 *  locale Mainsail carries and the logic does not. Pure, so the fork can test
 *  the mapping without a store or a socket. */
export function panelLanguage(mainsailLocale: string): string {
    return PANEL_LANGUAGES.get(mainsailLocale) ?? 'en'
}

type Json = Record<string, unknown>

const isObject = (value: unknown): value is Json => typeof value === 'object' && value !== null && !Array.isArray(value)

/** Keep the keys that are there, so an absent reading stays absent and the
 *  mapping keeps its own "unknown" default instead of being handed a zero. */
const pick = (source: Json, keys: string[]): Json => {
    const out: Json = {}

    for (const key of keys) {
        if (key in source) out[key] = source[key]
    }

    return out
}

/** One Spoolman spool in the shape the spool-list mapping reads: the id and
 *  the filament it names. */
const asSpool = (spool: unknown): Json | null => {
    if (!isObject(spool) || typeof spool.id !== 'number') return null

    const filament = isObject(spool.filament) ? spool.filament : {}
    const vendor = isObject(filament.vendor) ? filament.vendor : {}

    return {
        ...pick(spool, ['id', 'archived', 'remaining_weight']),
        filament: { ...pick(filament, ['material', 'color_hex']), vendor: pick(vendor, ['name']) },
    }
}

export class OpenamsAdapter {
    readonly logic: OpenamsLogic = createLogic()

    view(): View {
        return this.logic.view()
    }

    subscribe(listener: (view: View) => void): () => void {
        return this.logic.subscribe(listener)
    }

    /** `server.openams_spoolman.status` and every `notify_openams_spoolman_status`
     *  carry the same object, so both land here. */
    applyComponentStatus(payload: unknown): boolean {
        return this.apply('component_status', '', payload)
    }

    /** The current job's file metadata, for the per-tool filament need. */
    applyMetadata(file: unknown): boolean {
        return this.apply('metadata', '', file)
    }

    /** Spoolman's vendors (`GET /v1/vendor` through `server.spoolman.proxy`), the
     *  names the spool editor offers. */
    applyVendorList(vendors: unknown): boolean {
        return this.apply('vendor_list', '', Array.isArray(vendors) ? vendors : [])
    }

    applySpoolList(spools: unknown): boolean {
        const list = Array.isArray(spools) ? spools.map(asSpool).filter((spool) => spool !== null) : []

        return this.apply('spool_list', '', list)
    }

    /** Every object Mainsail's own subscription already holds, in the order the
     *  logic wants them: `oams_manager` first, so the FPS lanes exist before
     *  the units are attached to them, then each `openams_ui <unit>`, then the
     *  two plain Klipper objects, then the job's and Spoolman's side data.
     *  `apply` merges key by key, so re-applying an unchanged object is free. */
    feedStore(state: OpenamsStoreState): void {
        const printer = state.printer ?? {}

        this.apply('oams_manager', '', printer.oams_manager)
        for (const key of Object.keys(printer)) {
            if (!key.startsWith(OPENAMS_UI_PREFIX)) continue

            this.apply('openams_ui', key.slice(OPENAMS_UI_PREFIX.length), printer[key])
        }
        this.apply('print_stats', '', printer.print_stats)
        this.apply('toolhead', '', printer.toolhead)
        this.applyMetadata(printer.current_file)
        this.applySpoolList(state.server?.spoolman?.spools)
    }

    private apply(kind: string, name: string, obj: unknown): boolean {
        if (obj === null || obj === undefined) return false

        return this.logic.apply(kind, name, obj)
    }
}
