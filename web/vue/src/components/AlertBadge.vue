<template>
    <span class="alert-wrap">
        <span v-if="!group && !clear" class="alert-slot none" aria-hidden="true"></span>
        <span v-else-if="!group" class="alert-slot none" role="img" :aria-label="labels.no_alerts"
            :title="labels.no_alerts">
            <svg class="none-icon" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor"
                stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
                <path d="M5 12.5l4.5 4.5L19 7.5" />
            </svg>
        </span>
        <template v-else>
            <button type="button" class="alert-slot" :class="`tone-${group.tone}`" :aria-label="labels.alerts"
                :aria-haspopup="'dialog'" :popovertarget="anchor"
                data-popover-invoker="alerts">
                <tone-icon :tone="group.tone" /><span class="alert-count">{{ group.count }}</span>
            </button>
            <action-popover :anchor-id="anchor" :heading="labels.alerts" name="alerts">
                <ul class="al-list">
                    <li v-for="(item, i) in group.items" :key="i" class="al-item">
                        <div class="al-title" :class="`tone-${toneFor(item)}`">
                            <tone-icon :tone="toneFor(item)" /><span>{{ item.title }}</span>
                            <span v-if="item.code" class="al-code">{{ item.code }}</span>
                        </div>
                        <p v-if="item.text" class="al-text">{{ item.text }}</p>
                        <div v-if="item.actions.length" class="al-actions">
                            <action-row v-for="(action, j) in item.actions" :key="j" :action="action" />
                        </div>
                    </li>
                </ul>
            </action-popover>
        </template>
    </span>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { Tone, ViewAlert, ViewAlertGroup, ViewLabels } from '@logic'
import ActionPopover from './ActionPopover.vue'
import ActionRow from './ActionRow.vue'
import ToneIcon from './ToneIcon.vue'
import { newAnchor } from '../popover'

/** The alert slot of a toolhead card or a unit header: an icon with a count
 *  that opens its items, each with its own actions (UNIFIED_UI.md 4b). The
 *  slot's space is always reserved, so the layout never changes (principle 2),
 *  and when nothing in its scope alerts at all it rests on a muted check named
 *  "No alerts" (not a control: it does nothing here, so it is not focusable). */
@Component({ components: { ActionPopover, ActionRow, ToneIcon } })
export default class AlertBadge extends Vue {
    @Prop({ default: null }) readonly group!: ViewAlertGroup | null
    /** Nothing in the scope alerts at all (the view's `alert_clear`). An empty
     *  slot without it is not "No alerts": what alerts is shown elsewhere, right
     *  beside the slot (the message row), so the slot stays empty. */
    @Prop({ default: false }) readonly clear!: boolean
    @Prop({ required: true }) readonly labels!: ViewLabels

    /** One popover per badge: the ids are unique per page, so two badges never
     *  fight over the same one. */
    readonly anchor = newAnchor()

    /** A badge is red when any item is a fault or a pause, and yellow
     *  otherwise, the same rule as the message row's (UNIFIED_UI.md 4b). */
    toneFor(item: ViewAlert): Tone {
        return item.severity === 'info' ? 'info' : 'error'
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

/* The badge and its popover are one widget; the wrapper only exists because a
   component has a single root, and it must not take part in the layout the
   reserved slot set up. */
.alert-wrap {
    display: contents;
}

.alert-slot {
    flex: none;
    width: 34px;
    height: 30px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 3px;
}

.alert-slot.none {
    color: var(--oams-text-muted);
}

button.alert-slot {
    /* The host reset (Vuetify) zeroes button padding; the reference keeps the
       browser's own 1px 6px, which the flex row then centers the icon and the
       count against, squeezing the icon to 11px. */
    padding: 1px 6px;
    border: 0;
    border-radius: 999px;
    cursor: pointer;
    font-size: 12px;
    font-weight: 700;
    background: color-mix(in srgb, var(--sev) 18%, transparent);
    color: var(--sev);

    &:hover {
        background: color-mix(in srgb, var(--sev) 30%, transparent);
    }

    @include oams-focus;
}

.al-list {
    display: flex;
    flex-direction: column;
    gap: 2px;
    max-width: 300px;
}

.al-item {
    padding: 4px 10px 6px;

    & + & {
        border-top: 1px solid var(--oams-line);
    }
}

.al-title {
    display: flex;
    align-items: center;
    gap: 6px;
    font-size: 12px;
    font-weight: 600;
    color: var(--sev);
}

/* The host's own code, small and secondary beside its own message. */
.al-code {
    @include oams-mono;
    font-size: 10px;
    font-weight: 400;
    color: var(--oams-text-faint);
}

.al-text {
    margin-top: 2px;
    max-width: 280px;
    font-size: 12px;
    line-height: 1.4;
    color: var(--oams-text);
}

.al-actions {
    display: flex;
    flex-direction: column;
    gap: 1px;
    margin: 4px -6px 0;
}
</style>
