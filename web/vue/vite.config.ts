import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue2';
import { resolve } from 'node:path';

// Library build: dist/openams-panel.js exports the panel components, so the
// generator can copy them into a Mainsail fork. The tokens (src/tokens.css) and
// the components' scoped styles ship as dist/openams-panel.css. The gallery is
// a dev page served by vite (see vite.gallery.config.ts for its build).
export default defineConfig({
  plugins: [vue()],
  resolve: { alias: { '@logic': resolve(__dirname, '../logic/index.ts') } },
  server: { fs: { allow: [resolve(__dirname, '../..')] } },
  build: {
    target: 'es2022',
    lib: {
      entry: resolve(__dirname, 'src/index.ts'),
      formats: ['es'],
      fileName: () => 'openams-panel.js',
    },
  },
});
