import { mathFont, mathText, numericSource, unitSource } from './math.mjs';

// Chart colours come from CSS custom properties so the light and dark
// themes share one drawing path. Outside a browser (npm test) the dark
// values below are used.
const fallback = {
  bg: '#0f1218', grid: 'rgba(255,255,255,0.07)', text: '#8c96a8', strong: '#e9edf3', focus: '#e9edf3',
  mesh: 'rgba(8,10,14,0.55)', ring: 'rgba(255,255,255,0.18)',
  series: ['#6ca8ff', '#f0883e', '#3cc8b4', '#a98bff', '#f27a9b', '#a8c96b'],
  axes: ['#6ca8ff', '#f0b45e', '#a98bff'],
};
export function theme() {
  if (typeof document === 'undefined' || typeof getComputedStyle === 'undefined') return fallback;
  const css = getComputedStyle(document.documentElement);
  const read = (name, value) => css.getPropertyValue(name).trim() || value;
  return {
    bg: read('--chart-bg', fallback.bg), grid: read('--chart-grid', fallback.grid),
    text: read('--chart-text', fallback.text), strong: read('--chart-strong', fallback.strong),
    focus: read('--chart-focus', fallback.focus), mesh: read('--chart-mesh', fallback.mesh),
    ring: read('--chart-ring', fallback.ring),
    series: fallback.series.map((value, i) => read(`--series-${i + 1}`, value)),
    axes: ['x', 'y', 'z'].map((axis, i) => read(`--axis-${axis}`, fallback.axes[i])),
  };
}
// Live view of the current series palette (index access and length).
export const colors = new Proxy([], {
  get(_, key) {
    const series = theme().series;
    return key === 'length' ? series.length : series[key];
  },
});
const font = '"Inter","HarmonyOS Sans SC","PingFang SC","Microsoft YaHei UI","Noto Sans SC",system-ui,sans-serif';

export function format(value, digits = 2) {
  if (!Number.isFinite(value)) return '—';
  return value.toLocaleString('zh-CN', { maximumFractionDigits: digits });
}

function frame(canvas) {
  const rect = canvas.getBoundingClientRect();
  if (!rect.width || !rect.height) return null;
  const ratio = Math.min(window.devicePixelRatio || 1, 2.5);
  const width = Math.round(rect.width * ratio);
  const height = Math.round(rect.height * ratio);
  if (canvas.width !== width) canvas.width = width;
  if (canvas.height !== height) canvas.height = height;
  const ctx = canvas.getContext('2d');
  ctx.setTransform(width / rect.width, 0, 0, height / rect.height, 0, 0);
  const t = theme();
  ctx.fillStyle = t.bg;
  ctx.fillRect(0, 0, rect.width, rect.height);
  return { ctx, w: rect.width, h: rect.height, t };
}

export function caption(ctx, value, x, y, align = 'left', fill = theme().text, size = 12) {
  const runs = (Array.isArray(value) ? value.flat(Infinity) : [value])
    .map(run => typeof run === 'string' ? { text: run } : run);
  const formula = run => run.math && (run.italic || run.formula);
  const fontFor = run => `${run.italic ? 'italic ' : ''}${(run.level ? size * .74 : size) * (formula(run) ? 1.08 : 1)}px ${formula(run) ? mathFont : font}`;
  const widths = runs.map(run => {
    ctx.font = fontFor(run);
    return ctx.measureText(run.text).width;
  });
  const width = widths.reduce((sum, part) => sum + part, 0);
  let left = x - (align === 'center' ? width / 2 : align === 'right' ? width : 0);
  ctx.fillStyle = fill;
  ctx.textAlign = 'left';
  ctx.textBaseline = 'alphabetic';
  for (const [index, run] of runs.entries()) {
    ctx.font = fontFor(run);
    const shift = run.level === 'sup' ? -.39 * size : run.level === 'sub' ? .22 * size : 0;
    ctx.fillText(run.text, left, y + .32 * size + shift);
    left += widths[index];
  }
}

