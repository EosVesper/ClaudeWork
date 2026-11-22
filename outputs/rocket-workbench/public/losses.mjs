// Delivered-performance envelope built from the ideal C solution and named
// efficiency factors. Pure functions: used by the page and by npm test.
export const gravity = 9.80665;

// Each factor has a lower bound, a nominal value and an upper bound. The
// defaults are engineering ranges with the cited basis; the page lets the
// user replace them with values from the course or a test report.
export const lossFactors = [
  { id: 'cstar', label: '燃烧效率', symbol: 'η_{c*}', unit: '1', min: 0.96, nom: 0.98, max: 0.995, step: 0.001,
    note: '燃烧室综合效率；约 98% 为常见值。第一次大作业基准取 $η_{c*} = η_{CF} = 0.97$，并在报告中注明偏保守（现代液氧甲烷 0.98–0.99）。',
    source: 'RocketIsp 效率文档', url: 'https://rocketisp.readthedocs.io/en/latest/efficiencies.html' },
  { id: 'angle', label: '喷管出口半角', symbol: 'α_{e}', unit: '°', min: 8, nom: 10, max: 15, step: 0.5,
    note: '扩张损失 $λ = (1 + cos α)/2$；15° 锥形喷管 $λ = 0.983$，钟形喷管出口角通常更小。',
    source: 'Malina 修正（Sutton《火箭发动机基础》第 3 章）', url: 'https://en.wikipedia.org/wiki/Thrust_coefficient' },
  { id: 'bl', label: '边界层与摩擦', symbol: 'η_{BL}', unit: '1', min: 0.985, nom: 0.99, max: 0.995, step: 0.001,
    note: '摩擦效率约 0.99；室压和喉径越大损失越小，面积比越大损失越大。',
    source: 'RocketIsp 喷管效率（SP-8120 关联式）', url: 'https://rocketisp.readthedocs.io/en/latest/nozzle_eff.html' },
  { id: 'gg', label: '其他支路流量份额', symbol: 'β = ṁ_{other}/ṁ', unit: '1', min: 0.05, nom: 0.09, max: 0.13, step: 0.005, cycle: 'gg',
    note: '不进入主推力室的流量（燃气发生器等）占整机流量的比例。第一次大作业据一项液氧甲烷燃气发生器循环一级发动机方案（室压 12 MPa、面积比 20：燃气发生器 65.8 kg/s，整机 699.4 kg/s，即 9.41%）取 0.09，探索范围 0.05–0.13；该研究原文献在作业中尚待补齐。',
    source: '第一次大作业「参数确定」', url: '' },
  { id: 'ggRatio', label: '涡轮排气速度比', symbol: 'c_{GG}/c', unit: '1', min: 0.3, nom: 0.45, max: 0.65, step: 0.05, cycle: 'gg',
    note: '$δI/I ≈ (ṁ_{GG}/ṁ)(1 − c_{GG}/c)$；F-1 算例的整机损失约 1.1%。',
    source: 'MIT 16.512 第 25 讲', url: 'https://ocw.mit.edu/courses/16-512-rocket-propulsion-fall-2005/8b2220397190eb677d156c4b486dd069_lecture_25.pdf' },
];

export function defaultLosses() {
  return Object.fromEntries(lossFactors.map(({ id, min, nom, max }) => [id, { min, nom, max }]));
}

export const divergence = degrees => (1 + Math.cos(degrees * Math.PI / 180)) / 2;

export function validLosses(losses) {
  return lossFactors.every(({ id }) => {
    const f = losses?.[id];
    return f && [f.min, f.nom, f.max].every(Number.isFinite) && f.min <= f.nom && f.nom <= f.max;
  });
}

function base(record) {
  const all = [record.main, ...(record.cases ?? [])].filter(item => item?.result);
  const heat = record.main.heat;
  const pick = model => all.find(item => item.model === model && item.heat === heat);
  const top = pick('eq') ?? record.main;
  const bottom = pick('fc') ?? null;
  return { top, bottom };
}

// One delivered state. Losses act on the vacuum momentum + pressure thrust;
// the back-pressure term uses the throat area that the delivered c* implies.
function deliver(item, input, eta) {
  const { performance: perf, chamber } = item.result;
  const cstar = eta.cstar * perf.c_star_m_s;
  const ispVac = perf.Isp_vac_m_s * eta.cstar * eta.lambda * eta.bl;
  const back = input.pa * perf.eps * cstar / chamber.p_c_Pa;
  return { cstar, ispVac, isp: ispVac - back };
}

function withFlow(state, mdot, engine) {
  const keep = engine ? 1 - engine.fraction * (1 - engine.ratio) : 1;
  const total = engine && mdot > 0 ? mdot / (1 - engine.fraction) : mdot;
  const isp = state.isp * keep / gravity;
  const ispVac = state.ispVac * keep / gravity;
  return {
    cstar: state.cstar, isp, ispVac,
    thrust: total > 0 ? isp * gravity * total / 1000 : null,
    thrustVac: total > 0 ? ispVac * gravity * total / 1000 : null,
    mdot: total > 0 ? total : null,
  };
}

