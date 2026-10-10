<template>
    <v-btn class="btn" :class="action.style" text :ripple="false" :data-action="action.id"
        :data-enabled="String(action.enabled)" :data-pending="pending ? 'true' : null" :aria-disabled="ariaDisabled"
        :aria-busy="pending ? 'true' : null" :title="title" @click="choose">{{ action.label }}</v-btn>
</template>

<script lang="ts">
import { Component, Inject, Prop, Vue } from 'vue-property-decorator'
import type { ViewAction } from '@logic'
import { INTERACT, type Interactivity } from '../interact'

/** A button for an action. Its enabled state and reason come from the core; a
 *  dimmed action stays focusable (aria-disabled, not disabled) and carries its
 *  reason as the tooltip (principle 8: a disabled action says why). A press is
 *  shown at once and stays until the printer's view says it happened, and a
 *  press while pending is ignored (principle 9). */
@Component
export default class ActionButton extends Vue {
    @Prop({ required: true }) readonly action!: ViewAction
    @Inject(INTERACT) readonly ctrl!: Interactivity

    get ariaDisabled(): string | undefined {
        return this.action.enabled ? undefined : 'true'
    }

    get pending(): boolean {
        return this.ctrl.isPending(this.action)
    }

    get title(): string | undefined {
        return this.action.enabled ? undefined : this.action.reason
    }

    choose(): void {
        this.ctrl.ask(this.action)
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

/* Buttons are neutral: red means an error, never an action.

   Vuetify's own button rules (.v-btn:not(.v-btn--round).v-size--default and
   friends) outrank a scoped class, and the design's values are what must show,
   so the geometry and the type are marked important. Everything else (colors,
   the dimmed state) is a plain cascade. */
.btn {
    min-width: 0 !important;
    height: auto !important;
    margin: 0;
    padding: 6px 12px !important;
    border: 1px solid var(--oams-line);
    border-radius: 9px !important;
    background: var(--oams-surface-2);
    color: inherit;
    font-family: inherit;
    font-size: 13px !important;
    font-weight: 400 !important;
    letter-spacing: normal !important;
    text-indent: 0 !important;
    text-transform: none !important;
    box-shadow: none !important;
    cursor: pointer;

    :hover:not([aria-disabled='true']) {
        background: var(--oams-surface-3);
    }

    &.primary {
        background: var(--oams-accent);
        border-color: transparent;
        color: #fff;
    }

    /* The pressed state: at once, and until the printer's view says it
       happened (principle 9). */
    &[data-pending='true'] {
        background: var(--oams-surface-3);
        box-shadow: inset 0 0 0 1px var(--oams-accent);
    }

    /* WCAG exempts disabled controls, but ours are dimmed so that they are
       understood, so the label stays readable (PRINCIPLES.md, floors). */
    &[aria-disabled='true'] {
        opacity: 0.45;
        cursor: not-allowed;

        &:hover {
            background: var(--oams-surface-2);
        }
    }

    @include oams-focus;
}
</style>
