<template>
    <svg viewBox="0 0 32 32" aria-hidden="true">
        <g class="fil" :fill="color" stroke="rgba(255,255,255,0.42)" stroke-width="0.85" stroke-linejoin="round">
            <rect x="11" y="1.8" width="10" height="2.6" rx="0.5" />
            <rect x="12.3" y="5.3" width="7.4" height="1.55" rx="0.25" />
            <rect x="12.3" y="7.5" width="7.4" height="1.55" rx="0.25" />
            <rect x="12.3" y="9.7" width="7.4" height="1.55" rx="0.25" />
            <rect x="11.2" y="12.1" width="9.6" height="5.4" rx="0.7" />
            <path d="M13.1 17.5h5.8L17.6 29.2h-3.2L13.1 17.5z" />
        </g>
    </svg>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'

/** The toolhead glyph; the fill is the loaded tool's color. */
@Component
export default class ToolheadIcon extends Vue {
    @Prop({ required: true }) readonly color!: string
}
</script>

<style lang="scss" scoped>
.fil {
    transition: fill 0.25s ease;
}
</style>
