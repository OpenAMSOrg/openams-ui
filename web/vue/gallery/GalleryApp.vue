<template>
    <div>
        <div id="controls">
            <div class="seg" id="mode"><button v-for="m in modes" :key="m" :data-v="m" :aria-pressed="mode === m"
                    @click="mode = m">{{ m === 'player' ? 'Fixture player' : 'State sheet' }}</button></div>
            <label id="fixture-label">Fixture <select id="fixture" v-model="fixture"><option v-for="name in fixtures"
                        :key="name">{{ name }}</option></select></label>
            <label id="step-label">Events <input id="step" v-model.number="step" type="range" min="0"
                    :max="events.length" /><output id="step-out">{{ step }} / {{ events.length }}</output></label>
            <div class="seg" id="theme"><button v-for="t in themes" :key="t" :data-v="t" :aria-pressed="theme === t"
                    @click="theme = t">{{ t === 'host-gray' ? 'Host, gray card' : t === 'host-white' ? 'Host, white card' : t }}</button></div>
            <div class="seg" id="width"><button v-for="w in widths" :key="w" :data-v="String(w)"
                    :aria-pressed="width === w" @click="width = w">{{ w }}</button></div>
            <label id="lang-label">Language <select id="lang" v-model="lang"><option v-for="l in languages"
                        :key="l.code" :value="l.code">{{ l.label }}</option></select></label>
            <label id="hostlang-label" v-if="isHost">Host language <select id="hostlang" v-model="hostlang"><option
                        value="">as the panel</option><option v-for="l in languages" :key="l.code"
                        :value="l.code">{{ l.label }}</option></select></label>
        </div>
        <div id="stage" v-bind="{ 'data-ready': ready ? 'true' : 'false', 'data-requests': String(requestCount) }">
            <div class="frame" :style="{ width: width + 'px' }">
                <template v-if="mode === 'player'">
                    <div class="card" :class="cardClass">
                        <v-app v-if="isHost" :dark="theme === 'host-gray'" class="host-app"><openams-view
                                :logic="player"
                                :language="hostLanguage"
                                theme="host" @request="onRequest" @language="onLanguage" /></v-app>
                        <openams-view v-else :logic="player" :theme="panelTheme" :language="hostLanguage"
                            @request="onRequest" @language="onLanguage" />
                    </div>
                </template>
                <template v-else>
                    <template v-for="card in sheet">
                        <h1 :key="'h' + card.caption" class="cap">{{ card.caption }}</h1>
                        <div :key="card.caption" class="card" :class="cardClass" :data-key="card.key"
                            :data-lang="card.lang">
                            <v-app v-if="isHost" :dark="theme === 'host-gray'" class="host-app"><openams-view
                                    :logic="card.core"
                                    :language="hostLanguage"
                                    theme="host" @request="onRequest" @language="onLanguage" /></v-app>
                            <openams-view v-else :logic="card.core" :theme="panelTheme" :language="hostLanguage"
                                @request="onRequest" @language="onLanguage" />
                        </div>
                    </template>
                </template>
            </div>
        </div>
    </div>
</template>

<script lang="ts">
import { Component, Vue, Watch } from 'vue-property-decorator';
import { createLogic, type Core, type ObjectKind } from '@logic';
import { LANG_CODES, LANG_NAMES } from '../../logic/strings.gen.ts';
import OpenamsView from '../src/components/OpenamsView.vue';
import { withFormExtras, type FormExtrasFixture } from './forms';
import { build } from './synth';
import { cards, tileStates } from './scenarios';
import type { Scenario } from './synth';

type Mode = 'player' | 'sheet';
type ThemeKey = 'dark' | 'light' | 'host-gray' | 'host-white';

interface FixtureEvent {
  kind: string;
  unit: string | null;
  payload: unknown;
}

interface Fixture {
  events: FixtureEvent[];
}

/** The recorded recorded fixtures, the same ones the Lit gallery played. */
const fixtures = import.meta.glob(
  ['../../../tests/fixtures/recorded/*.json', '!../../../tests/fixtures/recorded/*.view.json'],
  { import: 'default' },
) as Record<string, () => Promise<Fixture>>;

/** ?spools=1: a Spoolman spool list (two with a color, one with none), so a bay
 *  offers "Assign spool". Off by default: the other specs see a bare list. */
