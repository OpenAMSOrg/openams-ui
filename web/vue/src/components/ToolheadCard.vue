<template>
    <section class="fps" :data-toolhead="toolhead.id">
        <div class="fps-head">
            <div class="fps-title">
                <div class="th-icon" :class="{ loaded: !!tool, outline: !!tool && tool.outline }" :style="style">
                    <toolhead-icon :color="iconColor" /><span v-if="tool" class="th-tool"
                        :class="`ink-${tool.ink}`">{{ tool.label }}</span>
                </div>
                <div class="fps-text">
                    <div class="fps-titleline"><h2>{{ toolhead.title }}</h2><span class="fps-sub">{{ toolhead.subtitle }}</span></div>
                    <p class="fps-loaded" :data-loaded="loaded ? 'true' : 'false'"><template v-if="loaded"><i
                                class="fps-swatch" :class="{ outline: loaded.outline }"
                                :style="swatchStyle"></i><span>{{ loaded.label }}</span></template></p>
                </div>
            </div>
            <pressure-bar :pressure="toolhead.pressure" />
            <div class="fps-alert"><alert-badge :group="toolhead.alert" :clear="toolhead.alert_clear" :labels="labels" /></div>
        </div>
        <div class="status" role="status">
            <div class="status-steps"><stepper v-if="hasStepsOrRest" :activity="toolhead.activity"
                        :label="toolhead.message.text" :quiet-rest="!!loaded" /></div>
            <div class="status-row">
                <message-row :message="toolhead.message" />
                <div class="th-actions">
                    <action-button v-for="action in toolhead.actions" :key="action.id" :action="action" />
                </div>
            </div>
        </div>
        <div class="mmus">
            <unit-section v-for="unit in toolhead.units" :key="unit.id" :unit="unit" :labels="labels" />
        </div>
    </section>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { ViewLabels, ViewToolhead } from '@logic'
import ActionButton from './ActionButton.vue'
import AlertBadge from './AlertBadge.vue'
import MessageRow from './MessageRow.vue'
import PressureBar from './PressureBar.vue'
import Stepper from './Stepper.vue'
import ToolheadIcon from './ToolheadIcon.vue'
import UnitSection from './UnitSection.vue'

/** A toolhead (an FPS lane): the icon and titles (the loaded filament's line
 *  under the title, its own place whether or not one is loaded), the pressure
 *  block centered, the alert slot, the fixed status area with the toolhead's
 *  actions beside the message they act on, and its units. */
@Component({
    components: { ActionButton, AlertBadge, MessageRow, PressureBar, Stepper, ToolheadIcon, UnitSection },
})
export default class ToolheadCard extends Vue {
    @Prop({ required: true }) readonly toolhead!: ViewToolhead
    @Prop({ required: true }) readonly labels!: ViewLabels

    get tool(): ViewToolhead['tool'] {
        return this.toolhead.tool
    }

    /** The hotend icon's frame is highlighted in the loaded filament's color. */
    get style(): Record<string, string> {
        return this.tool?.color ? { '--tool': this.tool.color } : {}
    }

    get iconColor(): string {
        return this.tool?.color ?? '#5c6370'
    }

    /** The loaded filament's own line (the core's resting label), given its
     *  weight in the header: what is in the toolhead is the headline. */
    get loaded(): { label: string; color: string | null; outline: boolean } | null {
        const rest = this.toolhead.activity.rest
        return rest && rest.loaded ? rest : null
    }

    get swatchStyle(): Record<string, string> {
        return this.loaded?.color ? { background: this.loaded.color } : {}
    }

    get hasSteps(): boolean {
        return this.toolhead.activity.steps.length > 0
    }

