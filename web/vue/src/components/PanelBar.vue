<template>
    <div class="panel-bar">
        <button type="button" class="icon-btn" :aria-label="view.labels.alerts">
            <oams-icon :path="mdiBell" /><span v-if="view.unread_count > 0" class="badge">{{ view.unread_count }}</span>
        </button>
        <button v-if="view.settings.length" type="button" class="icon-btn" :aria-label="view.labels.settings"
            :popovertarget="anchor" data-popover-invoker="settings">
            <oams-icon :path="mdiCog" />
        </button>
        <settings-popover :anchor-id="anchor" name="settings" :heading="view.labels.settings" :view="view"
            @language="onLanguage" />
    </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import { mdiBell, mdiCog } from '@mdi/js'
import type { View } from '@logic'
import OamsIcon from './OamsIcon.vue'
import SettingsPopover from './SettingsPopover.vue'
import { newAnchor } from '../popover'

/** The panel-level bar: the bell and the settings, for the standalone page
 *  (in a host the host's own bar shows them). The settings button opens the
 *  settings popover, which holds the five switches and the language choice. */
@Component({ components: { OamsIcon, SettingsPopover } })
export default class PanelBar extends Vue {
    @Prop({ required: true }) readonly view!: View

    /** One id per bar, so a page of twelve panels has twelve settings buttons
     *  and each opens its own popover (see popover.ts's newAnchor). */
    readonly anchor = newAnchor()

    mdiBell = mdiBell
    mdiCog = mdiCog

    /** A language was picked: the panel asks the core to switch and redraws. */
    onLanguage(code: string): void {
        this.$emit('language', code)
    }
}
</script>

<style lang="scss" scoped>
@use '../tokens' as *;

.panel-bar {
    display: flex;
    justify-content: flex-end;
    gap: 4px;
    padding: 0 0 8px;
}

.icon-btn {
    @include oams-focus;
    position: relative;
    /* The host reset (Vuetify) zeroes button padding; the reference keeps the
       browser's own 1px 6px, which the fixed 36x36 box then centers against. */
    padding: 1px 6px;
    width: 36px;
    height: 36px;
    border: 0;
    border-radius: 10px;
    background: transparent;
    color: var(--oams-text-muted);
    display: grid;
    place-items: center;
    cursor: pointer;

    &:hover {
        background: var(--oams-surface-3);
    }

    .badge {
        position: absolute;
        top: 2px;
        right: 0;
        min-width: 16px;
        height: 16px;
        padding: 0 4px;
        border-radius: 8px;
        background: var(--oams-error);
        color: #fff;
        font-size: 10px;
        font-weight: 700;
        line-height: 16px;
        text-align: center;
    }
}
</style>
