// The gallery page (gallery.html + this file): a fixture player over the real
// logic and a state sheet of synthetic states, both drawn by the panel.
import Vue from 'vue';
import Vuetify from 'vuetify';
import { Touch, Ripple } from 'vuetify/lib/directives';
import 'vuetify/dist/vuetify.min.css';
import GalleryApp from './GalleryApp.vue';
import '../src/tokens.css';
import tokens from '../../../design/tokens.json';

Vue.use(Vuetify, { directives: { Touch, Ripple } });

// The host theme (docs/design/UNIFIED_UI.md 6): inside a v-app the card, the
// text and the font are the host's, and the panel reads Vuetify's --v-*
// variables. Like Mainsail's own two themes, the dark card gets the status
// colors of design/tokens.json's dark set and the light card the light set, so
// a host card matches the approved screenshots and meets the contrast floor.
const palette = (set: typeof tokens.color.dark) => ({
  primary: set.accent,
  info: set.info,
  success: set.ok,
  warning: set.warn,
  error: set.error,
});

new Vue({
  vuetify: new Vuetify({
    theme: {
      dark: true,
      options: { customProperties: true, variations: false },
      themes: { light: palette(tokens.color.light), dark: palette(tokens.color.dark) },
    },
    icons: { iconfont: 'mdiSvg' },
  }),
  render: (h) => h(GalleryApp),
}).$mount('#app');
