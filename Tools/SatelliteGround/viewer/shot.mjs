// Headless render smoke-test: load the viewer, screenshot, report console errors.
import { chromium } from 'playwright-core';

const EXE = '/opt/pw-browsers/chromium-1194/chrome-linux/chrome';
const URL = 'http://127.0.0.1:8765/viewer/index.html';
const OUT = process.argv[2] || '/tmp/viewer_shot.png';
const MODE = process.argv[3] !== undefined ? parseInt(process.argv[3], 10) : 1;
const RELIEF = process.argv[4] !== undefined ? parseInt(process.argv[4], 10) : 6;

const browser = await chromium.launch({
  executablePath: EXE,
  headless: true,
  args: ['--use-gl=angle', '--use-angle=swiftshader',
         '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
         '--no-sandbox', '--disable-dev-shm-usage'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
const errors = [];
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', (e) => errors.push('PAGEERROR ' + e.message));

await page.goto(URL, { waitUntil: 'networkidle' });
await page.waitForTimeout(3000);
// Select render mode + relief, then let it settle.
await page.click(`#modes button[data-mode="${MODE}"]`).catch(() => {});
await page.evaluate((r) => {
  const s = document.getElementById('relief');
  s.value = String(r); s.dispatchEvent(new Event('input'));
}, RELIEF);
await page.waitForTimeout(800);

const errText = await page.$eval('#err', (e) => e.textContent).catch(() => '');
const stats = await page.$eval('#stats', (e) => e.textContent).catch(() => '');
// Is the canvas actually drawing (not uniform)? sample via toDataURL size proxy.
const canvasInfo = await page.evaluate(() => {
  const c = document.querySelector('canvas');
  if (!c) return { ok: false };
  const gl = c.getContext('webgl2') || c.getContext('webgl');
  return { ok: !!gl, w: c.width, h: c.height,
           renderer: gl ? gl.getParameter(gl.VERSION) : 'none' };
});
await page.screenshot({ path: OUT });
await browser.close();

console.log('STATS:', stats);
console.log('ERR#:', errText || '(none)');
console.log('CANVAS:', JSON.stringify(canvasInfo));
console.log('CONSOLE_ERRORS:', errors.length ? errors.slice(0, 8).join(' | ') : '(none)');
console.log('SHOT:', OUT);
