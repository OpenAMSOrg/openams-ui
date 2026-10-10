<template>
    <li class="cell">
        <article
            class="lane"
            :class="classes"
            :style="style"
            :data-slot="tile.slot_id"
            :data-state="tile.state">
            <button type="button" class="lane-hit" :aria-label="spoolName" :aria-haspopup="'dialog'"
                :popovertarget="tileAnchor" data-target="tile"
                data-popover-invoker="tile"></button>
            <div class="lane-top">
                <span class="lane-group"><span class="lane-id" :class="{ blank: unlabeled }"
                        :aria-hidden="unlabeled ? 'true' : undefined">{{ tile.label }}</span><spare-badge
                        v-if="tile.spare" :spare="tile.spare" /><button v-if="changeGroup" type="button"
                        class="group-hit" data-target="group" :aria-label="groupLabel" :title="groupTitle"
                        :aria-disabled="changeGroup.enabled ? undefined : 'true'"
                        :aria-haspopup="changeGroup.enabled ? 'dialog' : undefined"
                        @click="openGroup"></button></span><rfid-icon v-if="tile.rfid" />
            </div>
            <div v-if="tile.sublabel" class="lane-sub">{{ tile.sublabel }}</div>
            <div class="lane-name">{{ tile.name }}</div>
            <div ref="meta" class="lane-meta"><span v-if="metaFirst" class="m-first">{{ metaFirst }}</span><span
                    v-if="tile.grams_text" class="g">{{ tile.grams_text }}</span><span v-if="tile.low"
                    class="low">{{ labels.low }}</span></div>
            <div class="lane-bottom">
                <ring v-if="hasRing" :tile="tile" /><span v-else class="ring-space"></span>
                <status-tag v-if="tile.tag" :tag="tile.tag" :quiet="quietTag" />
                <button type="button" class="ring-hit" :aria-label="spoolName" :aria-haspopup="'dialog'"
                    :popovertarget="tileAnchor" data-target="ring"
                    data-popover-invoker="ring"></button>
                <button v-if="tile.tag" type="button" class="tag-hit" :aria-label="tile.tag.text"
                    :aria-haspopup="'dialog'" :popovertarget="tagAnchor"
                    data-target="tag" data-popover-invoker="tag" :title="tagTitle"></button>
            </div>
            <action-popover v-if="tile.tag" :anchor-id="tagAnchor" :heading="tile.tag.text" name="tag">
                <p class="tag-detail">{{ tile.tag.detail }}</p>
                <p v-if="tile.tag.code" class="tag-code">{{ tile.tag.code }}</p>
                <action-row v-for="(action, i) in actions('tag')" :key="'t' + i" :action="action" />
            </action-popover>
            <action-popover :anchor-id="tileAnchor" :heading="spoolName" :actions="menu" name="tile" />
        </article>
    </li>
</template>

<script lang="ts">
import { Component, Inject, Prop, Vue } from 'vue-property-decorator'
import type { ViewAction, ViewLabels, ViewTile } from '@logic'
import ActionPopover from './ActionPopover.vue'
import ActionRow from './ActionRow.vue'
import RfidIcon from './RfidIcon.vue'
import Ring from './Ring.vue'
import SpareBadge from './SpareBadge.vue'
import StatusTag from './StatusTag.vue'
import { INTERACT, type Interactivity } from '../interact'
import { newAnchor } from '../popover'

/** One bay: the label, the sublabel, the spool's name and meta, and the ring
 *  with its tag. Every string is the core's. The state drives the CSS classes
 *  and nothing else, so a new state needs no code here.
 *
 *  The click targets of a tile (UNIFIED_UI.md 4b): the tile and the ring's
 *  row open one menu (the actions the core marked 'tile' or 'ring'); the tag
 *  opens its own details and recovery; the tool pill (`T0 ∞n`) changes the
 *  bay's group directly: the pill is where the group is shown, so it is where
 *  it is changed. */
@Component({ components: { ActionPopover, ActionRow, RfidIcon, Ring, SpareBadge, StatusTag } })
export default class LaneTile extends Vue {
    @Prop({ required: true }) readonly tile!: ViewTile
    @Prop({ required: true }) readonly labels!: ViewLabels
    @Inject(INTERACT) readonly ctrl!: Interactivity