const num = (value, digits = 3) => mathText(numericSource(value, digits), true);
const unit = value => mathText(unitSource(value), true);
const bracket = value => [mathText(' [', true), unit(value), mathText(']', true)];
const axisLabel = (symbol, units) => [mathText(symbol), bracket(units)];

function segment(ctx, x1, y1, x2, y2, color, width = 1, dash = []) {
  ctx.strokeStyle = color;
  ctx.lineWidth = width;
  ctx.setLineDash(dash);
  ctx.beginPath();
  ctx.moveTo(x1, y1);
  ctx.lineTo(x2, y2);
  ctx.stroke();
  ctx.setLineDash([]);
}

function dot(ctx, x, y, size, fill) {
  ctx.fillStyle = fill;
  ctx.beginPath();
  ctx.arc(x, y, size, 0, Math.PI * 2);
  ctx.fill();
}

export function extent(values) {
  const finite = values.filter(Number.isFinite);
  return finite.length ? [Math.min(...finite), Math.max(...finite)] : [0, 1];
}

function linearTicks([min, max], target) {
  const width = max - min || Math.max(1, Math.abs(max) * 0.04);
  const rough = width / target;
  const base = 10 ** Math.floor(Math.log10(rough));
  const step = ([1, 2, 2.5, 5, 10].find(unit => unit * base >= rough) ?? 10) * base;
  const lower = Math.floor(min / step) * step - (min === max ? step : 0);
  const upper = Math.max(Math.ceil(max / step) * step, lower + step);
  return Array.from({ length: Math.round((upper - lower) / step) + 1 },
    (_, index) => +(lower + index * step).toPrecision(10));
}

export function linearChart(canvas, opts) {
  const layout = frame(canvas);
  if (!layout) return null;
  const { ctx, w, h, t } = layout;
  const grid = t.grid;
  const valid = opts.series.flatMap(line => line.points.filter(point => point.every(Number.isFinite)));
  if (!valid.length) {
    caption(ctx, '当前工况无可绘制数据', w / 2, h / 2, 'center');
    return null;
  }
  const xt = opts.xTicks || linearTicks(extent(valid.map(p => p[0])), 5);
  const yt = opts.yTicks || linearTicks(extent(valid.map(p => p[1])), 4);
  const left = 56, right = w - 17, top = 15, bottom = h - 40;
  const [x0, x1] = opts.xDomain ?? [xt[0], xt.at(-1)];
  const px = x => left + (x - x0) * (right - left) / (x1 - x0);
  const py = y => bottom - (y - yt[0]) * (bottom - top) / (yt.at(-1) - yt[0]);
  for (const tick of yt) {
    segment(ctx, left, py(tick), right, py(tick), grid);
    caption(ctx, opts.yFormat?.(tick) ?? num(tick), left - 8, py(tick), 'right');
  }
  for (const tick of xt) {
    segment(ctx, px(tick), top, px(tick), bottom, grid);
    caption(ctx, opts.xFormat?.(tick) ?? num(tick), px(tick), bottom + 13, 'center');
  }
  caption(ctx, opts.xLabel, (left + right) / 2, h - 9, 'center');

  ctx.save();
  ctx.beginPath();
  const reveal = Math.min(1, Math.max(0, opts.reveal ?? 1));
  ctx.rect(left - 1, top - 1, (right - left) * reveal + 2, bottom - top + 2);
  ctx.clip();
  for (const mark of opts.refs ?? []) {
    if (Number.isFinite(mark.x)) segment(ctx, px(mark.x), top, px(mark.x), bottom, mark.color, 1, [3, 4]);
    if (Number.isFinite(mark.y)) segment(ctx, left, py(mark.y), right, py(mark.y), mark.color, 1, [4, 4]);
  }
  for (const line of opts.series) {
    ctx.strokeStyle = line.color;
    ctx.lineWidth = line.width ?? 2;
    ctx.setLineDash(line.dash ?? []);
    ctx.beginPath();
    let drawing = false;
    for (const [x, y] of line.points) {
      if (!Number.isFinite(x) || !Number.isFinite(y)) {
        drawing = false;
        continue;
      }
      if (drawing) ctx.lineTo(px(x), py(y));
      else ctx.moveTo(px(x), py(y));
      drawing = true;
    }
    ctx.stroke();
    if (line.dots) {
      for (const [x, y] of line.points) {
        if (Number.isFinite(x) && Number.isFinite(y)) dot(ctx, px(x), py(y), 2.35, line.color);
      }
    }
  }
  ctx.setLineDash([]);
  if (opts.focus?.every(Number.isFinite)) {
    const x = px(opts.focus[0]);
    const y = py(opts.focus[1]);
    segment(ctx, x, top, x, bottom, t.focus, 1, [2, 3]);
    dot(ctx, x, y, 3.5, t.focus);
  }
  for (const mark of opts.refs ?? []) {
    if (Number.isFinite(mark.x) && Number.isFinite(mark.dot)) dot(ctx, px(mark.x), py(mark.dot), 3.25, mark.color);
  }
  ctx.restore();
  return {
    fromPixel: x => x0 + (x - left) * (x1 - x0) / (right - left),
    hit: (x, y) => x >= left && x <= right && y >= top && y <= bottom,
  };
}

