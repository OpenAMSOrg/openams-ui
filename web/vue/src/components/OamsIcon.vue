<template>
    <svg :width="size" :height="size" viewBox="0 0 24 24" aria-hidden="true">
        <path :d="path" />
    </svg>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'

/** A Material Design icon (an @mdi/js path) at the current color. */
@Component
export default class OamsIcon extends Vue {
    @Prop({ required: true }) readonly path!: string
    @Prop({ default: 18 }) readonly size!: number
}
</script>

<style lang="scss" scoped>
svg {
    display: block;
    fill: currentColor;
}
</style>
