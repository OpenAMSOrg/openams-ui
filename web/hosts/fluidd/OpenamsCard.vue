<template>
  <collapsable-card
    :title="$t('app.openams.Headline')"
    :icon="icon"
    draggable
    layout-path="dashboard.openams-card"
  >
    <v-card-text class="pt-1">
      <openams-view
        :logic="adapter.logic"
        :language="language"
        theme="host"
        @request="onRequest"
      />
    </v-card-text>
  </collapsable-card>
</template>

<script lang="ts">
import { Component, Mixins, Watch } from 'vue-property-decorator'
import { mdiViewGrid } from '@mdi/js'
import { consola } from 'consola'
import StateMixin from '@/mixins/state'
import { EventBus } from '@/eventBus'
import getFilePaths from '@/util/get-file-paths'
import { OpenamsAdapter, OPENAMS_SPOOLMAN_STATUS, panelLanguage } from './adapter'
import type { OpenamsStoreState } from './adapter'
import OpenamsView from '../../vue/src/components/OpenamsView.vue'
import type { ActionResult } from '../../logic'

/** The socket for a method Fluidd's Moonraker.Methods map does not name, which
 *  is every method the OpenAMS component adds: SocketActions only wraps the
 *  ones Fluidd itself calls. */
type RawSocket = {
  emit: (method: string, options: { params?: Record<string, unknown> }) => Promise<unknown>
}

/** The words of a refused call: a rejected socket call carries the JSON-RPC
 *  error, `{ code, message }`, or an Error. */
function errorMessage (error: unknown): string {
  if (typeof error === 'object' && error !== null && 'message' in error) return String((error as { message: unknown }).message)

  return String(error)
}

@Component({ components: { OpenamsView } })
export default class OpenamsCard extends Mixins(StateMixin) {
  icon: string = mdiViewGrid

  adapter = new OpenamsAdapter()

  private onSpoolmanStatus: ((payload: unknown) => void) | null = null

  mounted () {
    this.onSpoolmanStatus = (payload: unknown) => this.adapter.applyComponentStatus(payload)
    EventBus.bus.$on(OPENAMS_SPOOLMAN_STATUS, this.onSpoolmanStatus)

    this.feed()
    this.requestComponentStatus()
  }

  beforeDestroy () {
    if (this.onSpoolmanStatus) EventBus.bus.$off(OPENAMS_SPOOLMAN_STATUS, this.onSpoolmanStatus)
  }

  /** The language the panel is drawn in, which is the page's: inside Fluidd
   *  the language is the user's choice in Fluidd, not the panel's (principle
   *  1, we are a guest), so the panel has no picker of its own and reads the
   *  host's locale.
   *
   *  A getter, not a field read once: that makes it a computed property, and
   *  vue-i18n keeps `locale` in a reactive `_vm` of its own, so picking a
   *  language in Fluidd re-reads this and hands the view a new one. */
  get language (): string {
    return panelLanguage(this.$i18n.locale)
  }

  /** Only the slices the logic reads, so the watcher is not woken by the rest
   *  of the (constantly changing) printer and spoolman state. */
  get openamsStoreState (): OpenamsStoreState {
    return {
      printer: {
        oams_manager: this.$typedState.printer.printer.oams_manager,
        print_stats: this.$typedState.printer.printer.print_stats,
        toolhead: this.$typedState.printer.printer.toolhead,
        ...this.openamsUiObjects
      },
      metadata: this.currentFileMetadata,
      spools: this.$typedState.spoolman.spools
    }
  }

  get openamsUiObjects (): Record<string, unknown> {
    const printer: Record<string, unknown> = this.$typedState.printer.printer
    const objects: Record<string, unknown> = {}

    for (const key of Object.keys(printer)) {
      if (key.startsWith('openams_ui ')) objects[key] = printer[key]
    }

    return objects
  }

  /** The running job's file metadata, which the files store holds: the
   *  printer objects do not carry a current_file, and Fluidd asks Moonraker
   *  for the metadata of a file when print_stats names a new one. */
  get currentFileMetadata (): unknown {
    const pathFilename = this.$typedState.printer.printer.print_stats?.filename

    if (!pathFilename) return undefined

    const { rootPath, filename } = getFilePaths(pathFilename, 'gcodes')

    return this.$typedGetters['files/getFile'](rootPath, filename)
  }

  @Watch('openamsStoreState', { deep: true })
  onStoreChange () {
    this.feed()
  }

  feed () {
    this.adapter.feedStore(this.openamsStoreState)
  }

  /** What the user chose in the panel, sent the way Fluidd sends its own:
   *  G-code through the console script call (so it shows in the console), an
   *  RPC by its method, a refusal as Fluidd's error notification. A `local`
   *  result is display-only and goes nowhere. The spool edit, the spool
   *  confirmation, Stop and a G-code script are the calls waited on: a refusal
   *  of each goes back to the panel with `done`, in the printer's own words. */
  onRequest (result: ActionResult, done?: (message?: string) => void) {
    switch (result.kind) {
      case 'gcode':
        // Waited on, like the spool edit: the printer answers a script only
        // once it has run, and a refusal ("Heat the extruder before
        // unloading") comes back in its own words. It is still the console's
        // own call, so the script shows there.
        this.rawSocket().emit('printer.gcode.script', { params: { script: result.script } }).then(
          () => done?.(),
          (error: unknown) => done?.(errorMessage(error))
        )
        break
      case 'rpc':
        if (result.method === 'server.openams_spoolman.edit') {
          // The spool edit is waited on: the printer can refuse it in words of
          // its own ("Spoolman is offline"), which go back to the panel with
          // `done`; the vendor list is read again after it (the host may have
          // added one).
          this.rawSocket().emit(result.method, { params: result.params }).then(
            () => {
              done?.()
              this.requestVendors()
            },
            (error: unknown) => done?.(errorMessage(error))
          )
        } else if (result.method === 'server.openams.stop' || result.method === 'server.openams_spoolman.confirm') {
          // Waited on, so the Stop button is released when the printer answers
          // ("stopping", or "nothing running": both end the wait) or refuses
          // (an older component has no such method), and a spool confirmation
          // the host turns down ("no bay is awaiting confirmation") says so in
          // the host's words.
          this.rawSocket().emit(result.method, { params: result.params }).then(
            () => done?.(),
            (error: unknown) => done?.(errorMessage(error))
          )
        } else {
          this.rawSocket().emit(result.method, { params: result.params }).catch(() => {})
        }
        break
      case 'error':
        EventBus.$emit(result.reason, { type: 'error' })
        break
      default:
        break
    }
  }

  async requestComponentStatus () {
    // The status call is not one of the typed Moonraker RPC methods, so this
    // is the one place the card reaches past that list.
    try {
      const status = await this.rawSocket().emit('server.openams_spoolman.status', {})

      this.adapter.applyComponentStatus(status)
      if ((status as { edit?: unknown } | null)?.edit === true) this.requestVendors()
    } catch (e) {
      consola.warn('[OpenAMS]: server.openams_spoolman.status failed', e)
    }
  }

  /** The names the spool editor offers as vendors: Spoolman's own list, read
   *  through the proxy. A host whose component can edit has Spoolman, so this
   *  only runs then. */
  async requestVendors () {
    try {
      this.adapter.applyVendorList(
        await this.rawSocket().emit('server.spoolman.proxy', { params: { request_method: 'GET', path: '/v1/vendor' } })
      )
    } catch (e) {
      consola.warn('[OpenAMS]: the Spoolman vendor list failed', e)
    }
  }

  private rawSocket (): RawSocket {
    return this.$socket as unknown as RawSocket
  }
}
</script>
