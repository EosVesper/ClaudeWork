// Hero visual: an animated nozzle cross-section driven by the solver profile.
// Particle speed follows the computed velocity u(x); colour follows T(x).
// Geometry: divergent section uses a 15° conical equivalent of the computed
// area ratios; convergent radii come from the isentropic area–Mach relation;
// the chamber cylinder and the plume beyond the exit are illustrative.

const DIVERGENCE = 15 * Math.PI / 180;
const CONVERGENCE = 32 * Math.PI / 180;
const CONTRACTION = 3.2;            // illustrative chamber-to-throat area ratio
const STOPS = [[255, 250, 238], [255, 214, 140], [255, 142, 64], [226, 64, 74], [120, 30, 92], [40, 18, 60]];

function areaFromMach(mach, gamma) {
  if (!(mach > 0)) return Infinity;
  const g = gamma > 1 ? gamma : 1.2;
  return (1 / mach) * ((2 / (g + 1)) * (1 + (g - 1) / 2 * mach * mach)) ** ((g + 1) / (2 * (g - 1)));
}

export function nozzleModel(result) {
  const { profile, throat, exit, chamber, performance } = result;
  if (!profile?.length || !(exit?.eps > 1)) return null;
  const rc = Math.sqrt(CONTRACTION);
  const lConv = (rc - 1) / Math.tan(CONVERGENCE);
  const lChamber = 1.5 * rc;
  const nodes = [];
  for (const p of [...profile, { ...throat, eps: 1, Ma: 1 }]) {
    const supersonic = p.Ma >= 1;
    const ratio = supersonic ? Math.max(1, p.eps || 1) : Math.min(CONTRACTION, areaFromMach(p.Ma, p.gamma ?? chamber.gamma_c));
    const r = Math.sqrt(ratio);
    const x = supersonic ? (r - 1) / Math.tan(DIVERGENCE) : -(r - 1) / Math.tan(CONVERGENCE);
    nodes.push({ x, r, T: p.T_K, u: p.u_m_s, Ma: p.Ma, p: p.p_Pa });
  }
  // chamber entrance node (u ≈ 0) so the flow starts from rest
  nodes.push({ x: -lConv - lChamber, r: rc, T: chamber.T_c_K, u: 0, Ma: 0, p: chamber.p_c_Pa });
  nodes.sort((a, b) => a.x - b.x);
  const exitNode = nodes.at(-1);
  return {
    nodes, rc, lChamber, xMin: nodes[0].x, xThroat: 0, xExit: exitNode.x, rExit: exitNode.r,
    uMax: Math.max(...nodes.map(n => n.u)), tMax: chamber.T_c_K, tExit: exit.T_K,
    throatRadius: performance?.d_t_m > 0 ? performance.d_t_m / 2 : null,
  };
}

function sample(model, x) {
  const { nodes } = model;
  if (x <= nodes[0].x) return nodes[0];
  if (x >= nodes.at(-1).x) return nodes.at(-1);
  let i = 1;
  while (nodes[i].x < x) i++;
  const a = nodes[i - 1], b = nodes[i];
  const t = (x - a.x) / (b.x - a.x || 1);
  return { r: a.r + (b.r - a.r) * t, T: a.T + (b.T - a.T) * t, u: a.u + (b.u - a.u) * t };
}

function heat(t) {
  const k = Math.min(1, Math.max(0, t)) * (STOPS.length - 1);
  const i = Math.min(STOPS.length - 2, Math.floor(k));
  const f = k - i;
  return STOPS[i].map((c, j) => Math.round(c + (STOPS[i + 1][j] - c) * f));
}

