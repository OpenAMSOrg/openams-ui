// The Fluidd adapter against a recorded OpenAMS printer.
//
// The store below is the final state of openams-ui's
// tests/fixtures/recorded/two_lanes.json: its `c2_status` events as Fluidd's
// `oams_manager`, its `openams_ui <unit>` events as Fluidd's
// `openams_ui <unit>`. It is inlined, not read, because the test travels into
// the Fluidd fork beside the adapter, where the fixture does not exist. The
// reduction is lossless: `oams_manager` contributes only `settings` and
// `lanes_by_fps` to the view (the rest of its 28 kB is topology the logic
// never reads), and the two unit objects are the merged diffs in full. What
// the logic then builds is compared with two_lanes.view.json, the view that
// same fixture produces.

import { describe, expect, it } from 'vitest'
import { OpenamsAdapter, panelLanguage } from './adapter'
import type { OpenamsStoreState } from './adapter'

const lane = (id: string, op: string, group: string, bay: string, stage: unknown, ms: boolean) => ({
  id,
  ext: 'extruder',
  op,
  group,
  bay,
  stage,
  runout: 'idle',
  from: null,
  to: null,
  ms
})

const unit = (name: string, idx: number, family: string, serial: string, laneValue: ReturnType<typeof lane>, bays: { id: number; s: string; a: string }[], group: string, act: string[]) => ({
  v: 1,
  unit: name,
  idx,
  family,
  serial,
  online: true,
  ready: true,
  cal: 'c',
  lane: laneValue,
  bays: bays.map((bay) => ({ ...bay, cal: 'c', sp: null })),
  groups: [{ n: group, b: [`${name}-0`], s: 'l' }],
  env: { t: 24, rh: 31 },
  dryer: { state: 'off', target: 0, min: 0, heat: 0, fan: 0, fault: 'none', adapter: name === 'oams1' },
  fault: null,
  nf: 0,
  act
})

const oams1 = unit('oams1', 1, 'ams2', '1FB20001752BC7C0CA7E8CE5', lane('fps1', 'LOADED', 'T0', 'oams1-0', null, true), [
  { id: 0, s: 'l', a: 'ua' },
  { id: 1, s: 'e', a: '' },
  { id: 2, s: 'e', a: '' },
  { id: 3, s: 'e', a: '' }
], 'T0', ['dryer_start', 'dryer_stop', 'clear_fault', 'rfid_scan'])

const oams2 = unit('oams2', 2, 'ams_ht', '1192033683C9A123837A65F2', lane('fps2', 'LOADING', 'T1', 'oams2-0', [['feed'], 0, null], false), [
  { id: 4, s: 'l', a: 'a' }
], 'T1', ['dryer_start', 'dryer_stop', 'clear_fault', 'rfid_scan'])

const manager = {
  ready: true,
  settings: {
    read_tag_on_insertion: true,
    read_tags_at_startup: true,
    auto_reload_on_runout: true,
    auto_create_spoolman_spools: true,
    apply_pa_on_load: false
  },
  lanes_by_fps: {
    fps1: {
      op: 'LOADED',
      group: 'T0',
      unit: [1, 0],
      runout: 'idle',
      state: 'loaded',
      current_slot: 0,
      pressure: 0.5,
      set_point: 0.5,
      stage: null,
      extruder: 'extruder',
      motion_settled: true
    },
    fps2: {
      op: 'LOADING',
      group: 'T1',
      unit: [2, 0],
      runout: 'idle',
      state: 'loading',
      current_slot: 4,
      pressure: 0.5,
      set_point: 0.5,
      stage: { plan: ['feed'], index: 0, failed: null },
      extruder: 'extruder',
      motion_settled: false
    }
  }
}

