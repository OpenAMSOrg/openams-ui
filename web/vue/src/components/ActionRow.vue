<template>
    <button
        type="button"
        class="action"
        :class="[action.style, { dim: !action.enabled }]"
        :data-action="action.id"
        :data-enabled="String(action.enabled)"
        :data-pending="pending ? 'true' : undefined"
        :aria-disabled="action.enabled ? undefined : 'true'"
        :aria-busy="pending ? 'true' : undefined"
        :title="title"
        @click="choose">
        <span class="action-label">{{ action.label }}</span>
    </button>
</template>

<script lang="ts">
import { Component, Inject, Prop, Vue } from 'vue-property-decorator';
import type { ViewAction } from '@logic';
import { INTERACT, type Interactivity } from '../interact';
import { closePopover } from '../popover';

/** One action in a popover: its label, its enabled state and its reason, and
 *  the pressed state while it waits for the printer. Enabled and reason come
 *  from the core; a dimmed action stays focusable (aria-disabled, not disabled)
 *  so it can be read and, on a touchscreen, asked why (principles 8, 10). */
@Component
export default class ActionRow extends Vue {
    @Prop({ required: true }) readonly action!: ViewAction;
    @Inject(INTERACT) readonly ctrl!: Interactivity;

    /** The pressed state, at once, and the wait behind it (principle 9). */
    get pending(): boolean {
        return this.ctrl.isPending(this.action);
    }

    get title(): string | undefined {
        return this.action.enabled ? undefined : this.action.reason;
    }

    choose(): void {
        const before = this.ctrl.dialog;
        this.ctrl.ask(this.action);
        // A chosen action is done with the menu: it closes, whether the action
        // went to the printer or a dialog now goes over it (and a dialog leaves
        // no popover behind it). A dimmed action only says why, so the menu
        // stays for another choice.
        if (this.action.enabled || (this.ctrl.dialog && !before)) closePopover(this.$el as Element);
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

/* A menu row. Buttons are neutral: red means an error, never an action, so a
   "danger" action is the plain style with its usual label. */
.action {
    display: flex;
    align-items: center;
    width: 100%;
    min-height: 34px;
    margin: 0;
    padding: 7px 10px;
    border: 0;
    border-radius: 8px;
    background: transparent;
    color: inherit;
    font-family: inherit;
    font-size: 13px;
    font-weight: 400;
    text-align: left;
    text-indent: 0;
    text-transform: none;
    line-height: 1.35;
    cursor: pointer;

    &:hover:not([aria-disabled='true']) {
        background: var(--oams-surface-3);
    }

    &:active:not([aria-disabled='true']) {
        background: color-mix(in srgb, currentColor 12%, transparent);
    }

    &.primary {
        font-weight: 600;
    }

    /* The pressed state: what the user sees within a tenth of a second, and
       what stays until the printer's view says it happened. */
    &[data-pending='true'] {
        background: var(--oams-surface-3);
        box-shadow: inset 2px 0 0 var(--oams-accent);
    }

    /* WCAG exempts disabled controls, but ours are dimmed so that they are
       understood, so the label stays readable (PRINCIPLES.md, floors). */
    &[aria-disabled='true'] {
        opacity: 0.5;
        cursor: not-allowed;

        &:hover {
            background: transparent;
        }
    }

    @include oams-focus;
}
</style>
