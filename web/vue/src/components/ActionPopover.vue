<template>
    <div
        :id="anchorId"
        ref="pop"
        popover="auto"
        class="oams-pop"
        :data-popover="name">
        <h4 v-if="heading" class="pop-head">{{ heading }}</h4>
        <slot />
        <div v-if="actions.length" class="pop-actions">
            <action-row v-for="(action, i) in actions" :key="action.id + i" :action="action" />
        </div>
    </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator';
import type { ViewAction } from '@logic';
import ActionRow from './ActionRow.vue';
import { findInvoker, findInvokers, followAnchors, place, prePlace } from '../popover';

/** An action list (docs/design/UNIFIED_UI.md 7: an ActionSheet is a popover
 *  anchored to its source). A native `popover` gives the top layer, the light
 *  dismiss, the Escape and the "one at a time, never nested" of principle 10
 *  without a line of code; only the placement is ours. */
@Component({ components: { ActionRow } })
export default class ActionPopover extends Vue {
    /** The id the source's `popovertarget` names. */
    @Prop({ required: true }) readonly anchorId!: string;
    /** The popover's own word for what it acts on, from the core. */
    @Prop({ default: '' }) readonly heading!: string;
    /** A test hook, so a spec can name the popover it opened. */
    @Prop({ required: true }) readonly name!: string;
    @Prop({ default: () => [] }) readonly actions!: ViewAction[];

    mounted() {
        followAnchors();
        const pop = this.pop;
        pop.addEventListener('beforetoggle', (e) => {
            if ((e as ToggleEvent).newState === 'open') prePlace(pop, findInvoker(this.anchorId));
        });
        pop.addEventListener('toggle', (e) => {
            const open = (e as ToggleEvent).newState === 'open';
            // A button that opens something says whether it is open.
            for (const invoker of findInvokers(this.anchorId)) invoker.setAttribute('aria-expanded', String(open));
            // The popover has no box of its own until the browser has laid it
            // out, and the frame after that is when it is painted: measure in
            // the frame, so the flip and the edge clamp are right first time.
            if (open) window.requestAnimationFrame(() => place(pop, findInvoker(this.anchorId)));
        });
    }

    /** Close the popover: one at a time, and a dialog never sits on one. */
    close(): void {
        if (typeof this.pop.hidePopover === 'function' && this.pop.matches(':popover-open')) this.pop.hidePopover();
    }

    private get pop(): HTMLElement {
        return this.$refs.pop as HTMLElement;
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

/* A popover is a small card of its own on the top layer, under the control that
   opened it (see popover.ts). The browser's defaults are reset: it centers a
   popover in the viewport and paints it in the system canvas colors, neither of
   which is the design's. */
.oams-pop {
    position: fixed;
    inset: auto;
    margin: 0;
    padding: 8px;
    width: max-content;
    min-width: 190px;
    max-width: min(320px, calc(100vw - 24px));
    border: 1px solid color-mix(in srgb, var(--oams-text, currentColor) 14%, var(--oams-pop-base, #14161a));
    border-radius: 14px;
    /* A popover floats above the card, so it needs a base of its own rather
       than the 5% alpha --oams-surface-2 a host's surfaces are made of. */
    background: color-mix(in srgb, var(--oams-text, currentColor) 5%, var(--oams-pop-base, #14161a));
    /* Its own ink, never its parent's: a tile's menu sits inside the tile, and
       a light spool's dark ink would vanish on this base (see OpenamsView). */
    color: var(--oams-pop-ink, inherit);
    text-shadow: none;
    font-family: var(--oams-font-ui, inherit);
    font-size: var(--oams-fs-body);
    box-shadow: 0 12px 32px rgba(0, 0, 0, 0.32);
    overflow: auto;

    /* Under its source, aligned to the same edge (see popover.ts, which puts it
       there and keeps it on screen). */
    max-height: calc(100vh - 16px);

    &::backdrop {
        background: transparent;
    }
}

.pop-head {
    font-size: var(--oams-fs-meta);
    font-weight: 600;
    color: var(--oams-text-muted);
    padding: 2px 10px 6px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.pop-actions {
    display: flex;
    flex-direction: column;
    gap: 1px;
}
</style>
