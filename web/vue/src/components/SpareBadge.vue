<template>
    <span class="inf"><span class="inf-sym">&#8734;</span>{{ position }}</span>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { ViewTile } from '@logic'

/** The spare badge: `∞n`, a bare `∞` on the group's loaded member, or nothing
 *  (the caller leaves the place out then). */
@Component
export default class SpareBadge extends Vue {
    @Prop({ required: true }) readonly spare!: NonNullable<ViewTile['spare']>

    get position(): string | number {
        return this.spare.position ?? ''
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

.inf {
    display: inline-flex;
    align-items: center;
    gap: 1px;
    height: 22px;
    @include oams-mono;
    font-size: 16px;
    font-weight: 600;
    line-height: 1;
    color: #fff;
    text-shadow: none;
    background: rgba(0, 0, 0, 0.55);
    border: 1px solid rgba(255, 255, 255, 0.16);
    border-radius: var(--oams-radius-chip);
    padding: 0 5px 0 4px;
    flex: 0 0 auto;
}

.inf-sym {
    font-family: var(--oams-font-ui, sans-serif);
    font-size: 1.3em;
    font-weight: 500;
    line-height: 1;
}
</style>
