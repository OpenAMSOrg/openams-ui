<template>
    <div class="tag-read" :class="`tone-${read.tone}`" role="note" :data-tag-read="read.state">
        <p class="tr-text"><tone-icon :tone="read.tone" /><span>{{ read.text }}</span></p>
        <ul v-if="shown.length" class="tr-candidates">
            <li v-for="(c, i) in shown" :key="i" class="tr-candidate">
                <span class="tr-swatch" :class="{ unknown: !c.color }" :style="swatch(c)" aria-hidden="true"></span>
                <span v-if="c.material" class="tr-material">{{ c.material }}</span>
                <span v-if="c.color_name" class="tr-color">{{ c.color_name }}</span>
            </li>
        </ul>
    </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { ViewTagCandidate, ViewTagRead } from '@logic'
import ToneIcon from './ToneIcon.vue'

/** What the host's tag attribution says about a bay whose reader several bays
 *  share (`tile.tag_read`): a plain note for a tag not read yet, the seen
 *  tag's material and color for a slot not confirmed, and the amber
 *  information look for a conflict (never the fault look). It lives in the
 *  bay's details - the ring's menu and the tag's popover - and never on the
 *  tile (VISUAL_SPEC rule 3). Every word is the core's; its actions ("It is
 *  this slot", Re-read) are in the menu with the bay's others. */
@Component({ components: { ToneIcon } })
export default class TagReadNote extends Vue {
    @Prop({ required: true }) readonly read!: ViewTagRead

    /** A candidate the tag told nothing about has nothing to show. */
    get shown(): ViewTagCandidate[] {
        return this.read.candidates.filter((c) => c.material || c.color)
    }

    swatch(c: ViewTagCandidate): Record<string, string> {
        return c.color ? { '--sw': c.color } : {}
    }
}
</script>

<style lang="scss" scoped>
.tag-read {
    margin: 0 0 6px;
    padding: 6px 10px;
    border-radius: 10px;
    font-size: var(--oams-fs-meta);
    color: var(--oams-text-muted);
    max-width: 300px;

    /* a conflict: the information tone (UNIFIED_UI 4a), as the message row draws it */
    &.tone-info {
        background: color-mix(in srgb, var(--oams-warn) 16%, transparent);
        color: var(--oams-warn);
    }
}

.tr-text {
    display: flex;
    align-items: flex-start;
    gap: 6px;
    margin: 0;
    line-height: 1.35;

    /* ToneIcon's root carries this component's scope too */
    > .tone-icon {
        flex: none;
        margin-top: 1px;
    }
}

.tr-candidates {
    list-style: none;
    margin: 6px 0 0;
    padding: 0;
    display: flex;
    flex-direction: column;
    gap: 4px;
}

.tr-candidate {
    display: flex;
    align-items: center;
    gap: 6px;
    color: var(--oams-text);
}

/* The tag's color as data, with a hairline so a dark or a light filament still
   reads on the popover; a color nobody told us is the hatched neutral swatch. */
.tr-swatch {
    flex: none;
    width: 14px;
    height: 14px;
    border-radius: 4px;
    background: var(--sw);
    box-shadow: inset 0 0 0 1px color-mix(in srgb, var(--oams-text, currentColor) 28%, transparent);

    &.unknown {
        background: repeating-linear-gradient(
            135deg,
            color-mix(in srgb, var(--oams-text, currentColor) 18%, transparent) 0 2px,
            transparent 2px 5px
        );
    }
}

.tr-material {
    font-weight: 600;
}

.tr-color {
    color: var(--oams-text-muted);
}
</style>