    /** The slot never sits empty: with a plan it shows the steps, and with no
     *  plan the core's resting path (PRINCIPLES.md 2). Either way the same
     *  rows, so its height is the same. */
    get hasStepsOrRest(): boolean {
        return this.hasSteps || this.toolhead.activity.rest !== null
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

.fps {
    container: fps / inline-size;
    background: var(--oams-surface-1);
    border-radius: var(--oams-radius-card);
    padding: 18px 16px 14px;
}

/* In a host the card is the host's own, so each FPS card gets a hairline to
   read as a card. */
.oams-panel[data-theme='host'] .fps {
    box-shadow: inset 0 0 0 1px var(--oams-line);
}

/* Three zones: icon and titles left, the pressure block centered, the alert
   slot top right (its space is always reserved). */
/* The title, the pressure block, the alert slot. The alert slot is only as
   wide as its icon, so the pressure block sits right of center and the title
   (the toolhead name, its FPS id and the loaded spool's line) gets the room
   (a centered block left the title ~150 px on a ~660 px card). */
.fps-head {
    display: grid;
    grid-template-columns: minmax(0, 1fr) minmax(200px, 320px) auto;
    gap: 20px;
    align-items: center;
    margin: 0 6px 12px;
}

.fps-title {
    grid-column: 1;
}

/* The alert's own column whether or not a pressure reading fills the middle
   one: the three zones stay where they are (principle 2). */
.fps-alert {
    grid-column: 3;
    justify-self: end;
    align-self: start;

    ::v-deep .alert-slot {
        width: 42px;
        height: 34px;
    }
}

.fps-title {
    display: flex;
    align-items: center;
    gap: 14px;
    min-width: 0;
}

.th-icon {
    position: relative;
    width: 56px;
    height: 56px;
    flex: 0 0 56px;
    border-radius: 12px;
    background: var(--oams-surface-3);
    border: 1px solid var(--oams-line);
    display: grid;
    place-items: center;

    svg {
        width: 42px;
        height: 42px;
    }

    &.loaded {
        border: 2px solid var(--tool);
        box-shadow:
            0 0 0 1px color-mix(in srgb, var(--tool) 35%, transparent),
            0 0 14px color-mix(in srgb, var(--tool) 45%, transparent);
    }

    /* a dark filament on the dark surface: a light 1 px outline (the core says when) */
    &.loaded.outline {
        outline: 1px solid var(--oams-text-faint);
        outline-offset: 0;
    }
}

.th-tool {
    @include oams-mono;
    position: absolute;
    left: 50%;
    top: 52%;
    transform: translate(-50%, -50%);
    font-size: 15px;
    font-weight: 700;
    padding: 3px 6px;
    border-radius: 3px;
    background: rgba(0, 0, 0, 0.62);
    color: #fff;
    text-shadow: 0 1px 1px rgba(0, 0, 0, 0.55);
    pointer-events: none;

    &.ink-dark {
        background: rgba(255, 255, 255, 0.82);
        color: #111;
        text-shadow: none;
    }
}

.fps-text {
    min-width: 0;

    h2 {
        font-size: var(--oams-fs-title);
        font-weight: var(--oams-fw-title);
        letter-spacing: -0.02em;
    }

}

/* The title and the demoted id (fps1) share a line. */
.fps-titleline {
    display: flex;
    align-items: baseline;
    gap: 8px;
    min-width: 0;
}

.fps-sub {
    flex: none;
    font-size: 12px;
    color: var(--oams-text-faint);
}

/* The loaded filament: body size, with a swatch. The line keeps its height
   whether or not something is loaded, so the header never jumps. */
.fps-loaded {
    display: flex;
    align-items: center;
    gap: 7px;
    min-height: 22px;
    margin-top: 5px;
    font-size: var(--oams-fs-body);
    line-height: 22px;
    color: var(--oams-text);

    span {
        min-width: 0;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
    }
}

.fps-swatch {
    flex: none;
    width: 12px;
    height: 12px;
    border-radius: 3px;
    background: var(--oams-surface-3);
    box-shadow: inset 0 0 0 1px var(--oams-line);

    &.outline {
        box-shadow: 0 0 0 1px var(--oams-text-faint);
    }
}

/* The status area never changes height: one row for the stepper, one line
   (two on a narrow card) for the message. */
.status {
    margin: 0 6px 12px;
}

.status-steps {
    min-height: 30px;
    container: steps / inline-size;
}

.mmus {
    container: mmus / inline-size;
    display: flex;
    flex-wrap: wrap;
    gap: var(--oams-gap-card-gap);
}

/* The message and the toolhead's actions share a row: Unload and Stop sit
   next to what they act on. A narrow card wraps them under the message. */
.status-row {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 8px 10px;
    margin-top: 8px;

    ::v-deep .message {
        flex: 1 1 220px;
        min-width: 0;
    }
}

.th-actions {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
    flex-wrap: wrap;
    margin-left: auto;
    align-items: center;
}

/* Stop is identifiable before it is urgent: a quiet error-toned outline at
   rest (fainter still when dimmed), firmer when it can be pressed. Unload
   stays neutral. */
.th-actions ::v-deep .btn[data-action='stop'] {
    color: color-mix(in srgb, var(--oams-error) 75%, var(--oams-text));
    border-color: color-mix(in srgb, var(--oams-error) 38%, transparent);

    &[data-enabled='true'] {
        color: var(--oams-error);
        border-color: var(--oams-error);
        background: color-mix(in srgb, var(--oams-error) 10%, var(--oams-surface-2));
    }
}

@container steps (max-width: 900px) {
    ::v-deep .step:not(.rest) span {
        display: none;
    }

    /* The resting path keeps its one label at every width (it is the only text
     * there is), and the narrow-width current-step row has nothing to name. */
    ::v-deep .stepper-wrap:not(.atrest) .step-name {
        display: block;
    }

    ::v-deep .stepper {
        gap: 3px;
    }
}

@container fps (max-width: 640px) {
    .fps-head {
        grid-template-columns: minmax(0, 1fr) auto;
        grid-template-areas:
            'title alert'
            'pressure pressure';
        gap: 12px 14px;
    }

    .fps-title {
        grid-area: title;
    }

    .fps-alert {
        grid-area: alert;
    }

    .pressure {
        grid-area: pressure;
    }

    .fps-text h2 {
        font-size: 18px;
    }

    ::v-deep .message {
        min-height: 44px;

        span {
            -webkit-line-clamp: 2;
        }
    }
}

@container panel (max-width: 440px) {
    .th-actions {
        flex: 1 1 100%;
    }

    .fps {
        padding: 12px 8px 8px;
        border-radius: 18px;
    }

    .fps-head {
        margin: 0 4px 12px;
    }
}
</style>
