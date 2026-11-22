import { caption, colors, extent, format, heatmap, linearChart, nozzleGeometry, nozzleMap, nozzleSurface, surface, theme } from './charts.mjs';
import { mathHtml as m, mathText, numericSource, richHtml, unitSource } from './math.mjs';
import { defaultLosses, divergence, envelope, envelopeMetrics, judgePublic, lossFactors, validLosses } from './losses.mjs';
import { createPlume } from './plume.mjs';

const $ = selector => document.querySelector(selector);
const form = $('#case-form');
const panel = $('#result-panel');
const stageLabels = { scan: '搜索温区', brent: 'Brent 求根', final: '最终复算' };
const stageColor = { scan: 0, brent: 2, final: 1 };
const modelColors = new Proxy({}, { get: (_, stage) => colors[stageColor[stage]] });
const profileMetrics = [
  { id: 'T_K', label: '温度', symbol: 'T', unit: 'K', read: p => p.T_K },
  { id: 'p_Pa', label: '绝对压强', symbol: 'p', unit: 'MPa', log: true, read: p => p.p_Pa / 1e6 },
  { id: 'Ma', label: '马赫数', symbol: 'Ma', unit: '1', read: p => p.Ma },
  { id: 'u_m_s', label: '流速', symbol: 'u', unit: 'm/s', read: p => p.u_m_s },
  { id: 'rho_kg_m3', label: '密度', symbol: 'ρ', unit: 'kg/m³', log: true, read: p => p.rho_kg_m3 },
  { id: 'gamma', label: '比热比', symbol: 'γ', unit: '1', read: p => p.gamma },
];
const exitLabels = {
  eps: ['出口面积比', 'A_{e}/A_{t}', 1, 1],
  pe: ['出口压强', 'kPa', 1000, .001],
  pr: ['出口／室压', 'p_{e}/p_{c}', 1, 0.00000001],
  mach: ['出口马赫数', 'Ma', 1, 1],
};
const resultKeys = [
  ['temperature', '燃烧室温度', 'K', 1], ['cstar', '特征速度', 'm/s', 1],
  ['isp', '给定环境比冲', 's', 2], ['ispVac', '真空比冲', 's', 2],
  ['thrust', '给定环境推力', 'kN', 2], ['thrustVac', '真空推力', 'kN', 2],
  ['exitPressure', '出口绝对压强', 'kPa', 2], ['exitMach', '出口马赫数', '1', 3],
];
const resultSymbols = {temperature:'T_{c}',cstar:'c^{∗}',isp:'I_{sp}',ispVac:'I_{sp}',
  thrust:'F',thrustVac:'F',exitPressure:'p_{e}',exitMach:'Ma_{e}'};
const initialCamera = () => ({angle:-33,tilt:29});
const state = {
  catalog: null, record: null, busy: false, stale: false, tab: 'overview',
  sample: 0, axis: 'pressure', overlay: false, variable: 'T_K',
  fieldModel: 'eq', scene:'nozzle', heatFocus: {row: 0, index: 0}, nozzleFocus: 0, camera: initialCamera(),
  comparisons: [], redraw: null, fieldMode: 'value',
  losses: loadLosses(), engineLevel: true,
};

function loadLosses() {
  try {
    const saved = JSON.parse(localStorage.getItem('rocket-workbench-losses-v2') ?? 'null');
    if (validLosses(saved)) return saved;
  } catch { /* fall back to defaults */ }
  return defaultLosses();
}
function saveLosses() {
  try { localStorage.setItem('rocket-workbench-losses-v2', JSON.stringify(state.losses)); } catch { /* memory only */ }
}
// Significant-figure display for tables (raw values stay in JSON/CSV).
function sig(value, digits = 5) {
  if (!Number.isFinite(value)) return '—';
  if (value === 0) return m('0', true);
  const abs = Math.abs(value);
  if (abs < 1e-3 || abs >= 1e7) return precise(value);
  const decimals = Math.max(0, digits - 1 - Math.floor(Math.log10(abs)));
  return m(numericSource(Number(value.toFixed(decimals)), decimals), true);
}
const unitCell = units => `<span class="unit-cell">${units === '1' ? '量纲一' : unit(units)}</span>`;

function esc(value) {
  return String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c]);
}
const element = name => form.elements.namedItem(name);
const value = name => element(name).value;
const numeric = name => element(name).valueAsNumber;
const show = (selector, text) => { $(selector).textContent = text; };
const showFormula = (selector, html) => { $(selector).innerHTML = html; };
const on = (selector, event, handler) => $(selector)?.addEventListener(event, handler);
const fmt = format;
const precise = (value, digits = 4) => m(numericSource(value, digits), true);
const unit = value => m(unitSource(value), true);
const quantity = (value, units, digits = 3) => Number.isFinite(value)
  ? m(`${numericSource(value,digits)} ${unitSource(units)}`,true) : '—';
const symbolUnit = (symbol, units) => `${m(symbol)} ${unit(`[${units}]`)}`;
const ng = (n, digits = 2) => Number.isFinite(n) ? m(`${n > 0 ? '+' : ''}${fmt(n,digits)}%`,true) : '—';
const badge = (text, kind = 'na') => `<span class="badge ${kind}">${esc(text)}</span>`;
const options = (rows, selected) => rows.map(([id, label]) =>
  `<option value="${esc(id)}"${selected === id ? ' selected' : ''}>${esc(label)}</option>`).join('');
const chart = (id, title, className = 'small-chart', notation) =>
  `<figure class="figure"><h3>${esc(title)}${notation ? ` ${symbolUnit(notation.symbol,notation.unit)}` : ''}</h3><canvas id="${id}" class="${className}" role="img" aria-label="${esc(title)}${notation ? `（${esc(notation.unit)}）` : ''}" data-title="${esc(title)}" data-symbol="${esc(notation?.symbol)}" data-unit="${esc(notation?.unit)}"></canvas></figure>`;
const header = (title, caption = '', tools = '') =>
  `<div class="panel-top"><div><h2>${title}</h2>${caption ? `<p>${caption}</p>` : ''}</div>${tools ? `<div class="toolbar">${tools}</div>` : ''}</div>`;
const btn = (id, title) => `<button id="${id}" type="button">${title}</button>`;
const link = (url, label) => `<a href="${esc(url)}" target="_blank" rel="noopener noreferrer">${esc(label)}</a>`;
// Tables: a column whose cells are mostly numbers is right-aligned,
// header included, so digits line up.
const numericCell = value => /^(<span class="math num">|—$|−?\d)/.test(String(value));
const fields = (head, rows) => {
  const numericColumn = head.map((_, c) => c > 0 && rows.length > 0
    && rows.filter(row => numericCell(row[c])).length >= Math.ceil(rows.length / 2));
  const cls = c => numericColumn[c] ? ' class="num"' : '';
  return `<div class="scroll-table"><table><thead><tr>${head.map((h, c) => `<th scope="col"${cls(c)}>${h}</th>`).join('')}</tr></thead><tbody>${
    rows.map(([first,...rest]) => `<tr><th scope="row">${first}</th>${rest.map((v, i) => `<td${cls(i + 1)}>${v}</td>`).join('')}</tr>`).join('')
  }</tbody></table></div>`;
};

async function request(url, opts) {
  const response = await fetch(url, opts);
  const data = await response.json();
  if (!response.ok) throw new Error(data.error || '请求失败');
  return data;
}

function readInput() {
  return {
    kind: value('kind'), preset: value('preset'), oxidizer: value('oxidizer'), fuel: value('fuel'),
    tox: numeric('tox'), tfuel: numeric('tfuel'), of: numeric('of'), pc: numeric('pc') * 1e6,
    pa: numeric('pa') * 1000, mdot: numeric('mdot'), exitMode: value('exitMode'),
    exitValue: numeric('exitValue') * exitLabels[value('exitMode')][2], model: value('model'),
    throat: value('throat'), points: numeric('points'), heat: numeric('heat'), heatMax: numeric('heatMax'),
    solid: value('solid'), tin: numeric('tin'),
  };
}

function status(message, className = '') {
  $('#run-status').className = `run-status ${className}`;
  show('#run-status', message);
}

function configureExit(reset = false) {
  const mode = value('exitMode');
  const [label, unit, , min] = exitLabels[mode];
  show('#exit-label', label);
  showFormula('#exit-unit', m(unit, mode === 'pe'));
  element('exitValue').min = min;
  if (reset) element('exitValue').value = { eps: 20.5, pe: 100, pr: .01, mach: 3 }[mode];
}

function setKind(kind) {
  element('kind').value = kind;
  $('#liquid-fields').hidden = $('#liquid-fields').disabled = kind === 'solid';
  $('#solid-fields').hidden = $('#solid-fields').disabled = kind !== 'solid';
  for (const label of document.querySelectorAll('[data-heat]')) {
    label.hidden = kind === 'solid';
    label.querySelector('input').disabled = kind === 'solid';
  }
  element('mdot').min = kind === 'solid' ? '.001' : '0';
}

