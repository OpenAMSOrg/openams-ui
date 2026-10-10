<template>
    <div v-if="pressure" class="pressure">
        <div class="pressure-row"><span class="pressure-label">{{ pressure.label }}</span><span
                class="pressure-val">{{ pressure.text }}</span></div>
        <div class="pressure-bar" role="meter" :aria-label="pressure.label" aria-valuemin="0" aria-valuemax="1"
            :aria-valuenow="pressure.value" :aria-valuetext="pressure.text">
            <b v-if="bandStyle" class="band" :style="bandStyle" data-band></b><i :style="{ width: fill }"></i><b
                class="setpoint" :style="{ left: setPoint }"></b>
        </div>
        <div class="pressure-scale" aria-hidden="true"><span v-for="(tick, i) in pressure.scale" :key="i">{{ tick }}</span></div>
    </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { ViewToolhead } from '@logic'

const clamp01 = (n: number): number => Math.min(1, Math.max(0, n))

/** The toolhead's pressure: the label from the core, its reading, and the bar
 *  with the set point. The bar stays neutral: a reading is not a judgment
 *  (docs/design/UNIFIED_UI.md 4b). When the host reports the regulator's
 *  normal band, it is drawn as a quiet segment of the track, behind the fill.
 *  The band is information only: the fill and the number never change color,
 *  whatever the reading; a problem still arrives as a message. */
@Component
export default class PressureBar extends Vue {
    @Prop({ default: null }) readonly pressure!: ViewToolhead['pressure']

    get fill(): string {
        return `${(clamp01(this.pressure!.value) * 100).toFixed(2)}%`
    }

    /** The band's segment (left and width), or null when the host has none. */
    get bandStyle(): Record<string, string> | null {
        const band = this.pressure!.band
        if (!band) return null
        const low = clamp01(Math.min(band[0], band[1]))
        const high = clamp01(Math.max(band[0], band[1]))
        return { left: `${(low * 100).toFixed(2)}%`, width: `${((high - low) * 100).toFixed(2)}%` }
    }

    get setPoint(): string {
        return `${(clamp01(this.pressure!.set_point) * 100).toFixed(2)}%`
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

.pressure {
    min-width: 0;
}

.pressure-row {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    margin-bottom: 8px;
}

.pressure-label {
    font-size: 12px;
    letter-spacing: 0.1em;
    text-transform: uppercase;
    color: var(--oams-text-faint);
}

.pressure-val {
    @include oams-mono;
    font-size: var(--oams-fs-value-mono);
    font-variant-numeric: tabular-nums;
    line-height: 1;
}

.pressure-bar {
    position: relative;
    height: 7px;
    border-radius: 99px;
    background: var(--oams-surface-3);
    overflow: hidden;

    > i {
        position: relative;
        z-index: 1;
        display: block;
        height: 100%;
        border-radius: inherit;
        background: var(--oams-accent);
        transition: width 0.12s linear;
    }
}

/* The regulator's band: a slightly lighter stretch of the track, under the fill. */
.band {
    position: absolute;
    top: 0;
    bottom: 0;
    z-index: 0;
    background: color-mix(in srgb, var(--oams-text) 16%, var(--oams-surface-3));
}

.setpoint {
    z-index: 2;
    position: absolute;
    top: 0;
    bottom: 0;
    width: 2px;
    margin-left: -1px;
    background: var(--oams-text-faint);
}

.pressure-scale {
    @include oams-mono;
    display: flex;
    justify-content: space-between;
    margin-top: 5px;
    font-size: 11px;
    color: var(--oams-text-faint);
}
</style>
