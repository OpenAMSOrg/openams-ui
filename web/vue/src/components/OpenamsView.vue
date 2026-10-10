<template>
    <div class="oams-panel panel" :data-theme="theme">
        <panel-bar v-if="theme !== 'host' && view" :view="view" @language="changeLanguage" />
        <div v-if="note" class="notice panel-notice" :class="`tone-${note.tone}`" role="status">{{ note.text }}</div>
        <template v-if="view">
            <toolhead-card v-for="toolhead in view.toolheads" :key="toolhead.id" :toolhead="toolhead"
                :labels="view.labels" />
            <section v-if="view.unassigned_units.length" class="fps unassigned">
                <div class="mmus">
                    <unit-section v-for="unit in view.unassigned_units" :key="unit.id" :unit="unit"
                        :labels="view.labels" />
                </div>
            </section>
        </template>
        <action-dialog :labels="view ? view.labels : null" />
    </div>
</template>

<script lang="ts">
import { Component, Prop, ProvideReactive, Vue, Watch } from 'vue-property-decorator';
import type { ActionResult, Core, Tone, View } from '@logic'
import { Interactivity, INTERACT, type FormValues } from '../interact'
// The root component brings the design tokens itself: a host wrapper imports
// this file directly, not the package index, and without them every
// var(--oams-*) resolves to nothing (no surfaces, no status colors).
import '../tokens.css'
import ActionDialog from './ActionDialog.vue'
import PanelBar from './PanelBar.vue'
import ToolheadCard from './ToolheadCard.vue'
import UnitSection from './UnitSection.vue'

export type Theme = 'host' | 'dark' | 'light'

/** The panel: it draws the view tree the logic returns and nothing else
 *  (docs/design/UNIFIED_UI.md 2). It reads no printer object and composes no
 *  text; every string on the screen is the core's.
 *
 *   logic  the Core (web/logic) to draw; without one the panel draws its frame.
 *   theme  "host" (the host's card, text and font, through Vuetify's --v-*
 *          variables), "dark" or "light" for a standalone page.
 *   language  the language a host is in, which the panel then draws in; null
 *          for a standalone page, which owns the choice itself.
 *
 * It also owns the one interaction state (Interactivity) and the one dialog,
 * and emits `request` with the ActionResult for every action that has one. That
 * event is the host's contract: a Mainsail wrapper sends the gcode or makes the
 * RPC call, so the panel itself never talks to a printer.
 *
 * The event's second argument, `done(message?)`, is for a call the host waits
 * on (the spool edit, and a G-code script the printer can refuse with words of
 * its own): `done()` ends the action's wait, `done(text)` ends it with the
 * printer's refusal in the panel's message row, in red, after the core's lead. */
// The name is spelled out rather than taken from the class: in Mainsail's
// tree this view and the panel wrapper beside it are both registered, and the
// wrapper keeps the name OpenamsPanel.
@Component({ name: 'OpenamsView', components: { ActionDialog, PanelBar, ToolheadCard, UnitSection } })
export default class OpenamsView extends Vue {
    @Prop({ default: null }) readonly logic!: Core | null
    @Prop({ default: 'host' }) readonly theme!: Theme
    @Prop({ default: null }) readonly language!: string | null

    view: View | null = null
    private stop: (() => void) | null = null
    private made: Interactivity | null = null
    /** The language the core is drawing in, and so the one the panel tags
     *  itself with; the settings popover reports a new one. */
    private code = 'en'

    /** The interaction state, provided to every widget below. */
    @ProvideReactive(INTERACT)
    get ctrl(): Interactivity {
        if (!this.made) this.made = new Interactivity(this.resolve)
        return this.made
    }

    mounted() {
        this.watchLogic()
    }

    beforeDestroy() {
        this.stop?.()
        this.stop = null
        this.made?.dispose()
        this.made = null
    }

    @Watch('logic')
    onLogic() {
        this.watchLogic()
    }

    /** A host changed the language it is in: draw the panel in it again. */
    @Watch('language')
    onLanguage() {
        this.applyLanguage()
    }

    /** The one message row: the panel's own transient message while it lasts,
     *  the core's notice otherwise (principle 2: a state gets its place). */
    get note(): { text: string; tone: Tone } | null {
        if (this.ctrl.message) return { text: this.ctrl.message, tone: this.ctrl.tone }
        if (this.view?.notice) return { text: this.view.notice.text, tone: 'neutral' }
        return null
    }

    /** Draw the current view, then redraw on every flush of the core. */
    private watchLogic() {
        this.stop?.()
        this.stop = null
        const logic = this.logic
        if (!logic) {
            this.view = null
            this.ctrl.setView(null)
            return
        }
        this.view = logic.view()
        this.ctrl.setView(this.view)
        this.tagLanguage()
        this.applyLanguage()
        this.stop = logic.subscribe((view: View) => {
            this.view = view
            this.ctrl.setView(view)
        })
    }

    /** The Language row was used: ask the core to switch and draw the view
     *  again. The language is module state inside the core, not model state,
     *  so nothing the printer sent changed - only the words it is drawn with.
     *  The host is told too, so the page around the panel can say which
     *  language it is in - a Mainsail page is the user's, not the panel's. */
    changeLanguage(code: string): void {
        const logic = this.logic
        if (!logic || !logic.setLanguage(code)) return
        this.code = code
        this.view = logic.view()
        this.ctrl.setView(this.view)
        this.tagLanguage()
        this.$emit('language', code)
    }