export function createPlume(canvas, { onLayout } = {}) {
  const ctx = canvas.getContext('2d');
  let model = null, particles = [], frame = 0, running = false, visible = true, last = 0, layout = null;
  const reduced = matchMedia('(prefers-reduced-motion: reduce)').matches;

  function resize() {
    const rect = canvas.getBoundingClientRect();
    if (!rect.width) return;
    const ratio = Math.min(devicePixelRatio || 1, 2);
    canvas.width = Math.round(rect.width * ratio);
    canvas.height = Math.round(rect.height * ratio);
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    computeLayout(rect.width, rect.height);
    paintStatic(true);
  }

  function computeLayout(w, h) {
    if (!model) { layout = null; return; }
    const plumeLength = (model.xExit - model.xMin) * 0.5;
    const span = model.xExit - model.xMin + plumeLength;
    const padL = Math.max(28, w * 0.045), padR = 0;
    const band = w < 760 ? 0.5 : 0.62;              // vertical room reserved for the nozzle
    const scale = Math.min((w - padL - padR) / span, (h * band) / (2 * model.rExit * 1.08));
    const cy = h * (w < 760 ? 0.7 : 0.66);
    const x0 = padL - model.xMin * scale;
    layout = { w, h, scale, cy, x0, plumeEnd: model.xExit + plumeLength,
      px: x => x0 + x * scale, py: y => cy - y * scale };
    onLayout?.(layout, model);
  }

  function spawn(fromStart) {
    const x = fromStart ? model.xMin + Math.random() * model.lChamber * .8
      : model.xMin + Math.random() * (layout.plumeEnd - model.xMin);
    return { x, f: (Math.random() * 2 - 1) * 0.94, size: 0.5 + Math.random() * 1.4, twinkle: Math.random() };
  }

  function reset() {
    if (!model || !layout) return;
    const count = Math.round(Math.min(1300, Math.max(420, layout.w * 0.9)));
    particles = Array.from({ length: count }, () => spawn(false));
  }

  function wallPath(sign) {
    const { px, py } = layout;
    ctx.beginPath();
    model.nodes.forEach((n, i) => {
      const X = px(n.x), Y = py(sign * n.r);
      if (i) ctx.lineTo(X, Y); else ctx.moveTo(X, Y);
    });
  }

  function paintStatic(clear) {
    if (!layout) return;
    const { w, h, px, py } = layout;
    if (clear) ctx.clearRect(0, 0, w, h);
    // interior glow: temperature gradient along the axis
    const grad = ctx.createLinearGradient(px(model.xMin), 0, px(model.xExit), 0);
    for (let i = 0; i <= 12; i++) {
      const x = model.xMin + (model.xExit - model.xMin) * i / 12;
      const [r, g, b] = heat(1 - sample(model, x).T / model.tMax * 0.95);
      grad.addColorStop(i / 12, `rgba(${r},${g},${b},0.045)`);
    }
    ctx.beginPath();
    model.nodes.forEach((n, i) => (i ? ctx.lineTo(px(n.x), py(n.r)) : ctx.moveTo(px(n.x), py(n.r))));
    [...model.nodes].reverse().forEach(n => ctx.lineTo(px(n.x), py(-n.r)));
    ctx.closePath();
    ctx.fillStyle = grad;
    ctx.fill();
  }

  function drawWalls() {
    const { px, py } = layout;
    const metal = ctx.createLinearGradient(px(model.xMin), 0, px(model.xExit), 0);
    metal.addColorStop(0, 'rgba(255,190,140,0.95)');
    metal.addColorStop(0.35, 'rgba(255,236,214,0.95)');
    metal.addColorStop(1, 'rgba(160,176,204,0.75)');
    ctx.save();
    ctx.lineWidth = 1.6;
    ctx.strokeStyle = metal;
    ctx.shadowColor = 'rgba(255,140,60,0.55)';
    ctx.shadowBlur = 14;
    for (const sign of [1, -1]) { wallPath(sign); ctx.stroke(); }
    ctx.restore();
    // axis and throat tick
    ctx.save();
    ctx.setLineDash([2, 6]);
    ctx.strokeStyle = 'rgba(255,255,255,0.14)';
    ctx.beginPath(); ctx.moveTo(px(model.xMin), py(0)); ctx.lineTo(px(layout.plumeEnd), py(0)); ctx.stroke();
    ctx.restore();
  }

  function glow(x, y, radius, rgb, alpha) {
    const g = ctx.createRadialGradient(x, y, 0, x, y, radius);
    g.addColorStop(0, `rgba(${rgb},${alpha})`);
    g.addColorStop(1, `rgba(${rgb},0)`);
    ctx.fillStyle = g;
    ctx.fillRect(x - radius, y - radius, radius * 2, radius * 2);
  }

  function drawExhaust() {
    const { px, py, scale } = layout;
    const xe = px(model.xExit), re = model.rExit * scale;
    ctx.save();
    ctx.globalCompositeOperation = 'lighter';
    // luminous core along the axis and an expanding exhaust cone (illustrative)
    const core = ctx.createLinearGradient(px(model.xThroat), 0, px(layout.plumeEnd), 0);
    core.addColorStop(0, 'rgba(255,230,190,0.10)');
    core.addColorStop(0.45, 'rgba(255,130,60,0.07)');
    core.addColorStop(1, 'rgba(255,60,110,0)');
    ctx.fillStyle = core;
    ctx.beginPath();
    ctx.moveTo(px(0), py(0.55));
    ctx.lineTo(xe, py(model.rExit * 0.55));
    ctx.lineTo(px(layout.plumeEnd), py(model.rExit * 1.15));
    ctx.lineTo(px(layout.plumeEnd), py(-model.rExit * 1.15));
    ctx.lineTo(xe, py(-model.rExit * 0.55));
    ctx.lineTo(px(0), py(-0.55));
    ctx.closePath();
    ctx.fill();
    glow(px(0), py(0), scale * 2.6, '255,214,150', 0.16);
    glow(xe, py(0), re * 1.6, '255,110,60', 0.10);
    ctx.restore();
  }

  function step(dt) {
    const { px, py } = layout;
    // fade previous frame for motion trails
    ctx.globalCompositeOperation = 'destination-out';
    ctx.fillStyle = 'rgba(0,0,0,0.30)';
    ctx.fillRect(0, 0, layout.w, layout.h);
    ctx.globalCompositeOperation = 'source-over';
    paintStatic(false);
    drawExhaust();
    ctx.globalCompositeOperation = 'lighter';
    const length = model.xExit - model.xMin;
    const vMax = length / 1.1;                        // exit transit ≈ 1.1 s at u_max
    for (const p of particles) {
      const inside = p.x <= model.xExit;
      const s = inside ? sample(model, p.x) : null;
      const u = inside ? s.u : model.uMax * 0.92;
      const speed = Math.max(0.16, u / model.uMax);   // floor keeps the chamber from clogging
      const v = speed * vMax;
      p.x += v * dt;
      p.f = Math.max(-0.96, Math.min(0.96, p.f + (Math.random() - 0.5) * 0.06 * speed));
      if (p.x > layout.plumeEnd) Object.assign(p, spawn(true));
      let r, T, alpha;
      let size = p.size;
      if (p.x <= model.xExit) {
        const q = sample(model, p.x); r = q.r; T = q.T;
        alpha = 0.18 + 0.8 * Math.sqrt(speed);          // slow chamber gas stays dim
        size *= 0.8 + 0.25 * q.r;
      } else {
        const d = (p.x - model.xExit) / (layout.plumeEnd - model.xExit);
        r = model.rExit * (1 + d * 0.6); T = model.tExit * (1 - d * 0.5); alpha = 0.9 * (1 - d) ** 1.6;
        size *= 1.4 + d;
      }
      const [cr, cg, cb] = heat(1 - T / model.tMax * 0.97);
      const X = px(p.x), Y = py(p.f * r * 0.92);
      ctx.fillStyle = `rgba(${cr},${cg},${cb},${alpha * (0.75 + 0.25 * Math.sin(p.twinkle * 6.28 + X * 0.02))})`;
      ctx.beginPath(); ctx.arc(X, Y, size, 0, Math.PI * 2); ctx.fill();
    }
    ctx.globalCompositeOperation = 'source-over';
    drawWalls();
  }

  function loop(now) {
    if (!running) return;
    const dt = Math.min(0.05, (now - (last || now)) / 1000);
    last = now;
    if (visible && layout) step(dt);
    frame = requestAnimationFrame(loop);
  }

  function start() {
    if (running || !model) return;
    if (reduced) { if (layout) { for (let i = 0; i < 40; i++) step(0.03); } return; }
    running = true; last = 0; frame = requestAnimationFrame(loop);
  }
  function stop() { running = false; cancelAnimationFrame(frame); }

  const observer = new IntersectionObserver(entries => { visible = entries[0].isIntersecting; });
  observer.observe(canvas);
  document.addEventListener('visibilitychange', () => { visible = !document.hidden; });
  new ResizeObserver(() => { resize(); reset(); }).observe(canvas);

  return {
    setResult(result) {
      model = nozzleModel(result);
      stop();
      resize();
      reset();
      start();
    },
  };
}
