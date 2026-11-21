export const gravity = 9.80665;

export const metrics = [
  { id: 'temperature', label: '燃烧室温度', unit: 'K', digits: 1, read: r => r.chamber.T_c_K },
  { id: 'cstar', label: '特征速度', unit: 'm/s', digits: 1, read: r => r.performance.c_star_m_s },
  { id: 'isp', label: '给定环境比冲', unit: 's', digits: 2, read: r => r.performance.Isp_m_s / gravity },
  { id: 'ispVac', label: '真空比冲', unit: 's', digits: 2, read: r => r.performance.Isp_vac_m_s / gravity },
  { id: 'thrust', label: '给定环境推力', unit: 'kN', digits: 2, read: r => r.performance.m_dot_kg_s > 0 ? r.performance.F_N / 1000 : null },
  { id: 'thrustVac', label: '真空推力', unit: 'kN', digits: 2, read: r => r.performance.m_dot_kg_s > 0 ? r.performance.F_vac_N / 1000 : null },
  { id: 'exitPressure', label: '出口压强', unit: 'kPa', digits: 2, read: r => r.exit.p_Pa / 1000 },
  { id: 'exitMach', label: '出口马赫数', unit: '1', digits: 3, read: r => r.exit.Ma },
];

const landspace = 'https://www.landspace.com/news-detail.html?itemid=46';
const sse = 'https://static.sse.com.cn/stock/disclosure/announcement/c/202512/002174_20251231_1QK0.pdf#page=211';
const caltPaper = 'https://castjournals.cast.org.cn/joweb/qzkj/EN/1241716665260495476';
// Public values. `basis` states what the number describes so the UI never
// compares a main-chamber model against a whole-engine figure silently.
const benchmarks = {
  'TQ-12B': [
    { metric: 'thrust', value: 1000, condition: '海平面 · 单台整机', basis: '设计指标（2023 年型号发布；招股书表列 102 吨）', source: '蓝箭航天型号发布', url: landspace },
    { metric: 'isp', value: 293, condition: '海平面 · 单台整机', basis: '招股书「根据公开数据整理」，与 TQ-12A 同列 293 s；B 型处于整机试车阶段', source: '蓝箭航天招股书 1-1-210 页', url: sse },
  ],
  'TQ-15B': [
    { metric: 'thrustVac', value: 1183, condition: '真空 · 单台整机', basis: '设计指标（2023 年型号发布；遥一飞行使用 TQ-15A）', source: '蓝箭航天型号发布', url: landspace },
    { metric: 'ispVac', value: 3500 / gravity, original: '3500 m/s', condition: '真空 · 单台整机', basis: '设计指标（2023 年型号发布）', source: '蓝箭航天型号发布', url: landspace },
  ],
  'YF-100K': [
    { metric: 'thrust', value: 1250, condition: '海平面 · 单台整机', basis: '系列文献值（未取得原文逐字核对）', source: '李斌（2022）表 1', url: caltPaper },
    { metric: 'isp', value: 302, condition: '海平面 · 单台整机', basis: '系列文献值（未取得原文逐字核对）', source: '李斌（2022）表 1', url: caltPaper },
  ],
};

// Engine cycle per preset. A gas-generator engine dumps turbine exhaust, so
// whole-engine Isp is below main-chamber Isp; staged combustion routes all
// flow through the main chamber.
export const engineCycles = {
  'TQ-12B': { cycle: 'gg', label: '燃气发生器循环', source: '蓝箭航天招股书 1-1-210 页', url: sse },
  'TQ-15B': { cycle: 'gg', label: '燃气发生器循环', source: '天鹊系列同属燃气发生器循环（招股书 1-1-210 页）', url: sse },
  'YF-100K': { cycle: 'staged', label: '高压补燃（富氧分级燃烧）循环', source: '招股书 1-1-210 页 YF-100 列', url: sse },
};