const GALLERY_SPOOLS = [
  { id: 1, remaining_weight: 740, filament: { id: 1, material: 'PLA', color_hex: 'c12e1f', vendor: { id: 1, name: 'Polymaker' } } },
  { id: 2, remaining_weight: 500, filament: { id: 2, material: 'PETG', color_hex: '2f8cf0', vendor: { id: 2, name: 'Bambu Lab' } } },
  { id: 3, remaining_weight: 300, filament: { id: 3, material: 'ASA', vendor: { id: 3, name: 'Prusament' } } },
];

/** A scenario of the state sheet, through the real logic. */
function coreFor(scenario: Scenario): Core {
  const core = createLogic();
  build(core, scenario);
  if (new URLSearchParams(location.search).get('spools') === '1') core.apply('spool_list', null, GALLERY_SPOOLS);
  return stamped(core);
}

/** ?form=require-change gives every form a label of its own and makes it wait
 *  for a change; ?form=plain clears both, the way a form the core gives neither
 *  is drawn (see gallery/forms.ts). */
function stamped(core: Core): Core {
  const fixture = new URLSearchParams(location.search).get('form');
  const extras: Record<string, FormExtrasFixture> = {
    'require-change': { submit_label: 'Save changes', require_change: true },
    plain: { submit_label: null, require_change: false },
  };
  return fixture && extras[fixture] ? withFormExtras(core, extras[fixture]) : core;
}

/** The gallery: a fixture player over the real logic, and a state sheet of
 *  synthetic states. Query parameters set the initial state so a script can
 *  drive it: ?mode=player|sheet&fixture=two_lanes&theme=dark&width=420&lang=de
 *  `hostlang` is the other way in, and the one a host uses: the language a
 *  Mainsail panel is handed through its `language` prop. It is separate from
 *  `lang` because `lang` is the gallery's own choice, the way the standalone
 *  page's picker makes it. */
@Component({ components: { OpenamsView } })
export default class GalleryApp extends Vue {
  modes: Mode[] = ['player', 'sheet'];
  themes: ThemeKey[] = ['dark', 'light', 'host-gray', 'host-white'];
  widths = [360, 420, 800, 1200];

  fixtures: string[] = Object.keys(fixtures)
    .map((path) => path.split('/').pop()!.replace('.json', ''))
    .sort();

  mode: Mode = 'player';
  theme: ThemeKey = 'dark';
  width = 1200;
  fixture = 'two_lanes';
  lang: string = LANG_CODES[0] as string;
  /** The language a host panel is handed; '' is no host language, which is the
   *  standalone page and leaves the panel to its own picker. */
  hostlang = '';
  step = -1;
  events: FixtureEvent[] = [];
  player: Core | null = null;
  ready = false;
  requestCount = 0;
  /** The query string, read once: a script drives the gallery through it. */
  private q = new URLSearchParams(location.search);

  @Watch('mode')
  @Watch('theme')
  @Watch('width')
  onChange(): void {
    this.syncHostPalette();
    void this.markReady();
  }

  /** A host has one palette per theme, like Mainsail's dark and light ones: the
   *  status colors the panel reads (--v-error-base ...) follow the card. */
  private syncHostPalette(): void {
    this.$vuetify.theme.dark = this.theme !== 'host-white';
  }

  @Watch('step')
  onStep(): void {
    this.play();
    void this.markReady();
  }

  @Watch('fixture')
  onFixture(): void {
    void this.load();
  }

  @Watch('lang')
  onLang(): void {
    this.setLang(this.lang);
    if (this.player) this.play();
    void this.markReady();
  }

  @Watch('hostlang')
  onHostlang(): void {
    void this.markReady();
  }

  created(): void {
    const q = this.q;
    this.mode = q.get('mode') === 'sheet' ? 'sheet' : 'player';
    const fixture = q.get('fixture');
    this.fixture = this.fixtures.includes(fixture ?? '') ? fixture! : 'two_lanes';
    const theme = q.get('theme');
    this.theme = (['dark', 'light', 'host-gray', 'host-white'].includes(theme ?? '')
      ? theme
      : 'dark') as ThemeKey;
    this.width = Number(q.get('width')) || 1200;
    const lang = q.get('lang');
    this.lang = LANG_CODES.includes(lang as typeof LANG_CODES[number]) ? lang! : this.lang;
    const hostlang = q.get('hostlang');
    this.hostlang = LANG_CODES.includes(hostlang as typeof LANG_CODES[number]) ? hostlang! : '';
    this.setLang(this.lang);
    this.syncHostPalette();
    void this.load();
  }

  /** Every language the build carries, each by its own name, so a translation
   *  can be read on the same screen the English is on. */
  get languages(): { code: string; label: string }[] {
    return LANG_CODES.map((code, i) => ({ code, label: LANG_NAMES[i] }));
  }