function fillPreset(id) {
  const preset = state.catalog?.presets.find(p => p.id === id);
  if (!preset) {
    show('#preset-source', '保留当前输入，可编辑各项工况。');
    return;
  }
  const { oxidizer, fuel, tox, tfuel, of, pc, pa, mdot, eps, source } = preset;
  const values = { preset: id, oxidizer, fuel, tox, tfuel, of, pc: pc / 1e6, pa: pa / 1000, mdot, exitMode: 'eps', exitValue: eps };
  for (const [key, item] of Object.entries(values)) element(key).value = item;
  show('#preset-source', `输入来源 · ${source.replace(/, https?:\/\/\S+$/, '')}`);
  configureExit();
}

function defaultCase() {
  if (!state.catalog) return;
  setKind('liquid');
  fillPreset('TQ-12B');
  for (const [key, item] of Object.entries({ model: 'eq', throat: 'sonic', points: 41, heat: 0, heatMax: 0 })) {
    element(key).value = item;
  }
}

function caseName(input) {
  if (input.kind === 'solid') return `固体配方 ${input.solid}`;
  return input.preset || `${input.oxidizer} / ${input.fuel}`;
}

const plume = createPlume($('#hero-canvas'), { onLayout: placeCallouts });
function placeCallouts(layout, model) {
  const box = $('#hero-callouts');
  if (!box || !state.record) return;
  const { main } = state.record;
  const { chamber, throat, exit } = main.result;
  const items = [
    { cls: 'above', x: layout.px(model.xMin + model.lChamber * .45), y: layout.py(model.rc) - 16,
      title: '燃烧室', body: `${quantity(chamber.T_c_K,'K',0)} · ${quantity(chamber.p_c_Pa / 1e6,'MPa',1)}` },
    { cls: 'below', x: layout.px(0), y: layout.py(-1) + 16,
      title: '喉部', body: `${m('Ma')} ${precise(throat.Ma,2)} · ${quantity(throat.T_K,'K',0)}` },
    { cls: 'exit', x: layout.px(model.xExit) + 16, y: layout.py(model.rExit) + 4,
      title: '出口', body: `${m('Ma_{e}')} ${precise(exit.Ma,3)} · ${quantity(exit.T_K,'K',0)} · ${quantity(exit.u_m_s,'m/s',0)}` },
  ];
  box.innerHTML = items.map(item => `<div class="callout ${item.cls}"><b>${item.title}</b><span>${item.body}</span></div>`).join('');
  [...box.children].forEach((node, i) => { node.style.left = `${items[i].x}px`; node.style.top = `${items[i].y}px`; });
}

