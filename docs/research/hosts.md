# Hosting `<openams-panel>` in Mainsail and Fluidd

Checked on 2026-09-29 against the upstream source: Mainsail 2.19.0 and Fluidd 1.37.6. Paths are relative to each repository. Only the claims below were verified.

## Bottom line

1. **Neither app loads user JavaScript.**
   - Mainsail loads `.theme/custom.css`, images and `navi.json` (`src/store/files/getters.ts:172-212`, `src/App.vue:192-204`).
   - Fluidd loads `.fluidd-theme/custom.css` and images (`src/App.vue:287-312`).
   - Neither has a plugin API (static Vite bundles; `src/plugins/*` are internal). There is no way to inject our panel without a code change.
2. **Both are Vue 2.7 + Vuetify 2.7.2**, not Vuetify 3 (Mainsail `package.json:61,71`; Fluidd's lock file).
   - Both set `customProperties: true` (Mainsail `src/plugins/vuetify.ts:12`; Fluidd `src/plugins/vuetify.ts:50-52`), so the theme is exposed as `--v-<key>-base` variables. That is what our host theme reads (design §6).
   - Fluidd's keys are `primary`, `primary-offset`, `secondary`, `card-heading`, `btncolor`, `drawer`, `appbar` and `logo`.
   - The exact emitted names were not checked in a browser.
3. **No-fork routes:**
   - **An iframe "webcam".** A Moonraker `[webcam]` with `service: iframe` renders a same-origin page on the dashboard in both apps, with no `sandbox` attribute (Mainsail `src/components/webcams/streamers/HtmlIframe.vue:3`; Fluidd `src/components/widgets/camera/services/IframeCamera.vue:2-10`). Our module and custom element run inside it.
     - It needs its own Moonraker websocket, since neither app shares its socket (Mainsail `src/plugins/webSocketClient.ts:100`; Fluidd `src/plugins/socketClient.ts:81`).
     - It lives in the webcam panel with a fixed aspect ratio (`HtmlIframe.vue:30`), while our height depends on the number of toolheads.
   - **A sidebar link (Mainsail only).** `.theme/navi.json` adds a link to a full page we serve (`src/components/mixins/navigation.ts:125-150`). The page is a standalone panel with its own socket. Fluidd has no equivalent.
4. **Native integration takes an upstream pull request** of a Vue 2 component. There is precedent in both apps: the Happy Hare and AFC panels were merged upstream (Mainsail `CHANGELOG.md` #2158, #2231, #2273, #2313, #2431; Fluidd #1789, #1769, #1907, #1900). They are shown only when their Klipper object exists (Mainsail #2313). Maintainers expect Vue, i18n and a passing lint (Mainsail `agent_docs/CODE_REVIEW.md:20-33`).

## Unverified (check before relying on it)
- **Serving files from Moonraker's config root.** Whether Moonraker serves our HTML, JS and wasm with usable MIME types is unchecked. Our wasm loader should fall back from `instantiateStreaming` to an `ArrayBuffer`, so the `application/wasm` MIME type is never required.
- **Theme in an iframe.** A same-origin iframe can read the parent's computed `--v-*` variables and font. This is standard DOM access, but it has not been tried with these apps.
- **Authentication.** With Moonraker authorization on, an iframe or standalone page needs its own token (Fluidd fetches theme files with one: `createFileUrlWithToken`).

## Consequences for the plan
- The panel needs a **standalone mode with its own Moonraker connection**: websocket, `printer.objects.subscribe` and `printer.gcode.script`. The no-fork routes need it, and so does any page outside a host.
- The **thin host wrapper** of design §8 feeds the core from the host's own store. It is what an upstream pull request would contain.
