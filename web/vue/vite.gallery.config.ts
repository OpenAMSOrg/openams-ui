import { defineConfig } from 'vite';
import vue from '@vitejs/plugin-vue2';
import { resolve } from 'node:path';

// The gallery build: the review page (gallery.html), the one Playwright drives.
export default defineConfig({
  plugins: [vue()],
  resolve: { alias: { '@logic': resolve(__dirname, '../logic/index.ts') } },
  server: { fs: { allow: [resolve(__dirname, '../..')] } },
  build: {
    target: 'es2022',
    outDir: 'dist-gallery',
    rollupOptions: { input: resolve(__dirname, 'gallery.html') },
  },
});