function countUp(node, value, digits) {
  if (!Number.isFinite(value)) { node.textContent = '—'; return; }
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;
  const format = v => v.toLocaleString('zh-CN', { minimumFractionDigits: digits, maximumFractionDigits: digits });
  if (reduced) { node.textContent = format(value); return; }
  const started = performance.now();
  const tick = now => {
    const t = Math.min(1, (now - started) / 1100);
    node.textContent = format(value * (1 - (1 - t) ** 4));
    if (t < 1) requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

function renderHero() {
  const { input, main } = state.record;
  const preset = state.catalog.presets.find(item => item.id === input.preset);
  const liquid = input.kind === 'liquid';
  show('#hero-name', liquid ? (preset?.name ?? `${input.oxidizer} / ${input.fuel}`) : `固体配方 ${input.solid}`);
  show('#hero-code', liquid ? (input.preset || '自定义') : 'SOLID');
  const cycle = state.record.cycle?.label;
  const exitText = input.exitMode === 'eps' ? `${m('ε')} ${precise(input.exitValue,4)}` : esc(exitLabels[input.exitMode][0]);
  showFormula('#hero-sub', [
    liquid ? `${esc(input.oxidizer)} / ${esc(input.fuel)}` : '固体推进剂',
    cycle ? esc(cycle) : '', `${m('p_{c}')} ${quantity(input.pc / 1e6,'MPa',2)}`,
    liquid ? `${m('O/F')} ${precise(input.of,3)}` : '', exitText, esc(main.modelLabel),
  ].filter(Boolean).join('<span class="dot-sep"></span>'));
  const env = envelope(state.record, state.losses, { engineLevel: state.engineLevel });
  const level = env?.engine ?? env?.chamber;
  const readouts = [
    { key: 'isp', label: `理想比冲 ${m('I_{sp}')}`, unit: 's', digits: 1, value: main.summary.isp,
      note: `真空 ${quantity(main.summary.ispVac,'s',1)}${level ? ` · 名义交付 ${quantity(level.nominal.isp,'s',1)}` : ''}`, hot: true },
    { key: 'temperature', label: `燃烧室温度 ${m('T_{c}')}`, unit: 'K', digits: 0, value: main.summary.temperature, note: esc(main.modelLabel) },
    { key: 'cstar', label: `特征速度 ${m('c^{∗}')}`, unit: 'm/s', digits: 0, value: main.summary.cstar, note: '主推力室' },
    { key: 'thrust', label: `推力 ${m('F')}`, unit: 'kN', digits: 0, value: main.summary.thrust,
      note: input.mdot ? `真空 ${quantity(main.summary.thrustVac,'kN',0)}` : '主推力室流量未提供' },
  ];
  $('#hero-readouts').innerHTML = readouts.map(r => `<div class="readout${r.hot ? ' hot' : ''}">
      <dt>${r.label}</dt><dd><span class="readout-value" data-key="${r.key}">—</span><span class="readout-unit">${unit(r.unit)}</span></dd>
      <p>${r.note}</p></div>`).join('');
  readouts.forEach(r => countUp($(`.readout-value[data-key="${r.key}"]`), r.value, r.digits));
  plume.setResult(main.result);
}

function showCurrent() {
  const { input, main, cases } = state.record;
  renderHero();
  show('#case-title', caseName(input));
  const kind = cases.length ? `单工况 + ${cases.length} 个区间算例` : '单工况（未算区间）';
  showFormula('#case-detail', `${esc(main.modelLabel)} · ${quantity(input.pc / 1e6,'MPa')} · ${esc(kind)}`);
  $('#stale-banner').hidden = !state.stale;
  status(`已完成 · ${fmt(main.seconds, 2)} s 基准算例${cases.length ? ` · ${cases.length} 个区间工况` : ''}${state.stale ? ' · 输入已修改' : ''}`);
}

function markStale() {
  state.stale = true;
  if (state.record && !state.busy) {
    status('输入已修改。右侧仍显示上次结果。');
    $('#stale-banner').hidden = false;
  }
}

function setTab(tab, focus = false) {
  state.tab = tab;
  for (const button of document.querySelectorAll('[data-tab]')) {
    if (button.dataset.tab === tab) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  }
  render();
  if (focus) panel.focus({ preventScroll: true });
}

async function run(range, tab, initiated = true) {
  if (!state.catalog || state.busy || !form.reportValidity()) return;
  const input = readInput();
  state.busy = true;
  state.stale = false;
  document.body.classList.add('busy');
  panel.setAttribute('aria-busy', 'true');
  $('#run').disabled = $('#run-range').disabled = true;
  status(range ? 'C 求解器正在复算平衡与冻结工况…' : 'C 求解器正在计算…', 'pending');
  try {
    state.record = await request('/api/calculate', {
      method: 'POST', headers: {'Content-Type':'application/json'}, body: JSON.stringify({input, range}),
    });
    state.sample = 0;
    state.revealCharts = true;
    state.heatFocus = {row: 0, index: 0};
    state.nozzleFocus = 0;
    state.scene = range && tab === 'field' ? 'cases' : 'nozzle';
    state.overlay = false;
    showCurrent();
    setTab(tab ?? state.tab);
    if (initiated && innerWidth <= 820) panel.scrollIntoView({ block: 'start' });
  } catch (error) {
    status(`求解失败 · ${error.message}`, 'error');
    if (!state.record) render();
  } finally {
    state.busy = false;
    document.body.classList.remove('busy');
    panel.setAttribute('aria-busy', 'false');
    $('#run').disabled = $('#run-range').disabled = false;
  }
}

let exportTimer;
function download(name, data, type) {
  const blob = new Blob([data], { type });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = name;
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
  $('#export-status').hidden = false;
  show('#export-status', `已生成 ${name} · ${fmt(blob.size / 1024, 1)} KB`);
  clearTimeout(exportTimer);
  exportTimer = setTimeout(() => { $('#export-status').hidden = true; }, 4500);
}
function csv(name, header, data) {
  const field = cell => `"${String(cell ?? '').replaceAll('"', '""')}"`;
  const lines = [header, ...data].map(row => row.map(field).join(','));
  download(name, '\ufeff' + lines.join('\r\n'), 'text/csv;charset=utf-8');
}
function png(id, name) {
  const source = $(`#${id}`);
  if (!source?.width) return;
  const canvas = document.createElement('canvas');
  const ratio = source.width / source.getBoundingClientRect().width;
  const top = Math.round(55 * ratio);
  canvas.width = source.width;
  canvas.height = source.height + top;
  const ctx = canvas.getContext('2d');
  const t = theme();
  ctx.fillStyle = t.bg;
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.drawImage(source, 0, top);
  const { title, symbol, unit: units } = source.dataset;
  const label = symbol ? [title,' ',mathText(symbol),mathText(` [${unitSource(units)}]`,true)] : title || source.getAttribute('aria-label');
  caption(ctx,label,14*ratio,19*ratio,'left',t.strong,18*ratio);
  ctx.fillStyle = t.text;
  ctx.font = `${12*ratio}px "Inter","PingFang SC","Microsoft YaHei UI",sans-serif`;
  ctx.fillText(`${caseName(state.record.input)} · C 求解器 · ${state.record.createdAt.slice(0,10)}`, 14 * ratio, 43 * ratio);
  canvas.toBlob(blob => { if (blob) download(name + '.png', blob, 'image/png'); });
}
function saveJson() {
  const env = envelope(state.record, state.losses, { engineLevel: state.engineLevel });
  const record = { ...state.record, losses: state.losses, engineLevel: state.engineLevel,
    envelope: env, publicJudgement: judgePublic(state.record, env, state.losses) };
  download(`${caseName(state.record.input)}-热力结果.json`, JSON.stringify(record, null, 2), 'application/json');
}
function saveProfile(cases = [state.record.main]) {
  const keys = ['i','p_Pa','T_K','u_m_s','Ma','gamma','rho_kg_m3','eps','Y_c','h_J_kg','s_J_kgK','a_m_s','G_kg_m2s'];
  csv('喷管剖面.csv', ['case','role','model','heat_kJ_kg','section', ...keys], cases.flatMap(item => {
    const { profile, throat } = item.result;
    const record = (name, p) => [item.id,item.role,item.model,item.heat,name, ...keys.map(key => {
      if (key !== 'eps') return p[key];
      if (name === 'throat') return 1;
      return p.Ma < 1 ? '' : p[key];  // solver leaves subsonic A/At at 0
    })];
    return [record('throat', throat), ...profile.map((point, index) => record(index === 0 ? 'chamber' : index === profile.length - 1 ? 'exit' : 'sample', point))];
  }));
}
function bindDownload(selector, action) { on(selector, 'click', action); }

function render() {
  state.redraw = null;
  if (!state.record) {
    panel.innerHTML = '<section class="panel initial"><h2>等待计算结果</h2><p>选择工况并运行。失败原因会显示在输入区。</p></section>';
    return;
  }
  const views = {overview: overview, process: processView, field: fieldView, validation: validationView, compare: compareView};
  views[state.tab]();
}

function setRedraw(fn) { state.redraw = fn; fn(); }
const sizeObserver = new ResizeObserver(() => state.redraw?.());
sizeObserver.observe(panel);

function stat(label, value, unit, digits, detail) {
  return `<article class="stat"><div class="stat-label">${label}</div><div class="stat-value">${precise(value,digits)} <span class="stat-unit">${Number.isFinite(value) ? m(unitSource(unit),true) : ''}</span></div><div class="stat-detail">${detail}</div></article>`;
}

function caseLegend(cases) {
  return `<div class="legend">${cases.map((item, index) => `<span><i class="series-${index % colors.length}"></i>${item.displayName ?? esc(item.name)}</span>`).join('')}<span><i class="series-1 dot"></i>首条曲线喉部</span></div>`;
}

function profilePlots(cases, axis, sampleReadout = true) {
  const active = cases.filter(item => item.main.result.profile.length >= 2);
  const canvases = profileMetrics.slice(0, 4).map(metric => $(`#plot-${metric.id}`));
  if (!active.length) {
    setRedraw(() => canvases.forEach(canvas => linearChart(canvas, {series:[]})));
    return;
  }
  const base = active[0].main.result;
  const logPressure = (point, result) => -Math.log(point.p_Pa / result.chamber.p_c_Pa);
  const getX = (point, result) => axis === 'pressure'
    ? logPressure(point, result) : point.i / (result.profile.length - 1);
  const includeThroat = (base.throat.p_Pa >= base.exit.p_Pa && base.throat.p_Pa <= base.chamber.p_c_Pa);
  const throatX = axis === 'pressure' ? logPressure(base.throat, base) : logPressure(base.throat, base) / logPressure(base.exit, base);
  const maxX = axis === 'pressure' ? Math.max(...active.map(c => logPressure(c.main.result.exit, c.main.result))) : 1;
  const span = Math.max(maxX, 1e-8);
  const decades = [];
  for (let k = 0; k * Math.LN10 <= span + 1e-9; k++) decades.push(k * Math.LN10);
  const xt = axis === 'pressure'
    ? (decades.length >= 2 ? decades : [0, span])
    : [0, .25, .5, .75, 1];
  const current = () => base.profile[Math.min(state.sample, base.profile.length - 1)];
  const layouts = [];
  let reveal = 1;
  const redraw = () => {
    const focus = current();
    profileMetrics.slice(0, 4).forEach((metric, i) => {
      const series = active.map((item, j) => {
        const result = item.main.result;
        return { color: colors[j % colors.length], dash: j ? [5, 3] : [],
          points: result.profile.map(point => [getX(point, result), metric.read(point)]) };
      });
      layouts[i] = linearChart(canvases[i], {
        series, xTicks: xt, xDomain: [0, axis === 'pressure' ? span : 1],
        xFormat: x => axis === 'pressure' ? mathText(x === 0 ? '1' : `10^{${-Math.round(x / Math.LN10)}}`, true) : mathText(numericSource(x,3),true),
        xLabel: axis === 'pressure' ? ['压强比 ', mathText('p/p_{c}'),'（对数刻度）'] : ['归一化膨胀进度 ',mathText('ξ')],
        refs: includeThroat ? [{x:throatX, dot:metric.read(base.throat), color:colors[1]}] : [],
        focus: sampleReadout && reveal >= 1 ? [getX(focus, base), metric.read(focus)] : null,
        reveal,
      });
    });
    if (sampleReadout && $('#sample-values')) {
      showFormula('#sample-values', `截面 ${focus.i} · ${m('p/p_{c}')} ${precise(focus.p_Pa / base.chamber.p_c_Pa,5)} · ${quantity(focus.T_K,'K',1)} · ${m('Ma')} ${precise(focus.Ma,3)} · ${quantity(focus.u_m_s,'m/s',1)}`);
    }
  };
  if (sampleReadout) {
    const slider = $('#profile-slider');
    slider.max = base.profile.length - 1;
    slider.value = Math.min(state.sample, base.profile.length - 1);
    slider.oninput = () => { state.sample = slider.valueAsNumber; redraw(); };
    canvases.forEach((canvas, i) => { canvas.onpointermove = event => {
      const layout = layouts[i];
      if (!layout?.hit(event.offsetX, event.offsetY)) return;
      const target = layout.fromPixel(event.offsetX);
      let nearest = base.profile[0], distance = Infinity;
      for (const point of base.profile) {
        const gap = Math.abs(getX(point, base) - target);
        if (gap < distance) { nearest = point; distance = gap; }
      }
      state.sample = nearest.i;
      slider.value = nearest.i;
      redraw();
    }; });
  }
  setRedraw(redraw);
  if (state.revealCharts && !matchMedia('(prefers-reduced-motion: reduce)').matches) {
    state.revealCharts = false;
    const started = performance.now();
    const tick = now => {
      if (!canvases[0]?.isConnected) return;   // view changed mid-animation
      const t = Math.min(1, (now - started) / 900);
      reveal = 1 - (1 - t) ** 3;
      redraw();
      if (t < 1) requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
  }
}

function stateTables({ chamber, throat, exit, performance, product_properties: props }) {
  const prop = (scope, key, flag = 'valid') => scope?.[flag] ? scope[key] : null;
  const areaMagnitude = performance.m_dot_kg_s > 0;
  const values = [
    ['温度', 'K', chamber.T_c_K, throat.T_K, exit.T_K],
    ['绝对压强', 'MPa', chamber.p_c_Pa / 1e6, throat.p_Pa / 1e6, exit.p_Pa / 1e6],
    ['马赫数', '1', 0, throat.Ma, exit.Ma],
    ['流速', 'm/s', 0, throat.u_m_s, exit.u_m_s],
    ['比焓', 'MJ/kg', chamber.h_c_J_kg / 1e6, throat.h_J_kg / 1e6, exit.h_J_kg / 1e6],
    ['比熵', 'kJ/(kg·K)', chamber.s_c_J_kgK / 1000, throat.s_J_kgK / 1000, exit.s_J_kgK / 1000],
    [`面积比 ${m('A/A_{t}')}`, '1', null, 1, exit.eps],
    ['截面积', 'm²', null, areaMagnitude ? performance.A_t_m2 : null, areaMagnitude ? performance.A_e_m2 : null],
    ['等效直径', 'm', null, areaMagnitude ? performance.d_t_m : null, areaMagnitude ? performance.d_e_m : null],
    ['凝相质量分数', '%', prop(props?.chamber, 'Y_c') === null ? null : 100 * props.chamber.Y_c, 100 * throat.Y_c, 100 * exit.Y_c],
    ['平衡定压比热', 'J/(kg·K)', prop(props?.chamber, 'cp_equilibrium_J_kgK', 'equilibrium_valid'), null, prop(props?.exit, 'cp_equilibrium_J_kgK', 'equilibrium_valid')],
    ['气相动力黏度', 'Pa·s', prop(props?.chamber, 'mu_Pa_s', 'transport_valid'), null, prop(props?.exit, 'mu_Pa_s', 'transport_valid')],
  ];
  return fields(['量', '单位', '燃烧室', '喉部', '出口'], values.map(([label, units, ...row]) => [
    label, unitCell(units), ...row.map(v => sig(v, 5)),
  ]));
}

function composition(result) {
  const sorted = result.composition.toSorted((a,b)=>b.n_mol_kg-a.n_mol_kg);
  const gas = sorted.filter(row=>row.phase === 'gas');
  const total = gas.reduce((sum,row)=>sum+row.n_mol_kg, 0);
  const bars = gas.slice(0, 8).map(row => {
    const pct = 100 * row.n_mol_kg / total;
    return `<div class="bar-row"><span title="${esc(row.name)}">${esc(row.name)}</span><meter min="0" max="100" value="${pct}" aria-label="${esc(row.name)} 气相摩尔百分比"></meter><span class="bar-values">${quantity(pct,'%',4)}</span></div>`;
  });
  return `<div class="bar-list">${bars.join('') || '<p class="panel-note">无气相组分。</p>'}</div>
  <details class="advanced"><summary>查看全部导出组分</summary>${fields(['组分', '相态', unit('mol/kg')], sorted.map(row => [esc(row.name), row.phase === 'gas' ? '气相' : '凝相', precise(row.n_mol_kg, 6)]))}</details>`;
}

function overview() {
  const { main, input, cases } = state.record;
  const result = main.result;
  const alt = cases.find(item => item.model === (main.model === 'fc' ? 'eq' : 'fc') && item.heat === main.heat);
  const compareProfiles = [{ name: `基准 · ${main.modelLabel}`, main }];
  if (alt && state.overlay) compareProfiles.push({ name: alt.modelLabel, main: alt });
  const charts = profileMetrics.slice(0, 4).map(metric => chart(`plot-${metric.id}`,metric.label,'small-chart',metric)).join('');
  const warnings = main.warnings.map(warning => `<p class="notice">${esc(warning)}</p>`).join('');
  const env = envelope(state.record, state.losses, { engineLevel: state.engineLevel });
  const level = env?.engine ?? env?.chamber;
  const delivered = level ? ` · 名义交付 ${quantity(level.nominal.isp,'s',1)}${env.engine ? '（整机）' : ''}` : '';
  panel.innerHTML = `<div class="stack">
    <p class="panel-note lead">顶部主视觉与读数为理想（${esc(main.modelLabel)}）热力解。计入燃烧、扩张、边界层${input.kind === 'liquid' ? '及整机支路' : ''}损失后的实际性能见「区间与验证」。</p>
    <section class="panel">
      ${header('喷管膨胀剖面', `环境绝对压强 ${quantity(input.pa / 1000,'kPa')} · ${result.profile.length} 个实算采样点`,
        `<select id="axis-choice" aria-label="剖面横轴">${options([['pressure','压强比'],['progress','膨胀进度']], state.axis)}</select>
        ${alt ? btn('toggle-alt', state.overlay ? '移除模型叠加' : '叠加另一模型') : ''}
        ${btn('profile-export','剖面 CSV')}`)}
      ${warnings}
      ${caseLegend(compareProfiles)}
      <div class="chart-grid">${charts}</div>
      <div class="sample"><label for="profile-slider">截面采样</label><input id="profile-slider" type="range" min="0" max="1" value="0"><strong id="sample-values"></strong></div>
      <details class="advanced"><summary>图表导出</summary><div class="toolbar">
        ${profileMetrics.slice(0,4).map(metric => btn(`export-${metric.id}`, `${metric.label} PNG`)).join('')}
      </div></details>
    </section>
    <div class="split">
      <section class="panel">${header('关键截面状态')}${stateTables(result)}</section>
      <section class="panel">${header('出口主要组分', '气相摩尔百分比 · 以导出的气相组分为基数')}${composition(result)}</section>
    </div>
    <section class="panel">${header('工况记录', '', `${btn('pin-case','保存到对比')}${btn('result-export','完整 JSON')}`)}
      <p class="panel-note">${esc(caseName(input))} · ${esc(main.modelLabel)} · 热损失 ${quantity(main.heat,'kJ/kg',2)} · ${quantity(input.mdot,'kg/s',2)}</p>
      <p class="panel-note" id="save-feedback" role="status"></p>
    </section>
  </div>`;
  on('#axis-choice', 'change', event => { state.axis = event.target.value; profilePlots(compareProfiles, state.axis); });
  on('#toggle-alt', 'click', () => { state.overlay = !state.overlay; overview(); });
  bindDownload('#profile-export', () => saveProfile());
  bindDownload('#result-export', saveJson);
  for (const metric of profileMetrics.slice(0,4)) bindDownload(`#export-${metric.id}`, () => png(`plot-${metric.id}`, metric.label));
  on('#pin-case', 'click', pinCase);
  profilePlots(compareProfiles, state.axis);
}

function processView() {
  const { main } = state.record;
  const { chamber, database, throat } = main.result;
  const traces = main.traces;
  const last = traces.at(-1);
  const phases = Object.entries(stageLabels);
  panel.innerHTML = `<div class="stack">
    <section class="panel">${header('求解链路', '计数与残差均由本次 C 程序运行记录。', btn('trace-export','试算 CSV'))}
      <div class="process-steps">
        <div class="process-step"><h3>九系数数据库</h3><span>${fmt(database.species,0)} 个规范化物种</span></div>
        <div class="process-step"><h3>燃烧室平衡</h3><span>${chamber.temperature_evaluations} 次温度试算</span></div>
        <div class="process-step"><h3>临界截面</h3><span>喉部 ${m('Ma')} ${precise(throat.Ma,4)}</span></div>
        <div class="process-step"><h3>膨胀与性能</h3><span>${main.result.profile.length} 个剖面采样点</span></div>
      </div>
      <p class="panel-note">Brent 迭代 ${chamber.brent_iterations} 次 · 末态平衡迭代 ${chamber.equilibrium_iterations} 次 · 凝相调整 ${chamber.phase_iterations} 轮 · 末次焓残差 ${m('Δh')} = ${quantity(last?.residual,'J/kg')}</p>
    </section>
    <section class="panel">${header('燃烧室温度求根','逐次记录试算温度与相对目标焓的偏差。')}
      <div class="legend">${phases.map(([key,label]) => `<span><i class="series-${stageColor[key]}"></i>${label}</span>`).join('')}</div>
      <div class="chart-grid">${chart('trace-temp','试算温度','wide-chart',{symbol:'T',unit:'K'})}${chart('trace-residual','绝对焓残差（对数刻度）','wide-chart',{symbol:'|Δh|',unit:'J/kg'})}</div>
      <p class="panel-note">残差图设 ${quantity(1e-10,'J/kg')} 显示下限。原始带符号值见表和 CSV。Brent 法要求残差与温度区间宽度同时达标才停止，所以残差先降到很小、随后在收窄区间时回升是正常现象；最终复算点（橙色）才是采用的解。</p>
      <details class="advanced"><summary>查看逐次试算表</summary>${fields(['序号','阶段',`温度 ${symbolUnit('T','K')}`,`焓残差 ${symbolUnit('Δh','J/kg')}`], traces.map(row=>[row.evaluation,stageLabels[row.stage] ?? '未知',precise(row.temperature,6),precise(row.residual,6)]))}</details>
    </section>
    <section class="panel">${header('状态守恒与复算检查')}${checkTable(main.checks)}</section>
  </div>`;
  bindDownload('#trace-export', () => csv('燃烧室求根.csv',['evaluation','stage','temperature_K','enthalpy_residual_J_kg'], traces.map(t=>[t.evaluation,t.stage,t.temperature,t.residual])));
  const traceSeries = f => [
    {color:theme().grid, width:1, points:traces.map(row=>[row.evaluation,f(row)])},
    ...phases.map(([stage])=>({ color:modelColors[stage], dots:true, points:traces.filter(row=>row.stage===stage).map(row=>[row.evaluation,f(row)]) })),
  ];
  const x = '温度试算序号';
  setRedraw(() => {
    linearChart($('#trace-temp'), {series:traceSeries(row=>row.temperature), xLabel:x});
    linearChart($('#trace-residual'), {
      series:traceSeries(row=>Math.log10(Math.max(1e-10,Math.abs(row.residual)))),
      xLabel:x, yFormat:tick=>mathText(`10^{${fmt(tick,1)}}`,true),
      refs:[{y:-2,color:theme().text}],
    });
  });
}

function checkTable(items) {
  const labels = {
    '燃烧室焓求根 |Δh|': `燃烧室焓求根 ${m('|Δh|')}`,
    '重建燃烧室质量闭合 |Σ nᵢMᵢ − 1|': `重建燃烧室质量闭合 ${m('|∑ n_{i} M_{i} − 1|')}`,
    '喉部声速条件 |Ma − 1|': `喉部声速条件 ${m('|Ma − 1|')}`,
  };
  const rows = fields(['检查项','偏差','判据上限','单位','结果'], items.map(item => [
    labels[item.label] ?? esc(item.label), precise(item.value), precise(item.limit), unitCell(item.unit),
    item.construct && item.status === 'pass' ? badge('按构造满足','na')
      : badge(({ pass:'满足', review:'需复核', na:'不适用' })[item.status], item.status),
  ]));
  return `${rows}<p class="panel-note">「按构造满足」的量由求解器直接施加（等熵推进、喉部声速搜索），零偏差只说明记账一致，不构成独立物理检验。焓、质量与推力复算可以失败，是真正的校核。</p>`;
}

function fieldView() {
  const { main, cases, input } = state.record;
  const metric = profileMetrics.find(item => item.id === state.variable);
  const caseRows = cases.filter(item => item.result.profile.length >= 2);
  const rows = (caseRows.length ? caseRows : [main].filter(item => item.result.profile.length >= 2))
    .map(item => ({
      id: item.id, model: item.model, modelLabel: item.modelLabel, heat: item.heat,
      label: `${item.modelLabel} · 热损失 ${fmt(item.heat,2)}`,
      shortLabel: item.model === 'eq' ? '平衡' : item.model === 'fc' ? '冻结' : item.modelLabel,
      points: item.result.profile, chamber: item.result.chamber,
    }));
  const geometry = nozzleGeometry(main.result);
  const availableModels = [...new Set(rows.map(row=>row.model))].filter(model =>
    new Set(rows.filter(row=>row.model===model).map(row=>row.heat)).size >= 2);
  if (availableModels.length && !availableModels.includes(state.fieldModel)) state.fieldModel = availableModels[0];
  const surfaceRows = rows.filter(row => row.model === state.fieldModel).sort((a,b)=>a.heat-b.heat);
  const views = [
    ...(geometry ? [['nozzle','喷管轴对称曲面']] : []),
    ...(availableModels.length ? [['cases','热损失工况曲面']] : []),
  ];
  if (!views.some(([id])=>id===state.scene)) state.scene = views[0]?.[0] ?? 'nozzle';
  const isNozzle = state.scene === 'nozzle';
  const radialUnit = geometry?.throatRadius != null ? 'm' : '1';
  const radialSymbols = radialUnit === 'm' ? ['y','z'] : ['y/r_{t}','z/r_{t}'];
  const axisKey = isNozzle
    ? `<span class="axis-x">绝对压强 ${symbolUnit('p','MPa')} · 对数刻度</span>
      <span class="axis-y">第一横向坐标 ${symbolUnit(radialSymbols[0],radialUnit)}</span>
      <span class="axis-z">第二横向坐标 ${symbolUnit(radialSymbols[1],radialUnit)}</span>`
    : `<span class="axis-x">归一化膨胀进度 ${symbolUnit('ξ','1')}</span>
      <span class="axis-y">单位质量热损失 ${symbolUnit('Q','kJ/kg')}</span>
      <span class="axis-z">${esc(metric.label)} ${symbolUnit(metric.symbol,metric.unit)}</span>`;
  const surfaceTitle = isNozzle ? '三维扩张段（面积–压强旋转体）' : '三维工况曲面';
  const surfaceCaption = isNozzle
    ? '按各截面面积比旋转生成，可拖动旋转。轴向按对数压强排布，不代表喷管真实长度和型面。'
    : '在同一膨胀模型下连接不同热损失工况的截面状态。';
  const surfaceNote = isNozzle
    ? `半径由 ${m('r/r_{t} = √(A/A_{t})')} 得到。轴向按压强定位，实际长度需另给几何坐标。颜色对应截面平均状态。${radialUnit === '1' ? '主推力室流量未知，横向坐标以喉半径归一化。' : ''}`
    : '曲面连接相邻实算样点。各工况在同一膨胀进度下可能对应不同压强。';
  const baseRow = rows.find(row => row.model === 'eq' && row.heat === main.heat) ?? rows[0];
  const canDelta = rows.length >= 2 && rows.every(row => row.points.length === baseRow?.points.length);
  if (!canDelta) state.fieldMode = 'value';
  const delta = state.fieldMode === 'delta';
  const readCell = delta
    ? (point, j, i) => metric.read(point) - metric.read(baseRow.points[i])
    : point => metric.read(point);
  const domain = extent(rows.flatMap((row, j) => row.points.map((point, i) => readCell(point, j, i))));
  // In difference mode the heat-loss surface shows the change from Q = 0 of
  // the same model, so the heat-loss effect is not swamped by the expansion.
  const surfaceBase = surfaceRows[0];
  const surfaceDelta = delta && surfaceRows.length >= 2 && surfaceRows.every(row => row.points.length === surfaceBase.points.length);
  const surfaceMetric = surfaceDelta ? { ...metric, log: false, label: `${metric.label}差值（相对 Q = ${fmt(surfaceBase.heat,0)}）`, read: point => point.delta } : metric;
  const surfaceData = surfaceDelta ? surfaceRows.map(row => ({ ...row,
    points: row.points.map((point, i) => ({ ...point, delta: metric.read(point) - metric.read(surfaceBase.points[i]) })) })) : surfaceRows;
  const surfaceDomain = extent(surfaceData.flatMap(row=>row.points.map(surfaceMetric.read)));
  if (!rows[state.heatFocus.row]) state.heatFocus = {row:0,index:0};
  panel.innerHTML = `<div class="stack">
    <section class="panel">
      ${header('准一维彩色剖面', '每一行是一组 C 求解工况，单元颜色对应一个实际采样点。', `
        <label for="field-variable">显示物理量</label><select id="field-variable">${options(profileMetrics.map(m=>[m.id,m.label]),state.variable)}</select>
        ${canDelta ? `<select id="field-mode" aria-label="色标模式">${options([['value','绝对值'],['delta',`相对${baseRow.modelLabel}差值`]],state.fieldMode)}</select>` : ''}
        ${btn('heat-export','彩色图 PNG')}`)}
      ${rows.length ? `${chart('heatmap', `${metric.label}${delta ? ` 差值（相对${baseRow.modelLabel}）` : ''} · 工况矩阵`, 'heat-chart', metric)}
        <div class="sample"><label for="heat-row">工况</label><select id="heat-row" aria-label="色图工况">${options(rows.map((r,i)=>[String(i),r.label]),String(state.heatFocus.row))}</select>
        <label for="heat-index">截面</label><input type="range" id="heat-index" min="0" max="1" value="0"><strong id="heat-values"></strong></div>` : '<p class="notice">本次计算缺少有效剖面。</p>'}
      <p class="panel-note">${m('ξ = ln(p/p_{c}) / ln(p_{e}/p_{c})')}。${m('0',true)} 为室压状态，${m('1',true)} 为出口。不同工况的同一 ${m('ξ')} 可对应不同压强。${metric.log && !delta ? '压强、密度跨越多个数量级，采用对数色标。' : ''}${canDelta && !delta ? '工况间差异远小于沿程变化，切换到「差值」可看清。' : ''}</p>
    </section>
    <section class="panel">
      ${header(views.length ? surfaceTitle : '三维可视化', views.length ? surfaceCaption : '', `
        ${views.length > 1 ? `<select id="surface-view" aria-label="三维图类型">${options(views,state.scene)}</select>` : ''}
        ${!isNozzle ? `<select id="surface-model" aria-label="三维曲面膨胀模型">${options(availableModels.map(model=>[model,state.catalog.models[model]]),state.fieldModel)}</select>` : ''}
        ${views.length ? btn('surface-export','三维图 PNG') : ''}
        ${!availableModels.length && input.kind === 'liquid' && input.heatMax > 0 ? btn('surface-range','计算工况区间') : ''}`)}
      ${views.length ? `<div class="axis-key" aria-label="三维坐标轴">${axisKey}</div>
        ${chart('surface',`${surfaceTitle} · ${metric.label}`,'surface',metric)}
        <div class="toolbar surface-controls"><label for="camera-angle">方位 ${unit('[°]')}</label><input type="range" id="camera-angle" min="-180" max="180" value="${state.camera.angle}">
        <label for="camera-tilt">俯视 ${unit('[°]')}</label><input type="range" id="camera-tilt" min="10" max="75" value="${state.camera.tilt}">
        ${btn('reset-camera','复位视角')}</div>` : ''}
      <p class="panel-note">${views.length ? surfaceNote : '本工况缺少有效喉后截面面积或多个独立的热损失工况，无法生成三维曲面。'}</p>
    </section>
    ${geometry ? `<section class="panel">
      ${header('基准扩张段截面着色', '半径比由求解器面积比换算。颜色显示各截面的均匀状态。', btn('nozzle-export','截面图 PNG'))}
      ${chart('nozzle-color', `${metric.label} · 喉部至出口`, 'wide-chart', metric)}
      <div class="sample" id="nozzle-sample"><label for="nozzle-index">截面</label><input type="range" id="nozzle-index" min="0" max="1" value="0"><strong id="nozzle-values"></strong></div>
      <p class="panel-note">纵轴 ${m('r/r_{t} = √(A/A_{t})')}。横轴为喉后膨胀进度 ${symbolUnit('ξ_{t}','1')}。</p>
    </section>` : ''}
  </div>`;
  on('#field-variable','change',event=>{state.variable=event.target.value; fieldView();});
  on('#field-mode','change',event=>{state.fieldMode=event.target.value; fieldView();});
  on('#surface-view','change',event=>{state.scene=event.target.value; fieldView();});
  on('#surface-model','change',event=>{state.fieldModel=event.target.value; fieldView();});
  on('#surface-range','click',()=>run(true,'field'));
  const heatCanvas = $('#heatmap');
  const surfaceCanvas = $('#surface');
  const heatIndex = $('#heat-index');
  let hit;
  const drawHeat = () => {
    if (!heatCanvas) return;
    const row = rows[state.heatFocus.row];
    const index = Math.min(Math.max(0, state.heatFocus.index), row.points.length - 1);
    state.heatFocus.index = index;
    heatIndex.max = row.points.length - 1;
    heatIndex.value = index;
    $('#heat-row').value = state.heatFocus.row;
    const point = row.points[index];
    const shown = readCell(point, state.heatFocus.row, index);
    showFormula('#heat-values', `截面 ${point.i} · ${m('ξ')} ${precise(index/(row.points.length-1),3)} · ${m('p/p_{c}')} ${precise(point.p_Pa/row.chamber.p_c_Pa,5)} · ${delta ? 'Δ' : ''}${m(metric.symbol)} ${quantity(shown,metric.unit,4)}`);
    hit = heatmap(heatCanvas, rows, metric, domain, state.heatFocus, { diverging: delta, read: readCell });
  };
  const drawSurface = () => {
    if (!surfaceCanvas) return;
    if (isNozzle) nozzleSurface(surfaceCanvas,geometry,metric,state.camera,state.nozzleFocus);
    else surface(surfaceCanvas,surfaceData,surfaceMetric,surfaceDomain,state.camera);
    $('#camera-angle').value = state.camera.angle;
    $('#camera-tilt').value = state.camera.tilt;
  };
  const nozzleCanvas = $('#nozzle-color');
  const nozzleIndex = $('#nozzle-index');
  let nozzlePlot;
  const drawNozzle = () => {
    if (!nozzleCanvas) return;
    nozzlePlot = nozzleMap(nozzleCanvas, geometry, metric, state.nozzleFocus);
    if (!nozzlePlot) return;
    const { points } = nozzlePlot;
    const point = points[state.nozzleFocus];
    nozzleIndex.max = points.length-1;
    nozzleIndex.value = state.nozzleFocus;
    showFormula('#nozzle-values', `${m('A/A_{t}')} ${precise(point.eps,4)} · ${m('p')} ${quantity(point.p_Pa/1000,'kPa',2)} · ${m(metric.symbol)} ${quantity(metric.read(point),metric.unit,4)}`);
  };
  const updateView = () => { drawHeat(); drawSurface(); drawNozzle(); };
  setRedraw(updateView);
  on('#heat-row','change',event=>{state.heatFocus.row=+event.target.value; drawHeat();});
  on('#heat-index','input',event=>{state.heatFocus.index=event.target.valueAsNumber; drawHeat();});
  heatCanvas?.addEventListener('pointermove',event => {
    const focus = hit?.(event.offsetX,event.offsetY);
    if (focus) { state.heatFocus=focus; drawHeat(); }
  });
  on('#camera-angle','input',event=>{state.camera.angle=event.target.valueAsNumber; drawSurface();});
  on('#camera-tilt','input',event=>{state.camera.tilt=event.target.valueAsNumber; drawSurface();});
  on('#reset-camera','click',()=>{state.camera=initialCamera(); drawSurface();});
  let drag = null;
  let pendingFrame = false;
  surfaceCanvas?.addEventListener('pointerdown',event => {
    surfaceCanvas.setPointerCapture(event.pointerId);
    drag = {x:event.clientX,y:event.clientY, ...state.camera};
  });
  surfaceCanvas?.addEventListener('pointermove',event => {
    if (!drag) return;
    const angle = drag.angle + (event.clientX - drag.x) * .55;
    state.camera = { angle: ((angle + 180) % 360 + 360) % 360 - 180,
      tilt: Math.min(75,Math.max(10,drag.tilt - (event.clientY-drag.y)*.34)) };
    if (!pendingFrame) {
      pendingFrame = true;
      requestAnimationFrame(() => {
        pendingFrame = false;
        if (surfaceCanvas.isConnected) drawSurface();
      });
    }
  });
  surfaceCanvas?.addEventListener('lostpointercapture',()=>{ drag = null; });
  const focusNozzle = index => {
    state.nozzleFocus = index;
    drawNozzle();
    if (isNozzle) drawSurface();
  };
  on('#nozzle-index','input',()=>focusNozzle(nozzleIndex.valueAsNumber));
  nozzleCanvas?.addEventListener('pointermove',event => {
    const index = nozzlePlot?.hit(event.offsetX);
    if (Number.isInteger(index) && index !== state.nozzleFocus) {
      focusNozzle(index);
    }
  });
  bindDownload('#heat-export',()=>png('heatmap',`${metric.label}-彩色剖面`));
  bindDownload('#surface-export',()=>png('surface',`${metric.label}-${surfaceTitle}`));
  bindDownload('#nozzle-export',()=>png('nozzle-color',`${metric.label}-扩张段截面`));
}

function lossTable(record) {
  const gg = record.cycle?.cycle === 'gg';
  const rows = lossFactors.filter(f => !f.cycle || gg).map(f => {
    const cur = state.losses[f.id];
    const cell = key => `<input class="loss-input" type="number" step="${f.step}" data-loss="${f.id}" data-key="${key}" value="${cur[key]}" aria-label="${esc(f.label)} ${key}">`;
    const lam = f.id === 'angle' ? `<br><span class="stat-detail">${m('λ',false)} ${sig(divergence(cur.max),4)} / ${sig(divergence(cur.nom),4)} / ${sig(divergence(cur.min),4)}</span>` : '';
    return [`${esc(f.label)} ${m(f.symbol)}`, cell('min'), cell('nom'), cell('max') + lam,
      `${richHtml(f.note, esc)}<br>${f.url ? link(f.url, f.source) : esc(f.source)}`];
  });
  return fields(['损失因子','下限','名义','上限','依据'], rows);
}

function validationView() {
  let sectionNo = 0;
  const no = () => '①②③④⑤⑥⑦⑧⑨'[sectionNo++];
  const {main, input, public: refs, cases, failed, bounds, analytical, implied, reference, cycle} = state.record;
  const liquid = input.kind === 'liquid';
  const env = envelope(state.record, state.losses, { engineLevel: state.engineLevel });
  const judged = judgePublic(state.record, env, state.losses);
  const caseTags = item => `${esc(item.modelLabel)}${item.heat ? ` · ${m('Q')} ${precise(item.heat,3)}` : ''}`;
  const caseText = id => { const item = cases.find(c => c.id === id); return item ? caseTags(item) : '—'; };

  // ① ideal model interval
  const hasHeat = cases.some(item => item.heat > 0);
  const boundRows = bounds.map(bound => [
    `${esc(bound.label)} ${symbolUnit(resultSymbols[bound.id],bound.unit)}`,
    bound.low === null ? '—' : `${sig(bound.low,6)}<br><span class="stat-detail">${caseText(bound.lowId)}</span>`,
    bound.high === null ? '—' : `${sig(bound.high,6)}<br><span class="stat-detail">${caseText(bound.highId)}</span>`,
    Number.isFinite(bound.low) && bound.high ? ng(100 * (bound.low / bound.high - 1)) : '—',
    ...(hasHeat ? [bound.probes.length ? bound.probes.map(probe => `<div>${caseText(probe.id)} · ${sig(probe.value,6)}</div>`).join('') : '—'] : []),
  ]);
  const idealSection = `<section class="panel">
    ${header(`${no()} 理想热力区间`, '同一推进剂、室压、混合比、面积比下，平衡流与组分冻结流两种膨胀模型给出的理想解边界。', `${btn('calculate-bounds', cases.length ? '重新计算区间' : '计算工况区间')}${cases.length ? btn('range-export','区间 CSV') : ''}`)}
    ${failed.map(item=>`<p class="notice">${esc(item.model)} · ${m('Q')} ${quantity(item.heat,'kJ/kg')} 的计算失败：${esc(item.message)}</p>`).join('')}
    ${boundRows.length ? fields(['指标','下界与工况','上界与工况','下界相对上界',...(hasHeat ? ['中间热损失样点'] : [])], boundRows)
      : '<p class="notice">当前只算了单工况。运行「计算工况区间」以加入冻结流下限。</p>'}
    <p class="panel-note">平衡流是理想上限，冻结流对应化学反应完全来不及进行的极限，真实喷管介于两者之间。这一层只包含热力学模型差异，不含燃烧、扩张、摩擦等实际损失，所以公开实测或设计值通常低于该区间。${hasHeat ? `热损失 ${m('Q')} 为可选敏感性输入：再生冷却发动机的壁面热流大部分回到推进剂，不宜整体计为损失。` : ''}</p>
  </section>`;

  // ② loss factors and ③ delivered envelope
  const fmtLevel = (lvl, id, digits) => lvl ? sig(lvl.nominal[id], digits) : '—';
  const range = (lvl, id, digits) => lvl && Number.isFinite(lvl.low[id]) ? `${sig(lvl.low[id],digits)} – ${sig(lvl.high[id],digits)}` : '—';
  const envRows = env ? envelopeMetrics.map(({id,label,symbol,unit:u}) => [
    `${esc(label)} ${symbolUnit(symbol,u)}`, sig(env.ideal[id],5),
    fmtLevel(env.chamber,id,5), range(env.chamber,id,5),
    ...(env.engine ? [fmtLevel(env.engine,id,5), range(env.engine,id,5)] : []),
  ]) : [];
  const lossSection = liquid ? `<section class="panel">
    ${header(`${no()} 损失因子`, `交付比冲按 ${m('I_{vac} = η_{c*} · λ · η_{BL} · I_{vac,ideal}')} 计算，给定环境比冲再扣除按交付 ${m('c^{∗}')} 换算的背压项。`, `${btn('reset-losses','恢复默认因子')}`)}
    ${cycle ? `<p class="panel-note">${esc(input.preset)} 为${esc(cycle.label)}（${link(cycle.url, cycle.source)}）。</p>` : ''}
    ${cycle?.cycle === 'gg' ? `<label class="check"><input type="checkbox" id="engine-level" ${state.engineLevel ? 'checked' : ''}> 按整机口径计入燃气发生器排气（${m('δI/I ≈ (ṁ_{GG}/ṁ)(1 − c_{GG}/c)')}）；输入流量视为主推力室流量</label>` : ''}
    ${lossTable(state.record)}
    <p class="panel-note">修改后即时重算，无需重新调用求解器。默认值是有出处的工程范围或标明的课程假设，请用课程资料或试验数据覆盖。组合效率 ${m('η_{c*}λη_{BL}')}：${env ? `${sig(env.efficiency.low,4)} / ${sig(env.efficiency.nominal,4)} / ${sig(env.efficiency.high,4)}` : '因子无效'}。</p>
  </section>
  <section class="panel">
    ${header(`${no()} 实际性能包络`, `名义值：平衡流 × 名义因子。包络下限：${env?.frozenIncluded ? '冻结流' : '平衡流（未算冻结流）'} × 各因子最不利值；上限：平衡流 × 各因子最有利值。`, btn('envelope-export','包络 CSV'))}
    ${env ? fields(['指标','理想（平衡流）','推力室名义','推力室包络',...(env.engine ? ['整机名义','整机包络'] : [])], envRows) : '<p class="notice">损失因子无效：须满足 下限 ≤ 名义 ≤ 上限。</p>'}
    ${env && !env.frozenIncluded ? '<p class="notice">尚未计算冻结流，包络下限偏乐观。运行「计算工况区间」后自动补全。</p>' : ''}
  </section>` : `<section class="panel">${header(`${no()} 实际性能包络`)}<p class="panel-note">固体算例暂不提供损失包络。</p></section>`;

  // ④ public values
  const publicRows = judged.map(ref => [
    `${esc(ref.label)}<br><span class="stat-detail">${esc(ref.condition)}</span>`,
    `${quantity(ref.value,ref.unit,ref.digits)}${ref.original ? `<br><span class="stat-detail">${m(ref.original,true)}</span>` : ''}`,
    ref.comparable ? `${sig(ref.nominal,5)}<br><span class="stat-detail">${range(env.engine ?? env.chamber, ref.metric, 4)}</span>` : badge('口径不同'),
    ref.comparable ? ng(ref.deltaNominal) : `<span class="stat-detail">${esc(ref.reason)}</span>`,
    ref.inside === null ? badge('待判定') : badge(ref.inside ? '在包络内' : '超出包络', ref.inside ? 'pass' : 'review'),
    ref.requiredEfficiency ? sig(ref.requiredEfficiency,4) : '—',
    `${esc(ref.basis)}<br>${link(ref.url,ref.source)}${ref.note ? `<br><span class="stat-detail">${richHtml(ref.note, esc)}</span>` : ''}`,
  ]);
  const flowLine = implied ? `<p class="panel-note">流量诊断：公开推力 ÷ 公开比冲 = ${quantity(implied.total,'kg/s',4)}（${esc(implied.condition)}）。${implied.ratio ? `输入的主推力室流量为其 ${sig(100*implied.ratio,4)}%${Math.abs(implied.ratio-1) < .005 ? '，两者同源，推力对比不独立。' : `，相当于假设 ${sig(100*(1-implied.ratio),3)}% 的流量不经过主推力室。`}` : '主推力室流量未知。'}</p>` : '';
  const publicSection = `<section class="panel">
    ${header(`${no()} 公开值对照`, `比较层级：${env?.engine ? '整机（含燃气发生器排气）' : '推力室'}。偏差 = 公开值 / 名义交付值 − ${m('1',true)}。所需综合效率 = 公开比冲 / 理想平衡流比冲。`)}
    ${judged.length ? fields(['指标','公开值','名义交付与包络','相对名义','包络判定','所需综合效率','口径、依据和出处'], publicRows) : '<p class="panel-note">此算例暂无已录入的同型号公开基准。</p>'}
    ${flowLine}
    <details class="advanced"><summary>查看资料及型号口径</summary><ul class="sources">${state.catalog.benchmarkContext.map(item=>`<li>${richHtml(item.description, esc)} ${link(item.url,item.label)}</li>`).join('')}</ul></details>
  </section>`;

  // ⑤ independent reference
  const refNames = {temperature:['燃烧室温度','K'],cstar:['特征速度','m/s'],isp:['给定环境比冲','s'],ispVac:['真空比冲','s'],exitPressure:['出口压强','kPa']};
  const referenceSection = reference ? `<section class="panel">
    ${header(`${no()} 独立代码复核`, `同一工况用 ${esc(reference.tool)} 独立求解（${reference.model === 'eq' ? '平衡流' : '冻结流'}），与 C 求解器对比。`)}
    ${fields(['指标','单位','Cantera','C 求解器','偏差'], reference.rows.map(row => [esc(refNames[row.id][0]), unitCell(refNames[row.id][1]), sig(row.reference,6), sig(row.computed,6), ng(row.deltaPercent)]))}
    <p class="panel-note">两套代码使用同源 NASA 热力数据，但平衡算法和喷管推进独立实现。偏差在 0.1% 量级，说明理想解可信，与公开值的差距来自损失和口径，而非求解错误。</p>
  </section>` : '';

  const idealRows = analytical ? resultKeys.filter(([id])=>Object.hasOwn(analytical,id)).map(([id,label,units,digits]) => {
    const a = analytical[id], b = main.summary[id];
    return [`${label} ${m(resultSymbols[id])}`,unitCell(units),sig(a,5),sig(b,5),Number.isFinite(a) && a !== 0 && Number.isFinite(b) ? ng(100*(b/a-1)) : '—'];
  }) : [];
  panel.innerHTML = `<div class="stack">
    ${idealSection}${lossSection}${publicSection}${referenceSection}
    <section class="panel">
      ${header(`${no()} 定比热理想气体对照`,`以同一室态、出口条件和环境压强独立解等熵关系，${m('γ')} 取室态值。`)}
      ${idealRows.length ? fields(['指标','单位','解析参考','C 数值解','相对解析参考偏差'],idealRows)
        : '<p class="panel-note">当前条件缺少适用的声速堵塞解析参考。</p>'}
      <p class="panel-note">定比热模型忽略离解复合放热，因此数值解比冲高 2–3% 属预期趋势，不是误差。</p>
    </section>
    <section class="panel">${header(`${no()} 基准算例数值检查`, '按求解器容差及界面复算精度检查。', btn('validate-export','完整 JSON'))}${checkTable(main.checks)}</section>
  </div>`;
  on('#calculate-bounds','click',()=>run(true,'validation'));
  on('#reset-losses','click',()=>{ state.losses = defaultLosses(); saveLosses(); validationView(); });
  on('#engine-level','change',event=>{ state.engineLevel = event.target.checked; validationView(); });
  for (const inputEl of panel.querySelectorAll('[data-loss]')) inputEl.addEventListener('change', () => {
    const next = structuredClone(state.losses);
    next[inputEl.dataset.loss][inputEl.dataset.key] = inputEl.valueAsNumber;
    state.losses = next;
    if (validLosses(next)) saveLosses();
    validationView();
  });
  bindDownload('#validate-export',saveJson);
  bindDownload('#range-export',()=>csv('区间工况.csv',
    ['id','role','model','heat_kJ_kg',...resultKeys.map(([id])=>id)],
    [main,...cases].map(item=>[item.id,item.role,item.model,item.heat,...resultKeys.map(([id])=>item.summary[id])])
  ));
  bindDownload('#envelope-export',()=> env && csv('实际性能包络.csv',
    ['metric','unit','ideal_eq','chamber_nominal','chamber_low','chamber_high','engine_nominal','engine_low','engine_high'],
    envelopeMetrics.map(({id,unit:u})=>[id,u,env.ideal[id],env.chamber.nominal[id],env.chamber.low[id],env.chamber.high[id],
      env.engine?.nominal[id] ?? '',env.engine?.low[id] ?? '',env.engine?.high[id] ?? ''])));
}

const comparisonStorage = 'rocket-workbench-comparisons-v1';
function loadComparison() {
  try {
    const records = JSON.parse(localStorage.getItem(comparisonStorage) ?? '[]');
    return Array.isArray(records) ? records.filter(item=>item?.input && item.main?.result?.profile).slice(0,6) : [];
  } catch { return []; }
}

function updateComparisons() {
  show('#compare-count', state.comparisons.length || '');
  try { localStorage.setItem(comparisonStorage, JSON.stringify(state.comparisons)); } catch { /* comparisons remain in memory */ }
}

function addComparison(record) {
  const {input, main, createdAt} = record;
  const same = state.comparisons.findIndex(item=>JSON.stringify(item.input)===JSON.stringify(input));
  if (same >= 0) state.comparisons.splice(same,1);
  state.comparisons = [...state.comparisons.slice(-5), {input, main, createdAt}];
  updateComparisons();
}

function pinCase() {
  addComparison(state.record);
  show('#save-feedback', `已保存 ${caseName(state.record.input)}。可在“工况对比”页查看。`);
}

const courseIds = ['TQ-12B','TQ-15B','YF-100K'];
async function compareCoursePresets() {
  if (state.busy) return;
  state.busy = true;
  $('#run').disabled = $('#run-range').disabled = true;
  $('#course-compare').disabled = $('#pin-current').disabled = true;
  const errors = [];
  try {
    for (const id of courseIds) {
      status(`正在复算对比工况 ${id}…`, 'pending');
      const p = state.catalog.presets.find(item=>item.id===id);
      const input = {...p, kind:'liquid', preset:id, model:'eq', throat:'sonic', points:41, heat:0, heatMax:0, exitMode:'eps', exitValue:p.eps};
      try {
        addComparison(await request('/api/calculate', {
          method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({input,range:false}),
        }));
      } catch (error) { errors.push(`${id} ${error.message}`); }
    }
  } finally {
    state.busy = false;
    $('#run').disabled = $('#run-range').disabled = false;
    status(errors.length ? errors.join('；') : '已将三组课设基准加入工况对比。',errors.length ? 'error' : '');
    render();
  }
}

function compareView() {
  const cases = state.comparisons.map(item=>({
    ...item,
    displayName: `${esc(caseName(item.input))} · ${esc(item.main.modelLabel)} · ${quantity(item.input.pc/1e6,'MPa',2)} · ${m('Q')} ${quantity(item.main.heat,'kJ/kg',2)}`,
  }));
  const charts = profileMetrics.slice(0,4).map(metric=>chart(`plot-${metric.id}`,metric.label,'small-chart',metric)).join('');
  panel.innerHTML = `<div class="stack">
    <section class="panel">
      ${header('工况对比','保留最近六组。以真空比冲和真空推力对齐环境口径。', `${btn('pin-current','加入当前工况')}${btn('course-compare','复算三组课设预设')}${cases.length?btn('compare-export','对比 CSV'):''}`)}
      ${cases.length ? fields(['工况',`环境 ${symbolUnit('p_{a}','kPa')}`,symbolUnit('T_{c}','K'),symbolUnit('c^{∗}','m/s'),`真空 ${symbolUnit('I_{sp}','s')}`,`真空 ${symbolUnit('F','kN')}`,'操作'],cases.map((item,i)=>[
        item.displayName,precise(item.input.pa/1000,3),precise(item.main.summary.temperature,1),precise(item.main.summary.cstar,1),
        precise(item.main.summary.ispVac,2),precise(item.main.summary.thrustVac,2),`<button type="button" data-unpin="${i}">移除</button>`,
      ])) : '<p class="panel-note">加入当前工况，或一键复算天鹊-12B、天鹊-15B 和 YF-100K 系列参考。</p>'}
    </section>
    ${cases.length ? `<section class="panel">${header('剖面叠加','按压强比对齐各工况。每条曲线止于本工况出口。')}${caseLegend(cases)}<div class="chart-grid">${charts}</div></section>` : ''}
  </div>`;
  if (state.busy) $('#pin-current').disabled = $('#course-compare').disabled = true;
  on('#pin-current','click',()=>{addComparison(state.record); compareView();});
  on('#course-compare','click',compareCoursePresets);
  for (const button of panel.querySelectorAll('[data-unpin]')) button.onclick = () => {
    state.comparisons.splice(+button.dataset.unpin,1); updateComparisons(); compareView();
  };
  bindDownload('#compare-export',()=>csv('工况对比.csv',
    ['name','model','heat_kJ_kg','environment_Pa','chamber_Pa','mass_flow_kg_s',...resultKeys.map(([id])=>id)],
    cases.map(item=>[caseName(item.input),item.main.model,item.main.heat,item.input.pa,item.input.pc,item.input.mdot,
      ...resultKeys.map(([id])=>item.main.summary[id])])
  ));
  if (cases.length) profilePlots(cases,'pressure',false);
}

function populate(catalog) {
  const recommended = catalog.presets.filter(item=>courseIds.includes(item.id));
  const others = catalog.presets.filter(item=>!courseIds.includes(item.id));
  const rows = list=>list.map(item=>[item.id,`${item.id} · ${item.name}`]);
  $('#preset').innerHTML = `<optgroup label="课设参考">${options(rows(recommended))}</optgroup>
    <optgroup label="其他预设">${options(rows(others))}</optgroup><option value="">自定义工况</option>`;
  $('#oxidizer').innerHTML = options(catalog.oxidizers.map(item=>[item,item]));
  $('#fuel').innerHTML = options(catalog.fuels.map(item=>[item,item]));
  $('#solid').innerHTML = options(catalog.solids.map(item=>[item,item]),'JPN');
  $('#model').innerHTML = options(Object.entries(catalog.models),'eq');
}

form.addEventListener('submit',event=>{ event.preventDefault(); run(false,state.tab === 'field' ? 'field' : 'overview'); });
form.addEventListener('input',markStale);
on('#run-range','click',()=>run(true,'validation'));
on('#preset','change',event=>{ fillPreset(event.target.value); markStale(); });
on('#exit-mode','change',()=>configureExit(true));
on('#reset','click',()=>{ defaultCase(); markStale(); });
on('#tabs','click',event=>{
  const button = event.target.closest('[data-tab]');
  if (button) setTab(button.dataset.tab);
});
for (const radio of form.querySelectorAll('[name="kind"]')) radio.addEventListener('change',()=>{
  setKind(radio.value);
  if (radio.value === 'solid') {
    const d = {solid:'JPN',tin:298.15,pc:7,mdot:1,pa:101.325,exitMode:'eps',exitValue:12};
    for (const [key,v] of Object.entries(d)) element(key).value = v;
    configureExit();
  } else {
    fillPreset(value('preset') || 'TQ-12B');
  }
  markStale();
});

const themeKey = 'rocket-workbench-theme';
function applyTheme(name, persist = false) {
  document.documentElement.dataset.theme = name;
  $('#theme-toggle')?.setAttribute('aria-label', name === 'dark' ? '切换到浅色主题' : '切换到深色主题');
  if (persist) { try { localStorage.setItem(themeKey, name); } catch { /* session only */ } }
  state.redraw?.();
}
(() => {
  let saved = null;
  try { saved = localStorage.getItem(themeKey); } catch { /* ignore */ }
  applyTheme(saved === 'light' || saved === 'dark' ? saved : 'dark');
})();
on('#theme-toggle', 'click', () => applyTheme(document.documentElement.dataset.theme === 'dark' ? 'light' : 'dark', true));

// Canvas text does not trigger web-font loading; load the formula faces
// explicitly and redraw once they are ready.
if (document.fonts?.load) {
  Promise.all(['16px Formula', 'italic 16px Formula', '16px Formula'].map((spec, i) =>
    document.fonts.load(spec, i === 2 ? '√≈∗' : 'Tpcηλ'))).then(() => state.redraw?.()).catch(() => {});
}

async function init() {
  $('#run').disabled = $('#run-range').disabled = true;
  try {
    state.catalog = await request('/api/presets');
    populate(state.catalog);
    defaultCase();
    state.comparisons = loadComparison();
    updateComparisons();
    show('#connection','本地 C 求解器已连接');
    $('#connection-dot').classList.add('ready');
    await run(true,'overview',false);
  } catch (error) {
    show('#connection','本地求解器连接失败');
    $('#connection-dot').classList.add('error');
    status(error.message, 'error');
    panel.setAttribute('aria-busy','false');
    render();
  }
}
init();