const ramp = [[40,30,92], [101,42,130], [178,58,110], [236,110,72], [252,206,118]];
const divergingRamp = [[52,110,214], [124,165,230], [206,212,222], [240,150,98], [210,76,52]];
// Colour scale: log for quantities spanning decades (p, ρ), symmetric
// diverging for differences against a baseline.
export function colorScaleOf(metric, domain, diverging = false) {
  const log = Boolean(metric?.log) && !diverging && domain[0] > 0 && domain[1] > domain[0];
  const f = log ? Math.log10 : v => v;
  const [low, high] = diverging ? [-Math.max(Math.abs(domain[0]), Math.abs(domain[1])), Math.max(Math.abs(domain[0]), Math.abs(domain[1]))] : domain;
  return { log, diverging, domain: [low, high], stops: diverging ? divergingRamp : ramp,
    t: value => high > low ? Math.min(1, Math.max(0, (f(value) - f(low)) / (f(high) - f(low)))) : 0.5 };
}
function heatColor(value, scale) {
  const stops = scale.stops ?? ramp;
  const t = scale.t ? scale.t(value) : 0.5;
  const i = Math.min(stops.length - 2, Math.floor(t * (stops.length - 1)));
  const mix = t * (stops.length - 1) - i;
  return `rgb(${stops[i].map((component, c) => Math.round(component + (stops[i+1][c] - component) * mix)).join(',')})`;
}

const progressTicks = [0, .25, .5, .75, 1];
function colorScale(ctx, scale, unit, left, right, y) {
  const start = left + Math.min(59, (right-left)*.21);
  const end = right - Math.min(130, (right-left)*.3);
  const gradient = ctx.createLinearGradient(start, y, end, y);
  scale.stops.forEach((rgb,i)=>gradient.addColorStop(i/(scale.stops.length-1),`rgb(${rgb.join(',')})`));
  ctx.fillStyle = gradient;
  ctx.fillRect(start, y - 3, Math.max(1,end-start), 7);
  const [low, high] = scale.domain;
  caption(ctx, num(low), start-6, y, 'right');
  const tag = scale.log ? ' · 对数色标' : scale.diverging ? ' · 差值' : '';
  caption(ctx, [num(high), ' ', mathText(unitSource(unit), true), tag], end+6, y);
}

