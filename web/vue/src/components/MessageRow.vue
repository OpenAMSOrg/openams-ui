<template>
    <div class="message" :class="`tone-${message.tone}`" :title="message.text" role="status">
        <tone-icon :tone="message.tone" /><span>{{ message.text }}</span>
    </div>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { Tone } from '@logic'
import ToneIcon from './ToneIcon.vue'

/** The one status line under the stepper (docs/design/UNIFIED_UI.md 4a): an
 *  error, a runout or other information, a notice or the plain status. The
 *  core merges them and writes the text. */
@Component({ components: { ToneIcon } })
export default class MessageRow extends Vue {
    @Prop({ required: true }) readonly message!: { text: string; tone: Tone }
}
</script>

<style lang="scss" scoped>
.message {
    display: flex;
    align-items: center;
    gap: 8px;
    min-height: 34px;
    padding: 0 12px;
    border-radius: 10px;
    font-size: 13px;
    color: var(--oams-text-muted);

    span {
        display: -webkit-box;
        -webkit-box-orient: vertical;
        -webkit-line-clamp: 1;
        overflow: hidden;
    }

    &.tone-error {
        background: color-mix(in srgb, var(--oams-error) 14%, transparent);
        color: var(--oams-error);
    }

    &.tone-info {
        background: color-mix(in srgb, var(--oams-warn) 16%, transparent);
        color: var(--oams-warn);
    }
}
</style>
