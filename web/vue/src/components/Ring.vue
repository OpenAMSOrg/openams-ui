<template>
    <div class="ring" :class="{ full: isFull, low: tile.low }" role="img" :aria-label="tile.pct_text">
        <svg viewBox="0 0 36 36">
            <circle class="track" cx="18" cy="18" :r="RADIUS" fill="none" stroke-width="3.4" />
            <circle
                v-if="drawsArc"
                class="arc"
                cx="18"
                cy="18"
                :r="RADIUS"
                fill="none"
                stroke-width="3.4"
                stroke-linecap="round"
                :stroke-dasharray="circumference"
                :stroke-dashoffset="offset"
                transform="rotate(-90 18 18)" />
            <text x="18" y="18">{{ tile.pct_text }}</text>
        </svg>
    </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { ViewTile } from '@logic'

const RADIUS = 15.5
const CIRCUMFERENCE = 2 * Math.PI * RADIUS

/** The remaining-filament ring: the arc from `pct`, the text from `pct_text`.
 *  Geometry only; every string is the core's. */
@Component
export default class Ring extends Vue {
    @Prop({ required: true }) readonly tile!: ViewTile

    RADIUS = RADIUS
    circumference = CIRCUMFERENCE.toFixed(2)

    get pct(): number {
        return this.tile.pct < 0 ? 0 : Math.min(100, this.tile.pct)
    }

    get drawsArc(): boolean {
        return this.pct > 0
    }

    get offset(): string {
        return (CIRCUMFERENCE - (this.pct / 100) * CIRCUMFERENCE).toFixed(2)
    }

    get isFull(): boolean {
        return this.tile.pct >= 100
    }
}
</script>

<style lang="scss" scoped>
.ring {
    flex: none;
    width: var(--ring);
    height: var(--ring);

    svg {
        display: block;
        width: 100%;
        height: 100%;
    }

    .track {
        stroke: var(--ring-track);
    }

    .arc {
        stroke: var(--ink);
        transition: stroke-dashoffset 0.4s ease;
    }

    /* A low ring sits on its own dark disc so the amber arc reads on any filament color. */
    &.low {
        .track {
            fill: rgba(0, 0, 0, 0.6);
            stroke: rgba(255, 255, 255, 0.25);
        }

        .arc {
            stroke: #ffb020;
        }

        text {
            fill: #ffd27a;
        }
    }

    text {
        font-family: var(--oams-font-ui, sans-serif);
        font-size: 10px;
        font-weight: 650;
        fill: var(--ink);
        text-anchor: middle;
        dominant-baseline: central;
        font-variant-numeric: tabular-nums;
        text-shadow: none;
    }

    &.full text {
        font-size: 9.2px;
    }
}
</style>