export function heatmap(canvas, rows, metric, domain, focus, opts = {}) {
  const scale = colorScaleOf(metric, domain, opts.diverging);
  const read = opts.read ?? metric.read;
  const layout = frame(canvas);
  if (!layout || !rows.length) return null;
  const { ctx, w, h, t } = layout;
  const grid = t.grid, text = t.text;
  const left = Math.min(147, w * .31), right = w - 21, top = 16, bottom = h - 68;
  const rowHeight = (bottom - top) / rows.length;
  rows.forEach((row, j) => {
    const n = row.points.length;
    const y = top + j * rowHeight;
    const label = w < 510 ? row.shortLabel : row.modelLabel;
    caption(ctx, [label, ' ', mathText('Q'), ' ', num(row.heat, 2)], left - 8, y + rowHeight / 2, 'right', text, w < 510 ? 11 : 12);
    const interval = (right - left) / (n - 1 || 1);
    row.points.forEach((point, i) => {
      const x = left + i * interval;
      const x0 = Math.max(left, x - interval / 2);
      const x1 = Math.min(right, x + interval / 2);
      const value = read(point, j, i);
      ctx.fillStyle = Number.isFinite(value) ? heatColor(value, scale) : grid;
      ctx.fillRect(x0, y, x1 - x0 + 0.5, rowHeight - 2);
      if (focus?.row === j && focus.index === i) {
        ctx.strokeStyle = t.focus;
        ctx.lineWidth = 1.5;
        ctx.strokeRect(x0, y + 1, Math.max(1, x1 - x0), rowHeight - 4);
      }
    });
  });
  for (const value of progressTicks) {
    const x = left + value * (right - left);
    segment(ctx, x, bottom, x, bottom + 5, t.text);
    caption(ctx, num(value), x, bottom + 16, 'center');
  }
  caption(ctx, ['归一化膨胀进度 ', axisLabel('ξ', '1')], (left + right) / 2, h - 33, 'center');
  colorScale(ctx,scale,metric.unit,left,right,h-13);
  return (x, y) => {
    if (x < left || x > right || y < top || y >= bottom) return null;
    const row = Math.floor((y - top) / rowHeight);
    const index = Math.round((x - left) * (rows[row].points.length - 1) / (right - left));
    return { row, index };
  };
}

export function nozzleGeometry({ throat, exit, profile, performance }) {
  const { p_Pa: throatPressure } = throat;
  const { p_Pa: exitPressure } = exit;
  if (profile.length < 2 || !(exitPressure > 0 && exitPressure < throatPressure && exit.eps > 0)) return null;
  const points = [
    { ...throat, eps: 1 },
    ...profile.filter(p=>p.p_Pa < throatPressure * (1-1e-9)
      && p.p_Pa > exitPressure * (1+1e-9) && p.eps > 0),
    exit,
  ].map(point => ({
    ...point, radiusRatio: Math.sqrt(point.eps),
    progress: point.p_Pa === throatPressure ? 0
      : Math.log(point.p_Pa / throatPressure) / Math.log(exitPressure / throatPressure),
  }));
  const diameter = performance?.d_t_m;
  const throatRadius = Number.isFinite(diameter) && diameter > 0 ? diameter / 2 : null;
  return { points, throatPressure, exitPressure, throatRadius };
}

