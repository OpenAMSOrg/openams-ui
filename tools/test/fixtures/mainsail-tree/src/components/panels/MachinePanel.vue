<template>
    <panel :title="$t('Panels.MachinePanel.Headline')" :collapsible="true" card-class="machine-panel">
        <v-card-text>
            <span>{{ state }}</span>
        </v-card-text>
    </panel>
</template>

<script lang="ts">
import { Component, Vue } from 'vue-property-decorator'

@Component
export default class MachinePanel extends Vue {
    get state() {
        return this.$store.state.machine?.state ?? 'ready'
    }
}
</script>