    /** A host that names the language owns the panel's words: inside Mainsail
     *  the page's language is the user's, and the panel is a guest on it, so it
     *  is handed the core already drawing in that language. A null prop is the
     *  standalone page, which owns the choice itself and has a picker for it;
     *  nothing is applied, and the panel is exactly what it always was. */
    private applyLanguage(): void {
        if (this.language !== null) this.changeLanguage(this.language)
    }

    /** `lang` on the panel, so the browser, a screen reader and a font stack
     *  all know which language the text on it is in - always the panel's own.
     *  The page's `lang` describes the page and says nothing about us: a
     *  Mainsail page carries `lang="en"` whatever language the user picked, so
     *  a panel that deferred to it tagged German text English and the browser
     *  picked the wrong font (Japanese read as Chinese). The panel's words are
     *  only ever guaranteed to be in the language the core is drawing in, so
     *  that is what the tag has to say.
     *
     *  The code is the core's, read back from the one settings row that is a
     *  choice rather than a switch: the panel can be handed a core already
     *  drawing in another language (the gallery's ?lang=), and the tag has to
     *  say so - a panel that says `lang="en"` over German text picks the
     *  English font for it. The row's own choice is the answer; the last one
     *  picked is the fallback for a view that has no row to read. */
    private tagLanguage(): void {
        const row = this.view?.settings.find((r) => r.options.length > 0);
        const code = (row && row.selected >= 0 ? row.options[row.selected]?.code : null) ?? this.code;
        this.$el.setAttribute('lang', code);
    }

    /** Ask the logic what an action line does, and hand a request to the host.
     *  An error is said in the panel; a "local" line has nothing to send. */
    private resolve(line: string, form: FormValues | null): ActionResult | null {
        const logic = this.logic
        if (!logic) return null
        const result = logic.action(line, form)
        if (result.kind === 'gcode' || result.kind === 'rpc') {
            // The spool editor is the one call refused with its own lead; any
            // other, a G-code script the printer turned down included, takes
            // the general one (H3).
            const edit = result.kind === 'rpc' && result.method === 'server.openams_spoolman.edit'
            this.$emit('request', result, (message?: string) => this.ctrl.settle(line, message, edit ? 'edit' : 'action'))
        }
        return result
    }
}
</script>

<style lang="scss">
/* The panel's own frame, for every child component: a host's card carries its
   own resets, so these are kept to what the design needs (principle 1). */
.oams-panel * {
    box-sizing: border-box;
}

.oams-panel h2,
.oams-panel h3,
.oams-panel h4,
.oams-panel p,
.oams-panel ul,
.oams-panel ol {
    margin: 0;
    padding: 0;
}

.oams-panel ul,
.oams-panel ol {
    list-style: none;
}

.oams-panel button {
    font: inherit;
    color: inherit;
}

/* A tone's severity color, for the alert badges (principle 6). */
.oams-panel .tone-error {
    --sev: var(--oams-error);
}

.oams-panel .tone-info {
    --sev: var(--oams-warn);
}

/* A popover and a dialog float above the panel, so they need a base of their
   own instead of the alpha surfaces a host's card is made of, and an ink of
   their own to read on it. A popover sits in the document inside what opened
   it (a tile's menu and its tag's details inside the tile), so an inherited
   color would be that element's: a light spool's dark ink on the dark base.
   A host's theme is Vuetify's own light/dark class (`theme--light` on the
   application, and on Mainsail's <html>), so the base and the ink follow it. */
.oams-panel {
    --oams-pop-base: #1e1e1e;
    --oams-pop-ink: #ffffff;
}

.oams-panel[data-theme='dark'] {
    --oams-pop-base: #14161a;
    --oams-pop-ink: var(--oams-text);
}

.oams-panel[data-theme='light'] {
    --oams-pop-base: #f4f5f7;
    --oams-pop-ink: var(--oams-text);
}

.theme--light .oams-panel[data-theme='host'] {
    --oams-pop-base: #ffffff;
    --oams-pop-ink: rgba(0, 0, 0, 0.87);
    /* On a light card a darkened surface would read gray: keep the faint
       foreground tint there (design/tokens.json host surface-2). */
    --oams-surface-2: color-mix(in srgb, currentColor 5%, transparent);
}
</style>

<style lang="scss" scoped>
.panel {
    display: flex;
    flex-direction: column;
    gap: var(--oams-gap-card-gap);
    color: var(--oams-text, inherit);
    font-family: var(--oams-font-ui, inherit);
    font-size: var(--oams-fs-body);
    background: var(--oams-bg);
    -webkit-font-smoothing: antialiased;
    container: panel / inline-size;
}

.notice {
    margin: 0 6px 12px;
    padding: 8px 12px;
    border-radius: 10px;
    font-size: 13px;
    background: var(--oams-surface-2);
}

.panel-notice {
    margin: 0 0 12px;
}

/* An error is red, and says so in words, not only in a color (principle 6). */
.notice.tone-error {
    color: var(--oams-error);
}
</style>
