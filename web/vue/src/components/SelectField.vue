<template>
    <div class="select-field">
        <button
            :id="id"
            type="button"
            class="select-btn"
            role="combobox"
            aria-haspopup="listbox"
            :aria-expanded="open"
            :aria-controls="listId"
            :aria-activedescendant="activeId"
            :aria-labelledby="labelledBy || undefined"
            :data-field="field"
            @click="toggle"
            @keydown="onKey">
            <span v-if="showSwatches" class="swatch" :class="swatchClass(chosen)" :style="swatch(chosen)" aria-hidden="true"></span>
            <span class="select-label">{{ chosenLabel }}</span>
            <span class="caret" aria-hidden="true"></span>
        </button>
        <div
            :id="listId"
            ref="list"
            popover="auto"
            class="oams-pop select-list"
            role="listbox"
            :aria-labelledby="labelledBy || undefined"
            :data-list="field">
            <div
                v-for="(option, i) in options"
                :id="optionId(i)"
                :key="option.value"
                class="option"
                role="option"
                :data-index="i"
                :data-option="option.value"
                :data-active="String(i === active)"
                :aria-selected="option.value === value"
                @mouseenter="hover(i)"
                @click="choose(i)">
                <span v-if="showSwatches" class="swatch" :class="swatchClass(option)" :style="swatch(option)" aria-hidden="true"></span>
                <span class="option-label">{{ option.label }}</span>
            </div>
        </div>
    </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator';
import type { ViewFieldOption } from '@logic';
import { followAnchors, place, prePlace } from '../popover';

/** How long a typed prefix lives before the next letter starts a new one. */
const TYPE_AHEAD_MS = 700;

/** A form's `select` field: a button showing the current option, and a popup
 *  list that follows the panel's own tokens in either theme. It is the panel's
 *  answer to a native `<select>`, whose list the browser paints in its own
 *  colors whatever the theme, and whose highlight follows the title bar rather
 *  than the pointer (PRINCIPLES.md 1, 12: the host's look, everywhere).
 *
 *  The popup is a `popover`, so the top layer, the click-outside and the
 *  Escape are the browser's; the placement and the list itself are ours. */
@Component
export default class SelectField extends Vue {
    /** The field's id, which its label names. */
    @Prop({ required: true }) readonly id!: string;
    /** The field's own id, the hook a spec finds it by. */
    @Prop({ required: true }) readonly field!: string;
    /** The id of the `<label>` that names this control. */
    @Prop({ default: '' }) readonly labelledBy!: string;
    @Prop({ default: () => [] }) readonly options!: ViewFieldOption[];
    @Prop({ default: 0 }) readonly value!: number;
    /** A color field draws each option with its own swatch. */
    @Prop({ default: false }) readonly swatches!: boolean;

    /** Where the list is, and which option the pointer or the keys are on. */
    open = false;
    active = -1;

    readonly listId = `${this.id}-list`;
    /** The prefix that has been typed; a second letter joins it. */
    private typed = '';
    private typedAt = 0;

    get list(): HTMLElement {
        return this.$refs.list as HTMLElement;
    }

    private get btn(): HTMLElement {
        return this.$el.querySelector('.select-btn') as HTMLElement;
    }

    /** A color field, or a list whose options carry a `color` of their own (a
     *  Spoolman spool), draws a swatch on each option. */
    get showSwatches(): boolean {
        return this.swatches || this.options.some((o) => o.color !== undefined);
    }

    /** The option the button names. */
    get chosen(): ViewFieldOption | undefined {
        return this.options.find((o) => o.value === this.value);
    }

    get chosenLabel(): string {
        return this.chosen?.label ?? '';
    }

    get activeId(): string | undefined {
        return this.open && this.active >= 0 ? this.optionId(this.active) : undefined;
    }

    optionId(i: number): string {
        return `${this.listId}-o${i}`;
    }

    mounted() {
        followAnchors();
        this.list.addEventListener('beforetoggle', (e) => {
            if ((e as ToggleEvent).newState === 'open') {
                this.open = true;
                // A list opens on the chosen option, unless the keys have
                // already named another one (type-ahead opens it there).
                if (this.active < 0) this.active = this.selectedIndex();
                prePlace(this.list, this.btn);
            }
        });
        this.list.addEventListener('toggle', (e) => {
            if ((e as ToggleEvent).newState !== 'open') {
                this.open = false;
                this.active = -1;
                return;
            }
            window.requestAnimationFrame(() => {
                place(this.list, this.btn);
                this.reveal();
            });
        });
    }