export function nozzleMap(canvas, geometry, metric, focused) {
  const layout = frame(canvas);
  if (!layout) return null;
  const { ctx, w, h, t } = layout;
  const grid = t.grid, text = t.text;
  if (!geometry) {
    caption(ctx,'当前算例缺少喉后有效面积比分布',w/2,h/2,'center');
    return null;
  }
  const { points } = geometry;
  const x0 = 51, x1 = w-22, mid = (h-60)/2;
  const axisY = h-70;
  const radialMax = Math.max(...points.map(point=>point.radiusRatio));
  const xOf = p => x0 + (x1-x0)*p.progress;
  const yOf = p => (mid-25)*p.radiusRatio/radialMax;
  const values = points.map(metric.read);
  const scale = colorScaleOf(metric, extent(values));
  const nodes = points.map((p,i)=>({x:xOf(p), y:yOf(p), v:values[i]}));
  ctx.save();
  ctx.beginPath();
  ctx.moveTo(nodes[0].x,mid-nodes[0].y);
  for (const node of nodes.slice(1)) ctx.lineTo(node.x,mid-node.y);
  for (const node of nodes.toReversed()) ctx.lineTo(node.x,mid+node.y);
  ctx.closePath();
  ctx.clip();
  nodes.forEach((p,i)=>{
    const left = i === 0 ? p.x : (p.x+nodes[i-1].x)/2;
    const right = i === nodes.length-1 ? p.x : (p.x+nodes[i+1].x)/2;
    ctx.fillStyle = Number.isFinite(p.v) ? heatColor(p.v,scale) : grid;
    ctx.fillRect(left,0,right-left+0.5,mid*2);
  });
  segment(ctx,x0,mid,x1,mid,t.ring,.8,[6,5]);
  if (nodes[focused]) {
    const node = nodes[focused];
    segment(ctx,node.x,mid-node.y,node.x,mid+node.y,t.focus,1.3,[3,3]);
  }
  ctx.restore();
  segment(ctx,x0,25,x0,mid*2-25,t.text);
  caption(ctx,mathText('r/r_{t}'),x0-8,9,'right',text,11);
  for (const tick of [-1,0,1]) {
    const py = mid-tick*(mid-25);
    caption(ctx,num(tick*radialMax,2),x0-8,py,'right');
  }
  segment(ctx,x0,axisY,x1,axisY,grid);
  progressTicks.forEach(tick=>{
    const x=x0+tick*(x1-x0);
    segment(ctx,x,axisY,x,axisY+4,text);
    caption(ctx,num(tick),x,axisY+13,'center');
  });
  caption(ctx,['喉后膨胀进度 ', axisLabel('ξ_{t}', '1')],(x0+x1)/2,h-37,'center');
  colorScale(ctx,scale,metric.unit,x0,x1,h-14);
  return {
    points,
    hit: x => {
      if (x < x0 || x > x1) return null;
      return nodes.reduce((best,p,i) => Math.abs(p.x-x)<Math.abs(nodes[best].x-x) ? i : best,0);
    },
  };
}

function fitProjection(w, h, camera, axes) {
  const az = camera.angle * Math.PI / 180;
  const el = camera.tilt * Math.PI / 180;
  const projection = (x, y, z) => {
    const d = Math.sin(az) * x + Math.cos(az) * y;
    return [
      Math.cos(az) * x - Math.sin(az) * y,
      d * Math.sin(el) - z * Math.cos(el),
      d * Math.cos(el) + z * Math.sin(el),
    ];
  };
  const [xs, ys, zs] = axes;
  const cube = xs.flatMap(x=>ys.flatMap(y=>zs.map(z=>projection(x,y,z))));
  const [minX,maxX] = extent(cube.map(point=>point[0]));
  const [minY,maxY] = extent(cube.map(point=>point[1]));
  const scale = Math.min(Math.max(25,w-136)/(maxX-minX), (h-110)/(maxY-minY));
  const originX = w*.5 - scale*(minX+maxX)/2;
  const originY = (h-27)*.5 - scale*(minY+maxY)/2;
  return (x,y,z) => {
    const [a,b,d] = projection(x,y,z);
    return [originX+a*scale, originY+b*scale, d];
  };
}

function polygon(ctx, points, fill, stroke, width = .55) {
  ctx.beginPath();
  ctx.moveTo(points[0][0], points[0][1]);
  for (const [x,y] of points.slice(1)) ctx.lineTo(x,y);
  ctx.closePath();
  ctx.fillStyle = fill;
  ctx.fill();
  ctx.strokeStyle = stroke;
  ctx.lineWidth = width;
  ctx.stroke();
}

const surfaceName = metric => [metric.label, ' ', axisLabel(metric.symbol, metric.unit)];