const store: OpenamsStoreState = {
  printer: {
    oams_manager: manager,
    'openams_ui oams1': oams1,
    'openams_ui oams2': oams2,
    print_stats: { filename: 'job.gcode', state: 'printing' },
    toolhead: { extruder: 'extruder' }
  },
  metadata: { filament_weights: [12, 8] },
  spools: [
    { id: 1, archived: false, remaining_weight: 1000, filament: { material: 'PLA', color_hex: 'FF0000', vendor: { name: 'Acme' } } },
    { id: 2, archived: false, remaining_weight: 500, filament: { material: 'PETG', color_hex: '00FF00', vendor: { name: 'Acme' } } }
  ]
}

const ids = (values: { id: string }[]) => values.map((value) => value.id)
const unitIds = (view: ReturnType<OpenamsAdapter['view']>) =>
  view.toolheads.flatMap((toolhead) => ids(toolhead.units)).concat(ids(view.unassigned_units))

describe('the adapter fed an OpenAMS store', () => {
  it('shows the toolheads and units two_lanes.view.json records', () => {
    const adapter = new OpenamsAdapter()
    adapter.feedStore(store)

    const view = adapter.view()

    // two_lanes.view.json: toolheads fps1 and fps2, oams1 on the first and
    // oams2 on the second, and nothing unassigned.
    expect(ids(view.toolheads)).toEqual(['fps1', 'fps2'])
    expect(unitIds(view)).toEqual(['oams1', 'oams2'])
    expect(ids(view.unassigned_units)).toEqual([])
    expect(view.toolheads[0].units[0].bays).toHaveLength(4)
    expect(view.toolheads[1].units[0].bays).toHaveLength(1)
  })

  it('reaches the same toolheads and units when fed one object at a time, as a status update arrives them', () => {
    const streamed = new OpenamsAdapter()

    streamed.feedStore({ printer: { oams_manager: manager } })
    streamed.feedStore({ printer: { 'openams_ui oams1': oams1 } })
    streamed.feedStore({ printer: { 'openams_ui oams2': oams2 } })

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

  it('takes Spoolman spools from the store, not from the printer objects', () => {
    const adapter = new OpenamsAdapter()
    adapter.feedStore({ printer: {}, spools: store.spools })

    // No OpenAMS unit yet, so the spools have nowhere to show: the point is
    // that a spool without a numeric id never reaches the logic.
    expect(() => adapter.feedStore({ printer: {}, spools: [{ filament: { material: 'PLA' } }] })).not.toThrow()
    expect(JSON.stringify(adapter.view())).not.toContain('Acme')
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
      bays: { 'oams1-0': { spool: { material: 'PLA', vendor: 'Polymaker', color_hex: 'FF0000', remaining_g: 500, remaining_pct: 50 } } }
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

// The card inside Fluidd speaks the page's language, not its own default: a
// Fluidd page is the user's, and the panel is a guest on it (PRINCIPLES.md 6).
// The mapping is the one place the two sets of codes meet, because Fluidd's
// are the file names in src/locales and the logic's carry the script or the
// region.
describe('panelLanguage', () => {
  it('carries a locale both sets name the same way straight through', () => {
    for (const code of ['en', 'de', 'es', 'fr', 'it', 'ja', 'ko', 'pl', 'ru']) {
      expect(panelLanguage(code)).toBe(code)
    }
  })

  it('names the script or the region that Fluidd leaves out', () => {
    expect(panelLanguage('zh-CN')).toBe('zh-Hans')
    expect(panelLanguage('pt_BR')).toBe('pt-BR')
    expect(panelLanguage('pt')).toBe('pt-BR')
  })

  it('answers in English for a locale the panel does not carry', () => {
    // Traditional script is not ours, and Ukrainian is never Russian.
    expect(panelLanguage('zh-HK')).toBe('en')
    expect(panelLanguage('uk')).toBe('en')
    // The rest of Fluidd's locales, which the panel has no strings for.
    for (const code of ['af', 'ar', 'cs', 'hu', 'nl', 'sl', 'sv', 'ta', 'th', 'tr']) {
      expect(panelLanguage(code)).toBe('en')
    }
    // A code nothing has heard of, and no code at all.
    expect(panelLanguage('xx')).toBe('en')
    expect(panelLanguage('')).toBe('en')
  })
})