    /** One anchor per target: the ids are unique per page, so two panels
     *  mounted at once never fight over the same popover. */
    private meter: ResizeObserver | null = null

    mounted(): void {
        this.fitMeta()
        // the words are measured in the face they are drawn in
        void document.fonts?.ready.then(() => this.fitMeta())
        const meta = this.$refs.meta as HTMLElement | undefined
        if (meta && typeof ResizeObserver !== 'undefined') {
            this.meter = new ResizeObserver(() => this.fitMeta())
            this.meter.observe(meta)
        }
    }

    updated(): void {
        this.fitMeta()
    }

    beforeDestroy(): void {
        this.meter?.disconnect()
    }

    /** The meta line never clips a word: when "color · grams Low" does not fit
     *  the tile's width in this language, the color name goes first, then the
     *  Low word (the grams are the decision). The container queries below
     *  decide by width alone; this decides by the words that are there. It is
     *  DOM-only on purpose (inline display, no reactive state), so it cannot
     *  re-render itself. */
    fitMeta(): void {
        const meta = this.$refs.meta as HTMLElement | undefined
        if (!meta) return
        const parts = [meta.querySelector<HTMLElement>('.m-first'), meta.querySelector<HTMLElement>('.low')]
        for (const part of parts) if (part) part.style.display = ''
        for (const part of parts) {
            if (!part || meta.scrollWidth <= meta.clientWidth) continue
            part.style.display = 'none'
        }
        // a part dropped by width alone leaves its separator to the next one
        const g = meta.querySelector<HTMLElement>('.g')
        if (g) g.classList.toggle('first', !parts[0] || getComputedStyle(parts[0]).display === 'none')
    }

    readonly tagAnchor = newAnchor()
    readonly tileAnchor = newAnchor()

    /** The bay's own "Change group..." (it is in the tile's menu too). */
    get changeGroup(): ViewAction | undefined {
        return this.tile.actions.find((a) => a.id === 'change_group')
    }

    /** The pill's click: the bay's own change-group, as the tile's menu does. */
    openGroup(): void {
        if (this.changeGroup) this.ctrl.ask(this.changeGroup)
    }

    /** "Change group… (T0)": the action, and the group the pill shows. */
    get groupLabel(): string {
        return this.changeGroup ? `${this.changeGroup.label} (${this.tile.label})` : ''
    }

    /** A dimmed change says why, on hover (principle 8). */
    get groupTitle(): string | undefined {
        const action = this.changeGroup
        return action && !action.enabled && action.reason ? action.reason : undefined
    }

    /** A tile with no spool has no ring, but its place stays reserved so the
     *  tag sits at the same height on every tile. */
    get hasRing(): boolean {
        return this.tile.state !== 'empty'
    }

    /** The meta line's first part. The core's name mirrors the material until a
     *  spool has a name of its own, and then the color name says something new;
     *  a real spool name keeps its material. Empty parts are left out. */
    get metaFirst(): string {
        const tile = this.tile
        if (tile.name.toLowerCase() !== tile.material.toLowerCase()) return tile.material
        // A low spool's line is "grams Low": the word outranks the color name
        // for the little room a tile's meta line has.
        return tile.low ? '' : tile.color_name
    }

    /** An empty bay with no tool has nothing to label: its box stays (the
     *  heights align), its "?" does not show. */
    get unlabeled(): boolean {
        return this.tile.state === 'empty' && this.tile.tool === null
    }

    /** An empty bay says its one word quietly; every other tag keeps its pill. */
    get quietTag(): boolean {
        return this.tile.state === 'empty' && this.tile.tag?.tone === 'neutral'
    }

    get filled(): boolean {
        return !!(this.tile.color || (this.tile.gradient_top && this.tile.gradient_bottom))
    }

    /** The bay's own name, the core's word for it: the menu's heading, and the
     *  accessible name of the tile and of the ring's row (both open the menu). */
    get spoolName(): string {
        return this.tile.sublabel ?? this.tile.label
    }

