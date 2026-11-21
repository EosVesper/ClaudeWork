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
const benchmarks = {
  'TQ-12B': [
    { metric: 'thrust', value: 1000, condition: '海平面 · 单台 B 型公开设计值', source: '蓝箭航天型号发布', url: landspace },
    { metric: 'isp', value: 293, condition: '海平面 · B 型公开额定值', source: '蓝箭航天招股书 1-1-210 页', url: sse },
  ],
  'TQ-15B': [
    { metric: 'thrustVac', value: 1183, condition: '真空 · 单台 B 型公开设计值', source: '蓝箭航天型号发布', url: landspace },
    { metric: 'ispVac', value: 3500 / gravity, original: '3500 m/s', condition: '真空 · B 型公开设计值', source: '蓝箭航天型号发布', url: landspace },
  ],
  'YF-100K': [
    { metric: 'thrust', value: 1250, condition: '海平面 · YF-100K 系列文献值', source: '李斌（2022）表 1', url: caltPaper },
    { metric: 'isp', value: 302, condition: '海平面 · YF-100K 系列文献值', source: '李斌（2022）表 1', url: caltPaper },
  ],
};

export const benchmarkContext = [
  { description: '朱雀三号遥一采用天鹊 A 型。此处采用 B 型公开设计口径。', label: '蓝箭航天遥一通报', url: 'https://www.landspace.com/news-detail.html?itemid=66' },
  { description: '长征十号乙首飞公开的起飞推力属于整箭。YF-100K 比对采用系列文献的单机口径。', label: '火箭院长十乙首飞通报', url: 'https://calt.spacechina.com/n689/c2573694/content.html' },
  { description: '给定环境推力按动量项与压强项复算，比冲按标准重力加速度换算。', label: 'NASA Glenn 推力方程', url: 'https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/rocket-thrust-equation/' },
  { description: '化学平衡与组分冻结是两类膨胀模型。热损失上限由用户设定，默认值用于展示敏感性。', label: 'NASA CEA 方法概览', url: 'https://ntrs.nasa.gov/citations/20240010609' },
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

export function comparePublic(input, preset, main, bounds) {
  const refs = benchmarks[input.preset] ?? [];
  const nominalGeometry = preset
    && ['oxidizer','fuel','pc','of','tox','tfuel','mdot'].every(key => input[key] === preset[key])
    && input.exitMode === 'eps' && input.exitValue === preset.eps;
  return refs.map(ref => {
    const meta = metrics.find(metric => metric.id === ref.metric);
    const computed = main.summary[ref.metric];
    const comparable = Boolean(nominalGeometry
      && (ref.metric.endsWith('Vac') || input.pa === 101325));
    const bound = bounds.find(item => item.id === ref.metric);
    return {
      ...ref, label: meta.label, unit: meta.unit, digits: meta.digits,
      computed, comparable,
      deltaPercent: comparable && Number.isFinite(computed)
        ? 100 * (computed / ref.value - 1) : null,
      inBounds: comparable && bound?.complete
        ? contains(ref.value, bound.low, bound.high) : null,
    };
  });
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
  return equilibrium.map(([label, value, limit, unit]) => ({
    label, value, limit, unit,
    status: !Number.isFinite(value) ? 'na' : value <= limit ? 'pass' : 'review',
  }));
}
