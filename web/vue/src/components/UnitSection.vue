<template>
    <section class="mmu" :class="{ offline: !unit.online }" :data-unit="unit.id" :data-lanes="unit.bays.length"
        :style="lanes">
        <div class="mmu-head">
            <h3>{{ unit.title }}</h3>
            <div class="mmu-alert"><alert-badge :group="unit.alert" :clear="unit.alert_clear" :labels="labels" /></div>
            <div class="sub">{{ unit.subtitle }}</div>
            <div class="climate">
                <div v-if="unit.status_text" class="pill offline-pill">{{ unit.status_text }}</div>
                <button v-if="env" type="button" class="pill env clickable" :data-unit="unit.id" aria-haspopup="true"
                    :popovertarget="anchor" data-popover-invoker="unit"
                    :title="envTitle">{{ env }}</button>
                <div v-else-if="unit.env" class="pill env">{{ unit.env.text }}</div>
                <div v-if="unit.dryer && unit.dryer.text" class="pill dryer"
                    :class="`tone-${unit.dryer.tone}`">{{ unit.dryer.text }}</div>
            </div>
        </div>
        <ul class="lanes">
            <lane-tile v-for="bay in unit.bays" :key="bay.slot_id" :tile="bay" :labels="labels" />
        </ul>
        <!-- No heading: the popover opens from this unit's own card, so naming
             the unit again ("ams2") adds nothing. -->
        <action-popover v-if="env" :anchor-id="anchor" :actions="unit.actions" name="unit" />
    </section>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { ViewLabels, ViewUnit } from '@logic'
import ActionPopover from './ActionPopover.vue'
import AlertBadge from './AlertBadge.vue'
import LaneTile from './LaneTile.vue'
import { newAnchor } from '../popover'

/** A unit: its header (title, config name, alert slot, pills) and its bays.
 *  The card's width follows the lane count (--n), so an AMS HT is as wide as
 *  its one lane and the tiles are the same size in every unit. */
@Component({ components: { ActionPopover, AlertBadge, LaneTile } })
export default class UnitSection extends Vue {
    @Prop({ required: true }) readonly unit!: ViewUnit
    @Prop({ required: true }) readonly labels!: ViewLabels

    /** The humidity and temperature reading opens the drying and heating
     *  menu, when the unit has one (UNIFIED_UI.md 4b). */
    get env(): string | null {
        return this.unit.actions.length > 0 ? this.unit.env?.text ?? null : null
    }

    get envTitle(): string {
        return this.unit.actions[0].label
    }

    /** The popover the env pill opens, and the id that names it. */
    readonly anchor = newAnchor()

    get lanes(): Record<string, string> {
        return { '--n': String(this.unit.bays.length) }
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

/* A unit card's width follows its lane count (--n), and a tile is the same
   size in every card: --t is a four-lane card's tile (108 px at most, less as
   the panel narrows), so a one-lane card holds one standard tile, and never
   less room than its header needs at the standard pill size (about 3 tiles). */
.mmu {
    --mpad: 24px;
    --gap: 8px;
    --t: min(108px, calc((min(480px, 100cqw) - var(--mpad) - 3 * var(--gap)) / 4));
    flex: 0 1 max(324px, calc(var(--n, 4) * var(--t) + (var(--n, 4) - 1) * var(--gap) + var(--mpad)));
    min-width: 0;
    background: var(--oams-surface-2);
    border-radius: var(--oams-radius-section);
    padding: 10px 12px 12px;
}

/* A fixed two-row header at every width: the title and the alert slot, then
   the config name with the pills right-aligned. Nothing wraps, so it never
   changes height. */
.mmu-head {
    display: grid;
    grid-template-columns: minmax(0, auto) minmax(0, 1fr);
    grid-template-rows: 30px 30px;
    grid-template-areas:
        'title alert'
        'sub pills';
    column-gap: 8px;
    align-items: center;
    margin-bottom: 8px;

    h3 {
        grid-area: title;
        font-size: var(--oams-fs-section);
        font-weight: var(--oams-fw-section);
        white-space: nowrap;
    }

    .sub {
        grid-area: sub;
        font-size: var(--oams-fs-meta);
        color: var(--oams-text-faint);
    }
}

.mmu-alert {
    grid-area: alert;
    justify-self: end;
}

.climate {
    grid-area: pills;
    display: flex;
    align-items: center;
    justify-content: flex-end;
    flex-wrap: nowrap;
    gap: 6px;
    min-width: 0;
    overflow: hidden;
}

.pill {
    display: flex;
    align-items: center;
    gap: 8px;
    background: var(--oams-pill-bg);
    color: var(--oams-pill-text);
    border-radius: var(--oams-radius-pill);
    padding: 6px 12px;
    font-size: var(--oams-fs-meta);
    white-space: nowrap;

    &.clickable {
        cursor: pointer;

        &:hover {
            background: color-mix(in srgb, currentColor 12%, transparent);
        }
    }

    &.offline-pill {
        background: color-mix(in srgb, var(--oams-error) 14%, transparent);
        color: var(--oams-error);
    }

    &.tone-heat {
        background: var(--oams-heat-bg);
        color: var(--oams-heat-text);
    }

    &.tone-hold {
        background: color-mix(in srgb, var(--oams-ok) 16%, transparent);
        color: var(--oams-ok);
    }

    &.tone-cool {
        background: color-mix(in srgb, var(--oams-info) 16%, transparent);
        color: var(--oams-info);
    }

    &.tone-fault {
        background: color-mix(in srgb, var(--oams-error) 16%, transparent);
        color: var(--oams-error);
    }
}

button.pill {
    @include oams-focus;
    border: 0;
    font: inherit;
}

.lanes {
    display: grid;
    grid-template-columns: repeat(var(--n, 4), var(--t));
    gap: var(--gap);
}

@container panel (max-width: 440px) {
    .mmu {
        --mpad: 12px;
        --gap: 6px;
        padding: 8px 6px;
        border-radius: 14px;
    }

    .pill {
        padding: 4px 8px;
        font-size: 11px;
    }

    .mmu-head .sub {
        font-size: 11px;
    }
}
</style>