    /** A click on the button is the list's own open/close; the click-outside
     *  and the Escape that follow are the browser's, as they are for every
     *  popover the panel draws. */
    toggle(): void {
        if (this.isOpen()) this.close();
        else this.openList();
    }

    /** What the browser thinks, not what this component last told it: a
     *  popover's `toggle` event is queued, so a second press arriving before
     *  it has been read would otherwise be answered by a flag a task behind. */
    isOpen(): boolean {
        const list = this.$refs.list as HTMLElement | undefined;
        return !!list && list.matches(':popover-open');
    }

    selectedIndex(): number {
        return this.options.findIndex((o) => o.value === this.value);
    }

    hover(i: number): void {
        this.active = i;
    }

    /** The pointer chose: the value goes up and the list goes away. */
    choose(i: number): void {
        const option = this.options[i];
        if (!option) return;
        this.$emit('input', option.value);
        this.close();
    }

    /** A swatch is the option's own color, in the core's own encoding: a
     *  decimal RGB number, and -1 for "not set" (PRINCIPLES.md 6: the color is
     *  content, and "none" is drawn as the empty look, never as a color). */
    swatch(option: ViewFieldOption | undefined): Record<string, string> {
        // An option with a color of its own: "#rrggbb" is the fill, null is
        // nothing known (the empty look, never black).
        if (option && option.color !== undefined) return option.color ? { background: option.color } : {};
        const v = option?.value ?? -1;
        if (v < 0) return {};
        return { background: `rgb(${(v >> 16) & 255}, ${(v >> 8) & 255}, ${v & 255})` };
    }

    swatchClass(option: ViewFieldOption | undefined): Record<string, boolean> {
        if (option && option.color !== undefined) return { 'swatch-unset': !option.color };
        return { 'swatch-unset': (option?.value ?? -1) < 0 };
    }

    close(): void {
        if (this.isOpen()) this.list.hidePopover();
    }

    /** Put the list under the button and on the screen; beforetoggle places it
     *  before it has a box, and the frame after that places it for real (see
     *  popover.ts). */
    private openList(): void {
        this.list.showPopover();
    }

    /** Keep the option the keys are on inside the list's own scroll. */
    private reveal(): void {
        const node = this.$el.querySelector(`#${CSS.escape(this.optionId(this.active))}`) as HTMLElement | null;
        node?.scrollIntoView({ block: 'nearest' });
    }

    /** Arrows, Home/End, Enter/Space and type-ahead, on the button the focus never
     *  leaves: the list is a popup, and moving the focus into it would hide the
     *  one control that says what is open. Escape is deliberately not handled:
     *  the list is above the dialog in the top layer, so the browser's own
     *  light dismiss closes the list and leaves the dialog alone. Closing it
     *  here as well would be two closes for one key. */
    onKey(event: KeyboardEvent): void {
        const key = event.key;
        const open = this.isOpen();
        if (key === 'Enter' || key === ' ') {
            // Closed, the button's own activation opens the list; open, the key
            // chooses, and the default (which would close it) is stopped.
            if (!open) return;
            event.preventDefault();
            this.choose(this.active);
            return;
        }
        if (key === 'ArrowDown' || key === 'ArrowUp' || key === 'Home' || key === 'End') {
            event.preventDefault();
            this.move(key);
            return;
        }
        if (key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
            event.preventDefault();
            this.jumpTo(key);
        }
    }

    private move(key: string): void {
        if (!this.isOpen()) {
            this.openList();
            return;
        }
        const last = this.options.length - 1;
        const here = this.active < 0 ? this.selectedIndex() : this.active;
        const at = key === 'Home' ? 0
            : key === 'End' ? last
            : key === 'ArrowDown' ? here + 1
            : here - 1;
        this.active = last < 0 ? -1 : (at + this.options.length) % this.options.length;
        this.reveal();
    }