  get isHost(): boolean {
    return this.theme.startsWith('host');
  }

  /** What a host panel is handed: null, which is the standalone page and its
   *  own picker, or the language Mainsail would have read off its own locale. */
  get hostLanguage(): string | null {
    return this.hostlang === '' ? null : this.hostlang;
  }

  /** The card the panel is drawn on: a host card (gray or white, inside a
   *  v-app) or the panel's own dark or light background. */
  get cardClass(): string {
    return this.isHost ? (this.theme === 'host-gray' ? 'gray' : 'white') : this.theme;
  }

  get panelTheme(): 'dark' | 'light' {
    return this.theme === 'light' ? 'light' : 'dark';
  }

  /** The state sheet: one fresh core per card, so every panel on the page has
   *  a model of its own.
   *
   *  Each card carries the language it was built in. That is the dependency
   *  this getter needs: a getter is a computed property, and a computed that
   *  reads nothing reactive is never re-evaluated - so without the language
   *  read here, a change of language would leave every card drawn in the last
   *  one. The tag says which language a card is in, for the same reason. */
  get sheet(): { caption: string; key?: string; lang: string; core: Core }[] {
    const lang = this.lang;
    return [
      { caption: 'Tile states: two FPS cards with two units each', key: 'tile-states', lang, core: coreFor(tileStates()) },
      ...cards.map((c) => ({ caption: c.caption, key: c.key, lang, core: coreFor(c.scenario) })),
    ];
  }

  /** Every `request` the panel emits, exactly as the host would receive it
   *  (UNIFIED_UI.md 8). The gallery is where the Mainsail wrapper is wired up
   *  in parallel, so a spec can read what the panel asked for. */
  onRequest(result: unknown, done?: (message?: string) => void): void {
    const list = ((window as unknown as { openamsRequests?: unknown[] }).openamsRequests ??= []);
    list.push(result);
    this.requestCount = list.length;
    // The host's answer to a spool edit, the one call it waits on: `?edit=<words>`
    // is a printer that refuses it in those words, `?edit=hold` one that has not
    // answered yet, and anything else a printer that took it.
    const edit = (result as { kind?: string; method?: string }).method === 'server.openams_spoolman.edit';
    const reply = this.q.get('edit');
    if (edit && reply !== 'hold') done?.(reply ?? undefined);
  }

  /** A panel's own settings popover was used to change the language: the whole
   *  page follows, because the language is one thing for all of them. */
  onLanguage(code: string): void {
    this.setLang(code);
  }

  private async load(): Promise<void> {
    // "logic=none" is the panel before its first view: the host has mounted it
    // but has fed the core nothing yet (principle 2).
    if (this.q.get('logic') === 'none') {
      this.events = [];
      this.player = null;
      await this.markReady();
      return;
    }
    const path = Object.keys(fixtures).find((p) => p.endsWith(`/${this.fixture}.json`));
    if (path) this.events = (await fixtures[path]()).events.filter((e) => e.payload);
    this.step = this.events.length;
    this.play();
    await this.markReady();
  }

  /** Replay the fixture's events into a fresh logic, the way a host's store
   *  would feed the panel. */
  private play(): void {
    const core = createLogic();
    for (const event of this.events.slice(0, Math.max(0, this.step))) {
      if (event.kind === 'c2_status') core.apply('oams_manager', null, event.payload);
      else core.apply(event.kind as ObjectKind, event.unit ?? null, event.payload);
    }
    this.player = stamped(core);
  }

  /** The language the whole page is reviewed in. It is module state inside
   *  the logic, not model state, so one call changes it for every core on the
   *  page; a code this build does not carry falls back to English.
   *
   *  The page is tagged as well as the panel, which now tags itself too: the
   *  gallery is the host around it, and everything a host draws beside the
   *  panel has to say which language the review is in too, or the browser
   *  picks the English font for the German. */
  private setLang(code: string): void {
    if (!createLogic().setLanguage(code)) code = LANG_CODES[0] as string;
    this.lang = code;
    document.documentElement.lang = code;
  }

  /** The stage is "ready" when the panel has painted and the fonts are in, so
   *  a screenshot is never taken mid-render. */
  private async markReady(): Promise<void> {
    this.ready = false;
    await this.$nextTick();
    await new Promise((r) => requestAnimationFrame(() => requestAnimationFrame(r)));
    if (document.fonts) await document.fonts.ready.catch(() => undefined);
    this.ready = true;
  }
}
</script>
