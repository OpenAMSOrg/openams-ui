<template>
    <svg class="rfid" viewBox="0 0 16 16" aria-hidden="true">
        <path fill="none" stroke="currentColor" stroke-width="1.5" d="M2 2h12v12H2V4.5h9.5V13H4.5V7h5v4" />
    </svg>
</template>

<script lang="ts">
import { Component, Vue } from 'vue-property-decorator'

/** The mark of a spool read from its RFID tag. */
@Component
export default class RfidIcon extends Vue {}
</script>