// Independent check of the ideal solution: Cantera 3.2 (NASA-9 gas data),
// tools/cantera_crosscheck.py, liquid reactant enthalpies from thermo.inp.
export const independentReference = {
  tool: 'Cantera 3.2 · tools/cantera_crosscheck.py',
  cases: {
    'TQ-12B': { eq: { temperature: 3499.9, cstar: 1876.98, isp: 323.82, ispVac: 352.22, exitPressure: 70.47 }, fc: { cstar: 1847.79, isp: 312.77, ispVac: 340.72, exitPressure: 62.81 } },
    'TQ-15B': { eq: { temperature: 3499.9, cstar: 1876.98, isp: 367.66, ispVac: 367.66, exitPressure: 22.17 }, fc: { cstar: 1847.79, isp: 354.04, ispVac: 354.04, exitPressure: 19.02 } },
    'YF-100K': { eq: { temperature: 3817.5, cstar: 1811.96, isp: 321.11, ispVac: 356.15, exitPressure: 52.29 }, fc: { cstar: 1775.15, isp: 300.92, ispVac: 335.24, exitPressure: 41.12 } },
  },
};

export const benchmarkContext = [
  { description: '朱雀三号遥一（2025-12）采用天鹊 12A/15A；此处 B 型数据为 2023 年发布的设计指标。', label: '蓝箭航天遥一通报', url: 'https://www.landspace.com/news-detail.html?itemid=66' },
  { description: '招股书第 1-1-210 页列出天鹊-12B：燃气发生器循环，海平面推力 102 吨，海平面比冲 293 秒，混合比 2.9，进展为整机试车。', label: '蓝箭航天招股书', url: sse },
  { description: '长征十号乙首飞公开的起飞推力属于整箭。YF-100K 比对采用系列文献的单机口径。', label: '火箭院长十乙首飞通报', url: 'https://calt.spacechina.com/n689/c2573694/content.html' },
  { description: '燃气发生器循环比冲损失 δIsp/Isp ≈ (ṁ_GG/ṁ)(1 − c_GG/c)；F-1 算例约 1.1%。', label: 'MIT 16.512 第 25 讲（Martinez-Sanchez）', url: 'https://ocw.mit.edu/courses/16-512-rocket-propulsion-fall-2005/8b2220397190eb677d156c4b486dd069_lecture_25.pdf' },
  { description: '交付比冲按各损失效率连乘；燃烧室综合效率约 98% 为常见值；动力学损失由喷管尺寸主导。', label: 'RocketIsp 效率文档', url: 'https://rocketisp.readthedocs.io/en/latest/efficiencies.html' },
  { description: '给定环境推力按动量项与压强项复算，比冲按标准重力加速度换算。', label: 'NASA Glenn 推力方程', url: 'https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/rocket-thrust-equation/' },
];

export function summarize(result) {
  return Object.fromEntries(metrics.map(metric => {
    const value = metric.read(result);
    return [metric.id, Number.isFinite(value) ? value : null];
  }));
}

// Calorically perfect, isentropic reference using only chamber state and
// requested outlet condition. It does not use the C nozzle's exit solution.
export function perfectGasReference(input, chamber) {
  const { gamma_c: gamma, R_m_c: gasConstant, p_c_Pa: pressure, T_c_K: temperature } = chamber;
  if (!(gamma > 1 && gasConstant > 0 && temperature > 0 && pressure > 0)) return null;
  const half = (gamma - 1) / 2;
  const exponent = (gamma + 1) / (2 * (gamma - 1));
  const area = mach => (2 * (1 + half * mach ** 2) / (gamma + 1)) ** exponent / mach;
  let mach;
  if (input.exitMode === 'mach') mach = input.exitValue;
  else if (input.exitMode === 'eps') {
    let low = 1;
    let high = 2;
    while (area(high) < input.exitValue && high < 128) high *= 2;
    for (let i = 0; i < 65; i++) {
      const middle = (low + high) / 2;
      if (area(middle) < input.exitValue) low = middle;
      else high = middle;
    }
    mach = input.exitValue === 1 ? 1 : (low + high) / 2;
  } else {
    const ratio = input.exitMode === 'pe' ? input.exitValue / pressure : input.exitValue;
    mach = Math.sqrt((ratio ** (-(gamma - 1) / gamma) - 1) / half);
  }
  if (!(mach >= 1 - 1e-9)) return null;
  const scale = 1 + half * mach ** 2;
  const exitTemperature = temperature / scale;
  const exitPressure = pressure * scale ** (-gamma / (gamma - 1));
  const cstar = Math.sqrt(gasConstant * temperature / gamma) * ((gamma + 1) / 2) ** exponent;
  const velocity = mach * Math.sqrt(gamma * gasConstant * exitTemperature);
  const areaPerFlow = area(mach) * cstar / pressure;
  const effective = velocity + (exitPressure - input.pa) * areaPerFlow;
  const vacuum = velocity + exitPressure * areaPerFlow;
  return {
    cstar, exitTemperature, exitPressure: exitPressure / 1000, exitMach: mach,
    isp: effective / gravity, ispVac: vacuum / gravity,
    thrust: input.mdot > 0 ? effective * input.mdot / 1000 : null,
    thrustVac: input.mdot > 0 ? vacuum * input.mdot / 1000 : null,
  };
}

