// Builds a self-contained offline replay page from the current frontend and
// fresh solver runs. Usage: node tools/build-replay.mjs <out.html> [dataDir]
// Every response in the page is a real C solver output; custom inputs that
// were not recorded return an explanatory error instead of fake numbers.
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, join } from 'node:path';
import { calculate, loadPresets, normalize } from '../engine.mjs';

const [out, dataDir] = process.argv.slice(2);
if (!out) throw new Error('usage: node tools/build-replay.mjs <out.html> [dataDir]');
const root = new URL('../public/', import.meta.url);
const read = name => readFile(new URL(name, root), 'utf8');
const catalog = await loadPresets();

// Mirror readInput() in app.mjs so request keys match what the page sends.
function formInput(preset, overrides = {}) {
  return {
    kind: 'liquid', preset: preset.id, oxidizer: preset.oxidizer, fuel: preset.fuel,
    tox: preset.tox, tfuel: preset.tfuel, of: preset.of, pc: (preset.pc / 1e6) * 1e6,
    pa: (preset.pa / 1000) * 1000, mdot: preset.mdot, exitMode: 'eps', exitValue: preset.eps * 1,
    model: 'eq', throat: 'sonic', points: 41, heat: 0, heatMax: 0, solid: 'JPN', tin: 298.15,
    ...overrides,
  };
}

const plan = [];
for (const id of ['TQ-12B', 'TQ-15B', 'YF-100K']) {
  const preset = catalog.presets.find(item => item.id === id);
  plan.push({ name: `${id}-单工况`, input: formInput(preset), range: false });
  plan.push({ name: `${id}-区间`, input: formInput(preset), range: true });
}
const tq12 = catalog.presets.find(item => item.id === 'TQ-12B');
plan.push({ name: 'TQ-12B-未知流量', input: formInput(tq12, { mdot: 0 }), range: false });
plan.push({ name: 'TQ-12B-未知流量-区间', input: formInput(tq12, { mdot: 0 }), range: true });
plan.push({ name: 'TQ-12B-热损失敏感性', input: formInput(tq12, { heatMax: 300 }), range: true });

const snapshots = [];
for (const item of plan) {
  process.stdout.write(`solving ${item.name}… `);
  const value = await calculate(normalize(item.input, catalog.presets), item.range, catalog.presets);
  snapshots.push({ input: item.input, range: item.range, value });
  if (dataDir) {
    await mkdir(dataDir, { recursive: true });
    await writeFile(join(dataDir, `${item.name}.json`), JSON.stringify(value, null, 1));
  }
  console.log('ok');
}

const [html, css, icon, math, losses, charts, app] = await Promise.all(
  ['index.html', 'style.css', 'icon.svg', 'math.mjs', 'losses.mjs', 'charts.mjs', 'app.mjs'].map(read));
const shim = `(function replay(catalog, snapshots) {
  function requestKey(input, range) {
    const common = ['kind','pc','pa','mdot','exitMode','exitValue','model','throat','points'];
    const specific = input.kind === 'solid' ? ['solid','tin'] : ['preset','oxidizer','fuel','tox','tfuel','of','heat','heatMax'];
    return JSON.stringify([range, ...common.concat(specific).map(field => input[field])]);
  }
  const responses = new Map(snapshots.map(row => [requestKey(row.input, row.range), row.value]));
  window.fetch = async (url, options) => {
    if (url === '/api/presets') return Response.json(catalog);
    if (url === '/api/calculate') {
      const { input, range } = JSON.parse(options.body);
      const sample = responses.get(requestKey(input, range));
      if (sample) return Response.json(sample);
      return Response.json({ error: '离线回放只收录了三组课设预设的默认工况、TQ-12B 流量为 0 以及 TQ-12B 热损失上限 300 kJ/kg 的实算结果。其他输入请启动源工程实时计算。' }, { status: 422 });
    }
    return Response.json({ error: '离线回放未包含此资源。' }, { status: 404 });
  };
})(${JSON.stringify(catalog)}, ${JSON.stringify(snapshots)});`;
const parts = { math, losses, charts, app };
const loader = `
    const parts = ${JSON.stringify(parts).replaceAll('</', '<\\/')};
    ${shim.replaceAll('</', '<\\/')}
    const url = source => URL.createObjectURL(new Blob([source], { type: 'text/javascript' }));
    const math = url(parts.math);
    const losses = url(parts.losses);
    const charts = url(parts.charts.replaceAll('./math.mjs', math));
    await import(url(parts.app.replaceAll('./math.mjs', math).replaceAll('./losses.mjs', losses).replaceAll('./charts.mjs', charts)));`;
const iconUri = `data:image/svg+xml,${encodeURIComponent(icon)}`;
// Inline the formula fonts so the single-file replay renders the same.
const fontNames = ['stix-two-text.woff2', 'stix-two-text-italic.woff2', 'stix-two-math-ops.woff2'];
let pageCss = css;
for (const name of fontNames) {
  const data = await readFile(new URL(`fonts/${name}`, root));
  pageCss = pageCss.replaceAll(`url("fonts/${name}")`, `url("data:font/woff2;base64,${data.toString('base64')}")`);
}
// Function replacers: the inserted code contains "$" sequences that a
// string replacement would interpret as substitution patterns.
const replayNote = '.replay-note{margin:0;padding:7px 16px;background:#14161b;color:#c9cfda;text-align:center;font-size:12.5px;letter-spacing:.02em;border-bottom:1px solid rgba(240,136,62,.35)}.replay-note::before{content:"";display:inline-block;width:6px;height:6px;border-radius:50%;background:#f0883e;margin-right:8px;vertical-align:middle}';
const page = html
  .replace('<link rel="icon" href="/icon.svg">', () => `<link rel="icon" href="${iconUri}">`)
  .replace('<link rel="stylesheet" href="/style.css">', () => `<style>${pageCss}\n${replayNote}</style>`)
  .replace('<script type="module" src="/app.mjs"></script>', () => `<script type="module">${loader}\n  </script>`)
  .replaceAll('src="/icon.svg"', () => `src="${iconUri}"`)
  .replace('<body>', () => '<body>\n<p class="replay-note">离线回放 · 界面与数据均为当前版本的真实 C 求解器输出快照 · 自定义工况请启动源工程</p>');
await mkdir(dirname(out), { recursive: true });
await writeFile(out, page);
console.log(`wrote ${out} (${(page.length / 1024).toFixed(0)} KB, ${snapshots.length} snapshots)`);
