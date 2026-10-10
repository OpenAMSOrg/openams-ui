<template>
    <svg v-if="tone === 'error'" class="tone-icon" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor"
        stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
        <path d="M8 2h8l6 6v8l-6 6H8l-6-6V8z" />
        <path d="M12 7.5v5.5M12 16.6v.1" />
    </svg>
    <svg v-else-if="tone === 'info'" class="tone-icon" width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor"
        stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
        <path d="M12 3 2 20h20L12 3z" />
        <path d="M12 10v4M12 17.2v.1" />
    </svg>
</template>

<script lang="ts">
import { Component, Prop, Vue } from 'vue-property-decorator'
import type { Tone } from '@logic'

/** The severity icon of a tone (principle 6: never color alone): an octagon
 *  with "!" for an error, a triangle with "!" for information, none for the
 *  neutral tone. */
@Component
export default class ToneIcon extends Vue {
    @Prop({ required: true }) readonly tone!: Tone
}
</script>