function contains(value, lower, upper) {
  const tolerance = 1e-8 * Math.max(1, Math.abs(lower), Math.abs(upper));
  return value >= lower - tolerance && value <= upper + tolerance;
}

export function sampledBounds(cases, expected, main) {
  const edges = cases.filter(item => item.role === 'edge');
  const probes = cases.filter(item => item.role === 'probe');
  const complete = edges.length === expected;
  return metrics.map(({ id, label, unit, digits }) => {
    const values = edges.filter(item => Number.isFinite(item.summary[id]));
    const sorted = values.toSorted((a, b) => a.summary[id] - b.summary[id]);
    const low = sorted[0];
    const high = sorted.at(-1);
    const full = complete && values.length === expected;
    const tested = main && !edges.some(item => item.model === main.model && item.heat === main.heat);
    return {
      id, label, unit, digits, complete: full,
      low: low?.summary[id] ?? null,
      high: high?.summary[id] ?? null,
      lowId: low?.id,
      highId: high?.id,
      baseline: tested && full && Number.isFinite(main.summary[id])
        ? contains(main.summary[id], low.summary[id], high.summary[id]) : null,
      probes: probes.map(({ id: caseId, summary }) => ({
        id: caseId, value: summary[id],
        within: full && Number.isFinite(summary[id])
          ? contains(summary[id], low.summary[id], high.summary[id]) : null,
      })),
    };
  });
}

function nominalInput(input, preset) {
  const same = keys => preset && keys.every(key => input[key] === preset[key]);
  return {
    // Specific quantities (Isp, c*) do not depend on mass flow.
    specific: Boolean(same(['oxidizer','fuel','pc','of','tox','tfuel'])
      && input.exitMode === 'eps' && input.exitValue === preset.eps),
    flow: Boolean(same(['mdot'])) && input.mdot > 0,
  };
}

export function comparePublic(input, preset, main, bounds) {
  const refs = benchmarks[input.preset] ?? [];
  const nominal = nominalInput(input, preset);
  return refs.map(ref => {
    const meta = metrics.find(metric => metric.id === ref.metric);
    const computed = main.summary[ref.metric];
    const isThrust = ref.metric.startsWith('thrust');
    const ambient = ref.metric.endsWith('Vac') || input.pa === 101325;
    const comparable = nominal.specific && ambient && (!isThrust || nominal.flow);
    const bound = bounds.find(item => item.id === ref.metric);
    return {
      ...ref, label: meta.label, unit: meta.unit, digits: meta.digits,
      computed, comparable, thrust: isThrust,
      reason: comparable ? '' : !nominal.specific ? '推进剂、室压或面积比与预设不同'
        : !ambient ? '环境压强与公开值口径不同' : '主推力室流量未知或已修改',
      deltaPercent: comparable && Number.isFinite(computed)
        ? 100 * (computed / ref.value - 1) : null,
      inBounds: comparable && bound?.complete
        ? contains(ref.value, bound.low, bound.high) : null,
    };
  });
}