export const envelopeMetrics = [
  { id: 'cstar', label: '特征速度', symbol: 'c^{∗}', unit: 'm/s', digits: 1 },
  { id: 'isp', label: '给定环境比冲', symbol: 'I_{sp}', unit: 's', digits: 1 },
  { id: 'ispVac', label: '真空比冲', symbol: 'I_{sp,vac}', unit: 's', digits: 1 },
  { id: 'thrust', label: '给定环境推力', symbol: 'F', unit: 'kN', digits: 1 },
  { id: 'thrustVac', label: '真空推力', symbol: 'F_{vac}', unit: 'kN', digits: 1 },
];

export function envelope(record, losses, { engineLevel = true } = {}) {
  if (!record?.main?.result || record.input.kind !== 'liquid' || !validLosses(losses)) return null;
  const { input } = record;
  const gg = record.cycle?.cycle === 'gg' && engineLevel;
  const { top, bottom } = base(record);
  const factor = key => ({ cstar: losses.cstar[key], bl: losses.bl[key] });
  const eta = {
    nom: { ...factor('nom'), lambda: divergence(losses.angle.nom) },
    low: { ...factor('min'), lambda: divergence(losses.angle.max) },
    high: { ...factor('max'), lambda: divergence(losses.angle.min) },
  };
  const ideal = withFlow({
    cstar: top.result.performance.c_star_m_s,
    isp: top.result.performance.Isp_m_s,
    ispVac: top.result.performance.Isp_vac_m_s,
  }, input.mdot, null);
  const chamber = {
    nominal: withFlow(deliver(top, input, eta.nom), input.mdot, null),
    low: withFlow(deliver(bottom ?? top, input, eta.low), input.mdot, null),
    high: withFlow(deliver(top, input, eta.high), input.mdot, null),
  };
  const engine = gg ? {
    nominal: withFlow(deliver(top, input, eta.nom), input.mdot, { fraction: losses.gg.nom, ratio: losses.ggRatio.nom }),
    low: withFlow(deliver(bottom ?? top, input, eta.low), input.mdot, { fraction: losses.gg.max, ratio: losses.ggRatio.min }),
    high: withFlow(deliver(top, input, eta.high), input.mdot, { fraction: losses.gg.min, ratio: losses.ggRatio.max }),
  } : null;
  return {
    ideal, chamber, engine, gg, frozenIncluded: Boolean(bottom),
    efficiency: {
      nominal: eta.nom.cstar * eta.nom.lambda * eta.nom.bl,
      low: eta.low.cstar * eta.low.lambda * eta.low.bl,
      high: eta.high.cstar * eta.high.lambda * eta.high.bl,
    },
  };
}

// Compare each public value with the delivered envelope at the same basis.
export function judgePublic(record, env, losses = defaultLosses()) {
  if (!env) return [];
  const level = env.engine ?? env.chamber;
  const implied = record.implied;
  const flowShared = implied?.ratio != null && Math.abs(implied.ratio - 1) < 0.005;
  const branchShared = env.engine && implied?.ratio != null && Math.abs(implied.ratio - (1 - losses.gg.nom)) < 0.005;
  return (record.public ?? []).map(ref => {
    const nominal = level.nominal[ref.metric];
    const low = level.low[ref.metric];
    const high = level.high[ref.metric];
    const usable = ref.comparable && [nominal, low, high].every(Number.isFinite);
    const ideal = env.ideal[ref.metric];
    let note = '';  // explanation shown under the source column
    if (ref.thrust && flowShared) note = '输入流量由同一组公开推力与比冲换算，推力偏差等同于比冲偏差，不算独立验证。';
    else if (ref.thrust && branchShared) note = `输入的主推力室流量 = 公开推力 ÷ 公开比冲 × $(1 − β)$，$β = ${losses.gg.nom}$（第一次大作业）。整机流量因此与公开值同源，推力偏差等同于比冲偏差，不算独立验证。`;
    else if (ref.thrust && implied?.ratio != null) note = `公开值隐含总流量 ${implied.total.toFixed(1)} kg/s，输入为其 ${(100 * implied.ratio).toFixed(1)}%。推力判定受流量口径支配。`;
    const inside = usable ? ref.value >= low && ref.value <= high : null;
    if (!note && inside === false && !ref.thrust && ref.value > high) {
      note = '公开值高于包络上限。可检查：实际面积比是否更大、涡轮排气是否引入喷管延伸段（提高 $c_{GG}/c$）、支路份额是否更小，或公开值是否为设计目标而非实测。';
    } else if (!note && inside === false && !ref.thrust) {
      note = '公开值低于包络下限，说明实际损失大于所设因子范围，或公开值口径不同。';
    }
    return {
      ...ref, nominal, low, high, ideal,
      deltaNominal: usable ? 100 * (ref.value / nominal - 1) : null,
      inside,
      requiredEfficiency: !ref.thrust && Number.isFinite(ideal) ? ref.value / ideal : null,
      note,
    };
  });
}