export function nozzleSurface(canvas, geometry, metric, camera, focused) {
  const layout = frame(canvas);
  if (!layout || !geometry) return;
  const { ctx, w, h, t } = layout;
  const grid = t.grid, axesColors = t.axes;
  const { points, throatRadius, throatPressure, exitPressure } = geometry;
  const maximumRatio = Math.max(...points.map(point => point.radiusRatio));
  const limit = .65;
  const depthAxis = -.82;
  const length = 3.45;
  const project = fitProjection(w,h,camera,[[-length/2,length/2+.26],[-.9,.88],[-1,.8]]);
  const nose = -length/2;
  const tail = length/2;
  const xOf = fraction => nose + length*fraction;
  const radialUnit = throatRadius === null ? '1' : 'm';
  const radialScale = maximumRatio * (throatRadius ?? 1);
  const symbols = throatRadius === null ? ['y/r_{t}','z/r_{t}'] : ['y','z'];
  const scale = colorScaleOf(metric, extent(points.map(metric.read)));
  const nPhi = 32;
  const rings = points.map(point => {
    const x = xOf(point.progress);
    const radius = limit * point.radiusRatio / maximumRatio;
    return Array.from({length:nPhi}, (_, i)=>{
      const angle = 2*Math.PI*i/nPhi;
      return project(x,radius*Math.cos(angle),radius*Math.sin(angle));
    });
  });
  const faces = [];
  for (let j = 1; j < points.length; j++) {
    const left = rings[j-1], right = rings[j];
    const band = (metric.read(points[j-1])+metric.read(points[j]))/2;
    for (let i = 0; i < nPhi; i++) {
      const next = (i+1)%nPhi;
      const corners = [left[i],left[next],right[next],right[i]];
      const depth = corners.reduce((sum,c)=>sum+c[2],0)/4;
      faces.push({ corners, depth, value:band, selected:j-1===focused || j===focused, band:j });
    }
  }
  for (const face of faces.sort((a,b)=>a.depth-b.depth)) {
    polygon(ctx, face.corners, Number.isFinite(face.value) ? heatColor(face.value,scale) : grid,
      face.selected ? t.focus : face.band % 4 === 0 ? t.mesh : t.grid);
  }

  const between = (a,b,color,width=1) => segment(ctx,a[0],a[1],b[0],b[1],color,width);
  const zAxis = z => project(tail+.26,.81,z);
  const yAxis = y => project(tail+.26,y,depthAxis-.1);
  const pAxis = fraction => project(xOf(fraction),depthAxis,depthAxis);
  between(pAxis(0),pAxis(1),axesColors[0],1.35);
  between(yAxis(-limit),yAxis(limit),axesColors[1],1.35);
  between(zAxis(-limit),zAxis(limit),axesColors[2],1.35);
  for (const fraction of [0,.5,1]) {
    const [x,y] = pAxis(fraction);
    const p = throatPressure / 1e6 * (exitPressure/throatPressure)**fraction;
    segment(ctx,x,y,x,y+5,axesColors[0]);
    caption(ctx,num(p),x,y+15,'center',axesColors[0]);
  }
  for (const factor of [-1,0,1]) {
    const [x,y] = yAxis(factor*limit);
    const [a,b] = zAxis(factor*limit);
    segment(ctx,x,y,x+5,y,axesColors[1]);
    segment(ctx,a,b,a+5,b,axesColors[2]);
    caption(ctx,num(factor*radialScale),x+7,y,'left',axesColors[1]);
    caption(ctx,num(factor*radialScale),a+7,b,'left',axesColors[2]);
  }
  caption(ctx,['喉部 ',mathText('p_{t}'),' → 出口 ',mathText('p_{e}'),' · 轴向按对数压强排布，非真实长度'],14,17);
  caption(ctx,surfaceName(metric),w-14,17,'right');
  const labelSize = w < 480 ? 11 : 13;
  const [px,py] = pAxis(.25);
  const [yx,yy] = yAxis(limit);
  const [zx,zy] = zAxis(limit);
  caption(ctx,[axisLabel('p','MPa'),' 对数'],px,py+29,'center',axesColors[0],labelSize);
  caption(ctx,axisLabel(symbols[0],radialUnit),yx,yy+15,'center',axesColors[1],labelSize);
  caption(ctx,axisLabel(symbols[1],radialUnit),zx,zy-15,'center',axesColors[2],labelSize);
  colorScale(ctx,scale,metric.unit,Math.max(12,w*.17),Math.min(w-12,w*.83),h-15);
}

