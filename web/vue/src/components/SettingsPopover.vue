<template>
    <div :id="anchorId" ref="pop" popover="auto" class="oams-pop settings" :data-popover="name">
        <h4 class="pop-head">{{ heading }}</h4>
        <div v-for="row in view.settings" :key="row.key" class="row" :data-setting="row.key">
            <div class="row-head">
                <span class="row-label">{{ row.label }}</span>
                <span v-if="!isChoice(row)" class="row-state">{{ state(row) }}</span>
                <span v-else class="row-state">{{ current(row) }}</span>
            </div>
            <p class="row-note">{{ row.note }}</p>
            <div v-if="isChoice(row)" class="choices" role="radiogroup" :aria-label="row.label">
                <button v-for="(opt, i) in row.options" :key="opt.code" type="button" class="choice"
                    role="radio" :aria-checked="i === row.selected ? 'true' : 'false'" :data-code="opt.code"
                    :lang="opt.code" @click="pick(row, opt.code)">
                    <span class="choice-label">{{ opt.label }}</span>
                    <span v-if="i === row.selected" class="tick" aria-hidden="true">✓</span>
                </button>
            </div>
            <div v-else class="switch">
                <action-row v-if="row.action_off" :action="row.action_off" />
                <action-row v-if="row.action_on" :action="row.action_on" />
            </div>
        </div>
    </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator';
import type { View, ViewSetting } from '@logic';
import ActionRow from './ActionRow.vue';
import { findInvoker, followAnchors, place, prePlace } from '../popover';

/** The host's settings, in one popover (UNIFIED_UI.md 7: a settings popover
 *  anchored to its source). Every label and note is the core's.
 *
 *  The last row is a choice, not a switch: the languages the image carries,
 *  each by its own name, with the current one selected. It sends no action -
 *  picking one emits `language`, and the panel calls logic.setLanguage() with
 *  the code and draws the view again (PRINCIPLES.md 5: the core composes the
 *  text, so one set of translations serves every renderer). */
@Component({ components: { ActionRow } })
export default class SettingsPopover extends Vue {
    @Prop({ required: true }) readonly anchorId!: string;
    @Prop({ required: true }) readonly heading!: string;
    @Prop({ required: true }) readonly name!: string;
    @Prop({ required: true }) readonly view!: View;

    mounted() {
        followAnchors();
        const pop = this.pop;
        pop.addEventListener('beforetoggle', (e) => {
            if ((e as ToggleEvent).newState === 'open') prePlace(pop, findInvoker(this.anchorId));
        });
        pop.addEventListener('toggle', (e) => {
            const open = (e as ToggleEvent).newState === 'open';
            const invoker = findInvoker(this.anchorId);
            invoker?.setAttribute('aria-expanded', String(open));
            if (open) window.requestAnimationFrame(() => place(pop, findInvoker(this.anchorId)));
        });
    }

    /** A choice row is the one with options; the rest are on/off switches. */
    isChoice(row: ViewSetting): boolean {
        return row.options.length > 0;
    }

    /** What a row shows on the right: a switch names the action that would
     *  run, a choice the option it holds. */
    state(row: ViewSetting): string {
        if (this.isChoice(row)) return this.current(row);
        const action = row.value ? row.action_on : row.action_off;
        return action ? action.label : '';
    }

    /** A choice row's own name for what it currently shows. English is the
     *  first option, and every list starts there. */
    current(row: ViewSetting): string {
        return row.selected >= 0 ? row.options[row.selected].label : '';
    }

    pick(row: ViewSetting, code: string): void {
        this.$emit('language', code);
    }

    private get pop(): HTMLElement {
        return this.$refs.pop as HTMLElement;
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

/* The same card ActionPopover draws (see there for why a popover carries its
   own base), a little wider: a settings note is a sentence. */
.settings {
    min-width: 230px;
    max-width: min(360px, calc(100vw - 24px));
}

.pop-head {
    font-size: var(--oams-fs-meta);
    font-weight: 600;
    color: var(--oams-text-muted);
    padding: 2px 10px 6px;
}

.row {
    padding: 6px 4px 8px;
    border-top: 1px solid color-mix(in srgb, currentColor 10%, transparent);

    &:first-of-type {
        border-top: 0;
    }
}

.row-head {
    display: flex;
    align-items: baseline;
    gap: 8px;
}

.row-label {
    font-size: 13px;
    font-weight: 600;
    line-height: 1.3;
}

/* The row's own right-hand value: On/Off, or the language picked. It is a
   fixed slot, so a longer name does not move the rows below it. */
.row-state {
    margin-left: auto;
    font-size: var(--oams-fs-meta);
    color: var(--oams-text-muted);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    max-width: 45%;
}

.row-note {
    font-size: var(--oams-fs-meta);
    color: var(--oams-text-muted);
    line-height: 1.35;
}

.choices {
    display: flex;
    flex-direction: column;
    gap: 1px;
    margin-top: 4px;
}

.choice {
    @include oams-focus;
    display: flex;
    align-items: center;
    gap: 8px;
    width: 100%;
    min-height: 34px;
    padding: 6px 10px;
    border: 0;
    border-radius: 8px;
    background: transparent;
    color: inherit;
    font-family: inherit;
    font-size: 13px;
    text-align: left;
    cursor: pointer;

    &:hover {
        background: var(--oams-surface-3);
    }

    /* Each language in its own script, whatever the UI is showing. */
    &[lang] {
        font-family: var(--oams-lang-ui, inherit);
    }

    &[aria-checked='true'] {
        font-weight: 600;
        background: var(--oams-surface-3);
    }
}

.choice-label {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
}

.tick {
    margin-left: auto;
    color: var(--oams-accent);
}

.switch {
    display: flex;
    gap: 4px;
    margin-top: 4px;
}
</style>
