// Screenshots for the look review: the fixture player (two_lanes) and the state
// sheet, each at 1200, 800, 420 and 360 px, dark and host-on-light. The same
// states and file names of the retired Lit panel's tool.
// Needs the gallery: this script starts vite on a free port itself.
//   npm run shoot   ->  web/vue/review/*.png (git-ignored)
import { chromium } from '@playwright/test';
import { createServer } from 'vite';
import { mkdirSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const out = resolve(root, 'review');
mkdirSync(out, { recursive: true });

const server = await createServer({ root, configFile: resolve(root, 'vite.config.ts'), server: { port: 0, host: '127.0.0.1' }, logLevel: 'error' });
await server.listen();
const base = `http://127.0.0.1:${server.config.server.port ?? server.httpServer.address().port}`;

const shots = [
  { name: 'two_lanes', q: 'mode=player&fixture=two_lanes' },
  { name: 'states', q: 'mode=sheet' },
];
const themes = [{ name: 'dark', q: 'dark' }, { name: 'host-light', q: 'host-white' }];
const widths = [1200, 800, 420, 360];

const browser = await chromium.launch();
try {
  for (const s of shots) for (const t of themes) for (const w of widths) {
    const page = await browser.newPage({ viewport: { width: w + 32, height: 900 } });
    page.on('pageerror', (e) => console.error('pageerror', e.message));
    await page.goto(`${base}/gallery.html?${s.q}&theme=${t.q}&width=${w}`);
    await page.waitForSelector('#stage[data-ready="true"]', { timeout: 20000 });
    await page.addStyleTag({ content: '#controls{display:none!important}' });
    // Animations off: the stepper's current step pulses, and a screenshot
    // would otherwise catch it at a different moment on every run.
    const shot = { animations: 'disabled' };
    const file = resolve(out, `${s.name}-${t.name}-${w}.png`);
    await page.locator('#stage').screenshot({ ...shot, path: file });
    console.log(file);
    if (s.name === 'states' && (w === 1200 || w === 360)) {
      const tiles = resolve(out, `states-tiles-${t.name}-${w}.png`);
      await page.locator('.card[data-key="tiles"]').screenshot({ ...shot, path: tiles });
      console.log(tiles);
    }
    if (s.name === 'states' && w === 1200) {
      // the first sheet (the two FPS cards), for a quick review
      const quick = resolve(out, `states-fps-${t.name}-1200.png`);
      await page.locator('.card').first().screenshot({ ...shot, path: quick });
      console.log(quick);
      // a toolhead with a dimmed Unload next to an enabled Stop (the loading scenario)
      const actions = resolve(out, `states-actions-${t.name}-1200.png`);
      await page.locator('.card[data-key="loading"]').screenshot({ ...shot, path: actions });
      console.log(actions);
      // an AMS 2 Pro beside an AMS HT that is drying
      const ht = resolve(out, `states-ht-${t.name}-1200.png`);
      await page.locator('.card[data-key="ht"]').screenshot({ ...shot, path: ht });
      console.log(ht);
    }
    await page.close();
  }

  // The interaction shots: the approved look with a popover, a form dialog and
  // a confirmation open, so the review can see what a press looks like. The
  // whole page, not the stage, because a popover and a dialog are in the top
  // layer and may sit outside the card.
  const interact = [
    {
      name: 'interact-popover',
      run: async (page) => {
        await page.locator('.card[data-key="tile-states"] .lane[data-slot="oams11"] [data-target="ring"]')
          .click({ position: { x: 6, y: 20 } });
        await page.waitForSelector('[data-popover="ring"]:popover-open');
      },
    },
    {
      name: 'interact-form-dialog',
      run: async (page) => {
        await page.locator('.card[data-key="tile-states"] [data-popover-invoker="unit"]').first().click();
        await page.locator('[data-popover="unit"]:popover-open [data-action="dry_start"]').click();
        await page.waitForSelector('.oams-panel dialog[open]');
      },
    },
    {
      name: 'interact-confirm-dialog',
      run: async (page) => {
        await page.locator('.card[data-key="tile-states"] .lane[data-slot="oams11"] [data-target="ring"]')
          .click({ position: { x: 6, y: 20 } });
        await page.locator('[data-popover="ring"]:popover-open [data-action="calibrate"]').click();
        await page.waitForSelector('.oams-panel dialog[open]');
      },
    },
  ];
  for (const i of interact) {
    const page = await browser.newPage({ viewport: { width: 1232, height: 900 } });
    page.on('pageerror', (e) => console.error('pageerror', e.message));
    await page.goto(`${base}/gallery.html?mode=sheet&theme=dark&width=1200`);
    await page.waitForSelector('#stage[data-ready="true"]', { timeout: 20000 });
    await page.addStyleTag({ content: '#controls{display:none!important}' });
    await i.run(page);
    const file = resolve(out, `${i.name}.png`);
    await page.screenshot({ animations: 'disabled', path: file });
    console.log(file);
    await page.close();
  }
} finally {
  await browser.close();
  await server.close();
}