    /** Type-ahead: the next option whose name starts with what has been typed,
     *  from the one after the active one, so the same letter cycles. A prefix
     *  nothing matches falls back to its own last letter, as a file picker's
     *  does, rather than leaving the pointer of the keys stranded. */
    private jumpTo(key: string): void {
        const now = Date.now();
        const whole = now - this.typedAt > TYPE_AHEAD_MS ? key : this.typed + key;
        this.typedAt = now;
        const from = this.active < 0 ? -1 : this.active;
        const once = this.match(whole, from);
        const hit = once < 0 ? this.match(key, from) : once;
        if (hit < 0) return;
        this.typed = whole;
        this.active = hit;
        if (this.isOpen()) this.reveal();
        else this.openList();
    }

    /** The first option at or after `from` whose name starts with `prefix`. */
    private match(prefix: string, from: number): number {
        const want = prefix.toLowerCase();
        for (let step = 1; step <= this.options.length; step += 1) {
            const i = (from + step) % this.options.length;
            if (this.options[i].label.toLowerCase().startsWith(want)) return i;
        }
        return -1;
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

.select-field {
    width: 128px;
}

.select-btn {
    @include oams-focus;
    display: flex;
    align-items: center;
    gap: 8px;
    width: 100%;
    min-height: 32px;
    margin: 0;
    padding: 4px 8px;
    border: 1px solid var(--oams-line);
    border-radius: 8px;
    background: var(--oams-surface-3);
    color: inherit;
    font: inherit;
    text-align: left;
    cursor: pointer;

    &:hover {
        border-color: var(--oams-text-faint);
    }
}

.select-label {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
}

/* The one chevron a list button has; its direction is the list's state. */
.caret {
    flex: none;
    width: 0;
    height: 0;
    border: 4px solid transparent;
    border-top-color: var(--oams-text-muted);
    margin-top: 3px;
}

.select-btn[aria-expanded='true'] .caret {
    margin-top: -3px;
    border-top-color: transparent;
    border-bottom-color: var(--oams-text-muted);
}

/* The same card ActionPopover draws (see there for why a popover carries its
   own base), anchored to its field and scrolled inside itself. The browser's
   own popover styles are reset the same way: `inset: auto` for the measured
   placement, and no `display` of our own, so a list that is shut is still the
   browser's `display: none` and takes no clicks. */
.select-list {
    position: fixed;
    inset: auto;
    margin: 0;
    padding: 4px;
    width: max-content;
    min-width: 150px;
    max-width: min(280px, calc(100vw - 24px));
    border: 1px solid color-mix(in srgb, var(--oams-text, currentColor) 14%, var(--oams-pop-base, #14161a));
    border-radius: 12px;
    background: color-mix(in srgb, var(--oams-text, currentColor) 5%, var(--oams-pop-base, #14161a));
    color: var(--oams-pop-ink, inherit);
    text-shadow: none;
    font-family: var(--oams-font-ui, inherit);
    font-size: var(--oams-fs-body);
    box-shadow: 0 12px 32px rgba(0, 0, 0, 0.32);
    overflow-y: auto;
    overscroll-behavior: contain;

    &::backdrop {
        background: transparent;
    }
}

.option {
    display: flex;
    align-items: center;
    gap: 8px;
    min-height: 32px;
    padding: 5px 8px;
    border-radius: 7px;
    font-size: 13px;
    line-height: 1.3;
    cursor: pointer;

    /* The option under the pointer is the one that is lit, and the chosen one
       keeps a mark so it is told apart from where the pointer is. */
    &[data-active='true'] {
        background: var(--oams-surface-3);
        color: var(--oams-text);
    }

    &[aria-selected='true'] {
        font-weight: 600;
    }
}

.option-label {
    min-width: 0;
    overflow: hidden;
    white-space: nowrap;
    text-overflow: ellipsis;
}

/* A filament color is content, so it is drawn as the color itself, with a hair
   line so a black or a white one is never a hole in the list (principle 6). */
.swatch {
    flex: none;
    width: 16px;
    height: 16px;
    border-radius: 4px;
    box-shadow: inset 0 0 0 1px var(--oams-line);
}

/* "Not set" is the design's one empty look, the same one an empty bay has. */
.swatch-unset {
    @include oams-hatch;
}
</style>