export function surface(canvas, rows, metric, domain, camera) {
  const layout = frame(canvas);
  if (!layout || rows.length < 2) return;
  const { ctx, w, h, t } = layout;
  const grid = t.grid, axesColors = t.axes;
  const [low, high] = domain;
  const scale = colorScaleOf(metric, domain);
  const heatMin = rows[0].heat;
  const heatMax = rows.at(-1).heat;
  const projection = fitProjection(w,h,camera,[[-.7,.7],[-.44,.44],[0,.96]]);
  const project = (x,y,z) => projection((x-.5)*1.4,(y-.5)*.88,z*.96);
  const between = (a, b, color, width) => segment(ctx, a[0], a[1], b[0], b[1], color, width);
  for (const tick of progressTicks) {
    between(project(tick, 0, 0), project(tick, 1, 0), grid);
    between(project(0, tick, 0), project(1, tick, 0), grid);
  }
  const zOf = value => high > low ? (value - low) / (high - low) : .5;
  const coordinates = rows.map(row => row.points.map((point, index) => {
    const x = index / (row.points.length - 1);
    const y = (row.heat - heatMin) / (heatMax - heatMin);
    const v = metric.read(point);
    return { point: project(x, y, zOf(v)), value: v };
  }));
  const polygons = [];
  for (let j = 1; j < coordinates.length; j++) {
    const a = coordinates[j - 1], b = coordinates[j];
    if (a.length !== b.length || rows[j - 1].heat === rows[j].heat) continue;
    for (let i = 1; i < a.length; i++) {
      const quad = [a[i - 1], a[i], b[i], b[i - 1]];
      if (quad.every(p => Number.isFinite(p.value))) polygons.push(quad);
    }
  }
  polygons.sort((a,b) => a.reduce((sum,p)=>sum+p.point[2],0) - b.reduce((sum,p)=>sum+p.point[2],0));
  for (const quad of polygons) {
    polygon(ctx,quad.map(p=>p.point),heatColor(quad.reduce((sum,p)=>sum+p.value,0)/4,scale),t.mesh);
  }
  for (const row of coordinates) {
    for (let i = 1; i < row.length; i++) {
      if (Number.isFinite(row[i-1].value) && Number.isFinite(row[i].value)) {
        between(row[i-1].point, row[i].point, t.strong, .9);
      }
    }
    if (row.length) {
      for (let i = 0; i < row.length; i += Math.max(1, Math.floor(row.length / 10))) {
        const { point, value } = row[i];
        if (Number.isFinite(value)) dot(ctx, point[0], point[1], 1.8, t.strong);
      }
    }
  }
  between(project(0,0,0), project(1,0,0), axesColors[0]);
  between(project(0,0,0), project(0,1,0), axesColors[1]);
  between(project(0,0,0), project(0,0,1), axesColors[2]);
  for (const tick of [0, .5, 1]) {
    const [x,y] = project(tick,0,0);
    caption(ctx,num(tick),x+8,y+13,'center',axesColors[0]);
    const [u,v] = project(0,tick,0);
    caption(ctx,num(heatMin+(heatMax-heatMin)*tick),u-11,v+8,'right',axesColors[1]);
    const [a,b] = project(0,0,tick);
    if (low !== high || tick === .5) caption(ctx,num(low+(high-low)*tick),a-8,b-7,'right',axesColors[2]);
  }
  caption(ctx,surfaceName(metric),14,17);
  caption(ctx,axisLabel('ξ','1'),w*.2,h-40,'center',axesColors[0]);
  caption(ctx,axisLabel('Q','kJ/kg'),w*.5,h-40,'center',axesColors[1]);
  caption(ctx,axisLabel(metric.symbol,metric.unit),w*.81,h-40,'center',axesColors[2]);
  colorScale(ctx,scale,metric.unit,Math.max(12,w*.17),Math.min(w-12,w*.83),h-15);
}
