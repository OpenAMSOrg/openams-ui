// The adapter against a small OpenAMS printer: the store Mainsail would hold
// (an oams_manager, two openams_ui units on two FPS lanes), and the logic's
// view must come out with the toolheads and units it holds.

import { describe, expect, it } from 'vitest'
import { OpenamsAdapter, panelLanguage } from './adapter'
import type { OpenamsStoreState } from './adapter'

const lane = (id: string, group: string, bay: string) => ({
    id,
    ext: 'extruder',
    op: 'LOADED',
    group,
    bay,
    stage: null,
    runout: 'idle',
    from: null,
    to: null,
    ms: true,
})

const unit = (name: string, idx: number, fps: string, group: string) => ({
    v: 1,
    unit: name,
    idx,
    family: 'ams2',
    serial: '',
    online: true,
    lane: lane(fps, group, `${name}-0`),
    bays: [0, 1, 2, 3].map((id) => ({ id: idx * 4 + id, s: id === 0 ? 'l' : 'e', cal: 'c', sp: null })),
    groups: [{ n: group, b: [`${name}-0`], s: 'r' }],
    env: { t: 24, rh: 31 },
    dryer: null,
    fault: null,
    nf: 0,
    act: [],
})

const manager = {
    ready: true,
    settings: { read_tag_on_insertion: true },
    lanes_by_fps: { fps1: { pressure: 0.5, set_point: 0.5 }, fps2: { pressure: 0.5, set_point: 0.5 } },
}

const printer: Record<string, unknown> = {
    oams_manager: manager,
    'openams_ui oams1': unit('oams1', 1, 'fps1', 'T0'),
    'openams_ui oams2': unit('oams2', 2, 'fps2', 'T1'),
    print_stats: { filename: 'job.gcode', state: 'printing' },
    toolhead: { extruder: 'extruder' },
}

const store: OpenamsStoreState = { printer, server: { spoolman: { spools: [] } } }

const ids = (values: { id: string }[]) => values.map((value) => value.id)
const unitIds = (view: ReturnType<OpenamsAdapter['view']>) =>
    view.toolheads.flatMap((toolhead) => ids(toolhead.units)).concat(ids(view.unassigned_units))

describe('the adapter fed an OpenAMS store', () => {
    it('shows one toolhead per FPS lane, each with its unit', () => {
        const adapter = new OpenamsAdapter()
        adapter.feedStore(store)

        const view = adapter.view()

        expect(ids(view.toolheads)).toEqual(['fps1', 'fps2'])
        expect(unitIds(view)).toEqual(['oams1', 'oams2'])
        expect(view.toolheads[0].units[0].bays).toHaveLength(4)
    })

    it('reaches the same toolheads and units when fed one object at a time, as a status update arrives them', () => {
        const streamed = new OpenamsAdapter()

        streamed.feedStore({ printer: { oams_manager: manager } })
        streamed.feedStore({ printer: { 'openams_ui oams1': printer['openams_ui oams1'] } })
        streamed.feedStore({ printer: { 'openams_ui oams2': printer['openams_ui oams2'] } })

        const view = streamed.view()

        expect(ids(view.toolheads)).toEqual(['fps1', 'fps2'])
        expect(unitIds(view)).toEqual(['oams1', 'oams2'])
    })

    it('feeding the same store again changes nothing', () => {
        const adapter = new OpenamsAdapter()
        adapter.feedStore(store)
        const before = JSON.stringify(adapter.view())

        adapter.feedStore(store)

        expect(JSON.stringify(adapter.view())).toBe(before)
    })
})

// The spool editor is offered while the host's component can edit (`edit` in its
// status) and Spoolman is up, and starts its vendor choice from Spoolman's list.
describe('the spool editor', () => {
    const editOf = (view: ReturnType<OpenamsAdapter['view']>) =>
        view.toolheads[0].units[0].bays[0].actions.find((action) => action.id === 'edit_spool')

    it('is dimmed until the component says it can edit, then offers the vendors', () => {
        const adapter = new OpenamsAdapter()

        adapter.feedStore(store)
        adapter.applyComponentStatus({ spoolman_online: true, bays: {} })
        expect(editOf(adapter.view())?.enabled).toBe(false)

        adapter.applyComponentStatus({
            spoolman_online: true,
            edit: true,
            bays: { 'oams1-0': { spool: { material: 'PLA', vendor: 'Polymaker', color_hex: 'FF0000', remaining_g: 500, remaining_pct: 50 } } },
        })
        adapter.applyVendorList([{ id: 1, name: 'Polymaker' }, { id: 2, name: 'Bambu Lab' }])

        const edit = editOf(adapter.view())
        expect(edit?.enabled).toBe(true)
        const vendor = edit?.form?.fields.find((field) => field.id === 'vendor')
        // Generic first, then Spoolman's own vendors sorted, then the curated brands
        // Spoolman does not already list, each once.
        const labels = vendor?.options?.map((option) => option.label) ?? []
        expect(labels.slice(0, 3)).toEqual(['Generic', 'Bambu Lab', 'Polymaker'])
        expect(labels).toContain('Prusament')
        expect(new Set(labels).size).toBe(labels.length)
    })
})

// The panel inside Mainsail speaks the page's language, not its own default:
// a Mainsail page is the user's, and the panel is a guest on it (PRINCIPLES.md
// 6). The mapping is the one place the two sets of codes meet, because they are
// not the same codes: the logic's carry the script or the region.
describe('panelLanguage', () => {
    it('carries a locale both sets name the same way straight through', () => {
        for (const code of ['en', 'de', 'es', 'fr', 'it', 'ja', 'ko', 'pl', 'ru']) {
            expect(panelLanguage(code)).toBe(code)
        }
    })

    it('names the script or the region that Mainsail leaves out', () => {
        expect(panelLanguage('zh')).toBe('zh-Hans')
        expect(panelLanguage('pt')).toBe('pt-BR')
    })

    it('answers in English for a locale the panel does not carry', () => {
        // Traditional script is not ours, and Ukrainian is never Russian.
        expect(panelLanguage('zh_TW')).toBe('en')
        expect(panelLanguage('uk')).toBe('en')
        // A code nothing has heard of, and no code at all.
        expect(panelLanguage('xx')).toBe('en')
        expect(panelLanguage('')).toBe('en')
    })
})