// Mass flow implied by a public thrust and Isp quoted at the same condition.
export function impliedFlow(presetId, mdot) {
  const refs = benchmarks[presetId] ?? [];
  const pairs = [['thrust','isp'],['thrustVac','ispVac']];
  for (const [f, i] of pairs) {
    const F = refs.find(ref => ref.metric === f);
    const I = refs.find(ref => ref.metric === i);
    if (F && I) {
      const total = F.value * 1000 / (I.value * gravity);
      return { total, condition: F.condition, ratio: mdot > 0 ? mdot / total : null };
    }
  }
  return null;
}

export function referenceCheck(input, preset, main) {
  const ref = independentReference.cases[input.preset];
  const nominal = nominalInput(input, preset);
  if (!ref || !nominal.specific || main.heat !== 0 || input.pa !== preset.pa
    || input.throat !== 'sonic') return null;
  const model = main.model === 'fc' ? 'fc' : main.model === 'eq' ? 'eq' : null;
  if (!model) return null;
  return { tool: independentReference.tool, model, rows: Object.entries(ref[model]).map(([id, value]) => ({
    id, reference: value, computed: main.summary[id],
    deltaPercent: Number.isFinite(main.summary[id]) ? 100 * (main.summary[id] / value - 1) : null,
  })) };
}

export function audit(input, result, trace) {
  const { chamber, throat, exit, performance: perf, checks, profile } = result;
  const outputs = [throat, exit, ...profile];
  const maxAbs = values => values.some(value => !Number.isFinite(value))
    ? null : Math.max(...values.map(Math.abs));
  const exhaust = exit.u_m_s > 0 && exit.G_kg_m2s > 0
    ? exit.u_m_s + (exit.p_Pa - input.pa) / exit.G_kg_m2s : null;
  const equilibrium = [
    ['燃烧室焓求根 |Δh|', trace.at(-1)?.stage === 'final' ? Math.abs(trace.at(-1).residual) : null, 0.01, 'J/kg'],
    ['重建燃烧室质量闭合 |Σ nᵢMᵢ − 1|', Number.isFinite(checks.mass_closure) ? Math.abs(checks.mass_closure - 1) : null, 1e-4, 'kg/kg'],
    ['重建燃烧室熵偏差', Number.isFinite(checks.s_deviation_J_kgK) ? Math.abs(checks.s_deviation_J_kgK) : null, 10, 'J/(kg·K)'],
    ['喉部声速条件 |Ma − 1|', input.throat === 'sonic' ? Math.abs(throat.Ma - 1) : null, 2e-4, '1'],
    ['喷管截面总焓最大偏差', maxAbs(outputs.map(s => Number.isFinite(s.h_J_kg) ? s.h_J_kg + s.u_m_s ** 2 / 2 - chamber.h_c_J_kg : null)), 0.1, 'J/kg'],
    ['喷管截面熵最大偏差', maxAbs(outputs.map(s => Number.isFinite(s.s_J_kgK) ? s.s_J_kgK - chamber.s_c_J_kgK : null)), 1, 'J/(kg·K)'],
    ['有效排气速度复算偏差', exhaust === null ? null : Math.abs(exhaust - perf.Isp_m_s), 0.01, 'm/s'],
    ['推力复算偏差', exhaust === null || input.mdot === 0 ? null : Math.abs(exhaust * input.mdot - perf.F_N), 2, 'N'],
  ];
  // These quantities are imposed by the solver (isentropic marching, sonic
  // throat search), so a zero deviation confirms bookkeeping, not physics.
  const imposed = new Set(['重建燃烧室熵偏差', '喷管截面熵最大偏差', '喉部声速条件 |Ma − 1|']);
  return equilibrium.map(([label, value, limit, unit]) => ({
    label, value, limit, unit, construct: imposed.has(label),
    status: !Number.isFinite(value) ? 'na' : value <= limit ? 'pass' : 'review',
  }));
}