    /** The tile's one menu: every action the core marked for the tile or the
     *  ring, in the core's own order. The tag's actions stay in the tag's
     *  popover, and the pill opens change-group directly. */
    get menu(): ViewAction[] {
        return this.tile.actions.filter((a) => a.target === 'tile' || a.target === 'ring')
    }

    get tagTitle(): string {
        const tag = this.tile.tag
        if (!tag) return ''
        return tag.code ? `${tag.detail} (${tag.code})` : tag.detail
    }

    /** The core decides which action belongs to which target; a renderer only
     *  lists them (UNIFIED_UI.md 4). */
    actions(target: ViewAction['target']): ViewAction[] {
        return this.tile.actions.filter((a) => a.target === target)
    }

    get classes(): Record<string, boolean> {
        return {
            [`state-${this.tile.state}`]: true,
            [`ink-${this.tile.ink}`]: true,
            dim: this.tile.dim,
            hatched: this.tile.hatched,
            pending: this.tile.pending_confirmation,
            filled: this.filled,
            bare: !this.filled,
        }
    }

    get style(): Record<string, string> {
        const tile = this.tile
        const style: Record<string, string> = {}
        if (tile.color) style['--c'] = tile.color
        if (tile.gradient_top && tile.gradient_bottom) {
            style['--fill'] = `linear-gradient(180deg, ${tile.gradient_top} 0%, ${tile.gradient_bottom} 100%)`
        } else if (tile.color) {
            // The tile fill is data: the same mixes the core computes (design/tokens.json).
            style['--fill'] =
                tile.ink === 'dark'
                    ? 'linear-gradient(180deg, color-mix(in srgb, var(--c) 96%, #000) 0%, color-mix(in srgb, var(--c) 74%, #1a1a1a) 100%)'
                    : 'linear-gradient(180deg, color-mix(in srgb, var(--c) 78%, #141414) 0%, color-mix(in srgb, var(--c) 42%, #1a1a1a) 100%)'
        }
        return style
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

/* ---- lanes: always four across, never stacked. A tile is an abstract
   spool, so it stays clearly taller than wide (3 : 4.15, at least 1.2 : 1);
   as it shrinks, the meta drops first, then the name. Label, sublabel, ring
   and tag stay. ---- */
.cell {
    container: tile / inline-size;
    aspect-ratio: 3 / 4.15;
    min-width: 0;
    align-self: start;
}

.lane {
    --ink: #fff;
    --ring-track: rgba(255, 255, 255, 0.2);
    --ring: clamp(40px, 45cqw, 60px);
    position: relative;
    width: 100%;
    height: 100%;
    overflow: hidden;
    border-radius: var(--oams-radius-tile);
    padding: 9px 8px 8px 10px;
    display: flex;
    flex-direction: column;
    color: var(--ink);
    text-shadow: 0 1px 2px rgba(0, 0, 0, 0.35);
    background: var(--fill, var(--oams-surface-3));

    > :not(.lane-bottom) {
        flex: none;
    }

    &.ink-dark {
        --ink: #15171b;
        --ring-track: rgba(0, 0, 0, 0.16);
        text-shadow: none;
    }

    /* A spool whose color nobody told us: the neutral surface, never a black fill
       that reads as black filament (principle 1). The text keeps the host's own
       ink, so it holds 4.5:1 on the surface in any theme. */
    /* (:where keeps the specificity low, so a loaded or error ring still wins.) */
    &:where(.bare:not(.dim)) {
        --ink: var(--oams-text);
        --ring-track: var(--oams-line);
        box-shadow: inset 0 0 0 1px var(--oams-line);
        text-shadow: none;
    }

    /* One disabled look: flat and dimmed, whatever the state. */
    &.dim {
        --ink: var(--oams-text-muted);
        --ring-track: var(--oams-line);
        background: var(--oams-surface-3);
        box-shadow: inset 0 0 0 1px var(--oams-line);
        text-shadow: none;
    }

    &.state-loaded {
        box-shadow:
            0 0 0 2px var(--oams-accent),
            0 0 14px color-mix(in srgb, var(--oams-accent) 45%, transparent),
            0 0 26px color-mix(in srgb, var(--c, transparent) 45%, transparent);
    }

    /* The hatched look (design/tokens.json "hatch"): a bay with no spool, or a
       tile of an offline unit. Stripes are the text color at a low alpha, so
       they work on any host card, and the text stays at 4.5:1 or better. */
    &.hatched {
        --ink: color-mix(in srgb, var(--oams-text, currentColor) 86%, transparent);
        @include oams-hatch;
    }

    &.dim.state-loaded {
        box-shadow: 0 0 0 2px var(--oams-accent);
    }

    &.state-error {
        box-shadow: 0 0 0 2px var(--oams-error);
    }

    &.dim.state-error {
        box-shadow: 0 0 0 2px var(--oams-error);
    }

    &.state-loading,
    &.state-positioning {
        box-shadow: 0 0 0 2px color-mix(in srgb, var(--oams-info) 70%, transparent);
    }

    &.pending {
        outline: 2px dashed var(--oams-warn);
        outline-offset: -3px;
    }
}

/* ---- the click targets of a tile (UNIFIED_UI.md 4b): the whole tile and the
   ring's row (one menu), and the tag. Each is an empty button laid over what it acts on
   rather than a wrapper around it, so the tile paints exactly as it was
   approved, and each says it is there on hover and on press (principle 10). ---- */

.lane-hit {
    position: absolute;
    inset: 0;
    z-index: 1;
    padding: 0;
    border: 0;
    border-radius: inherit;
    background: transparent;
    cursor: pointer;

    &:hover {
        background: rgba(255, 255, 255, 0.07);
    }

    &:active {
        background: rgba(255, 255, 255, 0.14);
    }

    @include oams-focus;
}

/* The ring's target is the tile's bottom row, not the ring alone: the tag is
   drawn over the ring, so two 44 px targets cannot both be concentric squares
   (principle 12). The row is the ring's own row, and the tag is a chip in it. */
.ring-hit {
    position: absolute;
    left: 0;
    right: 0;
    top: 50%;
    transform: translateY(-50%);
    z-index: 2;
    height: max(44px, var(--ring));
    padding: 0;
    border: 0;
    border-radius: 8px;
    background: transparent;
    cursor: pointer;

    @include oams-focus;
}

/* The tag's target is the tag's own place, grown to the 44 px floor. */
.tag-hit {
    position: absolute;
    left: 50%;
    top: 50%;
    transform: translate(-50%, -50%);
    z-index: 3;
    min-width: 44px;
    min-height: 44px;
    padding: 0;
    border: 0;
    border-radius: 6px;
    background: transparent;
    cursor: pointer;

    @include oams-focus;
}

/* The target is a sibling of what it acts on, so the feedback is painted on
   that: the ring brightens, the tag chip does. */
.lane:has(.ring-hit:hover) .ring {
    filter: brightness(1.1);
}

.lane:has(.ring-hit:active) .ring {
    filter: brightness(0.94);
}

.lane:has(.tag-hit:hover) .status-tag {
    filter: brightness(1.12);
}

.lane:has(.tag-hit:active) .status-tag {
    filter: brightness(0.92);
}

/* The tag's popover: the sentence that explains the tag, and the host's own
   code under it, small and secondary. */
.tag-detail {
    padding: 2px 10px 6px;
    max-width: 280px;
    font-size: 13px;
    line-height: 1.4;
    color: var(--oams-text);
}

.tag-code {
    @include oams-mono;
    padding: 0 10px 8px;
    font-size: 11px;
    color: var(--oams-text-faint);
}

.lane-top {
    display: flex;
    align-items: center;
    gap: 5px;
    min-width: 0;
}

/* The tool pill: the group's name and the spare badge, one target. It is
   stacked above the tile's own target as a whole, so a hover effect that
   gives it a stacking context (the filter below) cannot sink it under the
   tile mid-click. */
.lane-group {
    position: relative;
    z-index: 2;
    display: inline-flex;
    align-items: center;
    gap: 5px;
}

/* The pill's target, grown to the 44 px floor (principle 12) and drawn over
   the tile's own target, so a click on the pill changes the group and a click
   anywhere else on the tile still opens the tile's actions. */
.group-hit {
    position: absolute;
    left: -6px;
    top: 50%;
    transform: translateY(-50%);
    width: max(44px, calc(100% + 12px));
    height: max(44px, 100%);
    padding: 0;
    border: 0;
    border-radius: 8px;
    background: transparent;
    cursor: pointer;

    &[aria-disabled='true'] {
        cursor: default;
    }

    @include oams-focus;
}

.lane-group:has(.group-hit:hover:not([aria-disabled='true'])) {
    filter: brightness(1.15);
}

.lane-id.blank {
    visibility: hidden;
}

.lane-id {
    font-size: var(--oams-fs-tile-label);
    font-weight: var(--oams-fw-tile-label);
    letter-spacing: -0.03em;
    line-height: 1;
}

.lane-sub {
    margin-top: 2px;
    font-size: 11px;
    line-height: 1.2;
    opacity: 0.8;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

/* The spare badge: mono on a dark chip. */
.dim .inf {
    color: var(--oams-text);
    background: var(--oams-surface-3);
    border-color: var(--oams-line);
}

.rfid {
    margin-left: auto;
    width: 14px;
    height: 14px;
    flex: none;
}

.lane-name {
    margin-top: 5px;
    min-height: 1.2em;
    font-size: var(--oams-fs-body);
    font-weight: 500;
    line-height: 1.2;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}

.lane-meta {
    margin-top: 2px;
    min-height: 1.2em;
    font-size: var(--oams-fs-meta);
    line-height: 1.2;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;

    > span:not(.low) {
        opacity: 0.8;
    }

    > span + span::before {
        content: ' · ';
    }

    > .g.first::before {
        content: none;
    }

    /* The word for a spool under 15 %, in the ring's own amber, on its own
       dark chip so it reads on any filament color. */
    > .low {
        margin-left: 4px;
        padding: 0 4px;
        border-radius: 3px;
        background: rgba(0, 0, 0, 0.6);
        color: #ffd27a;
        font-weight: 600;
        text-shadow: none;

        &::before {
            content: none !important;
        }
    }
}

/* The ring sits bottom center; the tag overlays it, centered both ways and
   hiding the percentage. */
.lane-bottom {
    position: relative;
    flex: none;
    margin-top: auto;
    display: flex;
    align-items: center;
    justify-content: center;
    min-height: var(--ring);
}

.ring-space {
    flex: none;
    width: var(--ring);
    height: var(--ring);
}

/* The meta line is "color name · grams  Low". fitMeta() drops the color
   name first (it is the one part with no fixed length in any language), then
   the Low word, when the words do not fit; the grams stay. Under 92 px the
   color name never has room, so it is not drawn at all. A low spool leaves
   the color name out altogether (see metaFirst). */
@container tile (max-width: 92px) {
    .lane-meta .m-first {
        display: none;
    }

    .lane-meta .g::before {
        content: none;
    }
}

@container tile (max-width: 104px) {
    .lane {
        border-radius: var(--oams-radius-tile-compact);
        padding: 7px 5px 6px 7px;
    }

    .lane-top {
        gap: 4px;
    }

    .lane-id {
        font-size: 16px;
    }

    .inf {
        height: 18px;
        font-size: 13px;
        padding: 0 3px;
        border-radius: 5px;
    }

    .rfid {
        width: 12px;
        height: 12px;
    }

    .lane-name {
        margin-top: 3px;
        font-size: 12px;
    }

    .lane-meta {
        font-size: 11px;
    }

    .lane-sub {
        font-size: 10px;
    }

    .lane .status-tag {
        font-size: 10px;
        padding: 1px 4px;
    }
}

@container tile (max-width: 92px) {

    .lane .status-tag {
        font-size: 10px;
        padding: 1px 3px;
    }
}

@container tile (max-width: 80px) {
    /* then the Low word; the grams and the percent are the decision */
    .lane-meta .low {
        display: none;
    }
}

@container tile (max-width: 70px) {
    .lane-name {
        display: none;
    }
}
</style>
