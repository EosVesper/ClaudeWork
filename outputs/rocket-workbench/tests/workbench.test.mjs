import assert from 'node:assert/strict';
import test from 'node:test';
import { calculate, loadPresets, normalize } from '../engine.mjs';
import { nozzleGeometry } from '../public/charts.mjs';
import { mathHtml, mathText, numericSource } from '../public/math.mjs';
import { defaultLosses, divergence, envelope, judgePublic } from '../public/losses.mjs';

const { presets } = await loadPresets();
const make = (id, overrides = {}) => {
  const preset = presets.find(item => item.id === id);
  return normalize({
    ...preset, kind: 'liquid', preset: id, exitMode: 'eps', exitValue: preset.eps,
    model: 'eq', throat: 'sonic', points: 41, heat: 0, heatMax: 0, ...overrides,
  }, presets);
};

function checkCase(record, inputs) {
  const { main } = record;
  const { chamber, throat, exit, profile } = main.result;
  assert.equal(main.result.schema_version, '1.2');
  assert.equal(profile.length, inputs.points);
  assert.equal(profile[0].p_Pa, inputs.pc);
  assert.ok(chamber.T_c_K > throat.T_K && throat.T_K > exit.T_K);
  assert.ok(Math.abs(1 - throat.Ma) < 2e-4);
  assert.ok(exit.Ma > throat.Ma);
  assert.equal(main.traces.length, chamber.temperature_evaluations);
  assert.equal(main.traces.at(-1).stage, 'final');
  assert.ok(Math.abs(main.traces.at(-1).temperature - chamber.T_c_K) < 1e-5);
  assert.ok(Math.abs(main.traces.at(-1).residual) <= 0.01);
  assert.ok(main.checks.every(check => check.status !== 'review'));
  for (let i = 1; i < profile.length; i++) {
    assert.ok(profile[i].p_Pa < profile[i - 1].p_Pa);
    assert.ok(Number.isFinite(profile[i].h_J_kg));
  }
}

const records = {};
for (const id of ['TQ-12B', 'TQ-15B', 'YF-100K']) {
  test(`${id} resolves equilibrium/frozen bounds and preserves public deviations`, async () => {
    const input = make(id);
    const output = await calculate(input, true, presets);
    records[id] = output;
    checkCase(output, input);
    assert.equal(output.cases.filter(item => item.role === 'edge').length, 2);
    assert.equal(output.cases.filter(item => item.role === 'probe').length, 0);
    assert.equal(output.failed.length, 0);
    assert.ok(output.analytical.exitMach > 1);
    assert.ok(output.analytical.ispVac >= output.analytical.isp);
    assert.ok(Math.abs(output.analytical.ispVac - output.main.summary.ispVac) / output.main.summary.ispVac < .15);
    for (const bound of output.bounds) {
      assert.equal(bound.complete, true);
      const edge = output.cases.filter(item => item.role === 'edge').map(item => item.summary[bound.id]);
      assert.equal(bound.low, Math.min(...edge));
      assert.equal(bound.high, Math.max(...edge));
      assert.ok(bound.probes.every(probe => probe.within === (probe.value >= bound.low - 1e-6 && probe.value <= bound.high + 1e-6)));
    }
    assert.ok(output.public.every(item => item.comparable && Number.isFinite(item.deltaPercent)));
    if (id === 'TQ-12B') {
      assert.ok(Math.abs(output.main.summary.thrust - 1006.149583) < .01);
      assert.ok(Math.abs(output.main.summary.isp - 323.9577152) < .001);
      assert.equal(output.public.find(row => row.metric === 'isp').inBounds, false);
    }
    // Independent Cantera solution agrees with the C solver within 0.2 %.
    assert.ok(output.reference && output.reference.rows.length >= 4);
    for (const row of output.reference.rows) assert.ok(Math.abs(row.deltaPercent) < 0.2, `${id} ${row.id}`);
  });
}

test('optional heat-loss sweep keeps edge and probe cases', async () => {
  const input = make('TQ-12B', { heatMax: 300, points: 21 });
  const output = await calculate(input, true, presets);
  assert.equal(output.cases.filter(item => item.role === 'edge').length, 4);
  assert.equal(output.cases.filter(item => item.role === 'probe').length, 2);
  assert.ok(output.bounds.every(bound => bound.complete));
});

test('delivered envelope is ordered and engine level applies only to gas-generator engines', () => {
  assert.ok(Math.abs(divergence(15) - 0.98296) < 1e-5);
  const losses = defaultLosses();
  for (const id of ['TQ-12B', 'TQ-15B', 'YF-100K']) {
    const record = records[id];
    const env = envelope(record, losses);
    for (const metric of ['cstar', 'isp', 'ispVac', 'thrust', 'thrustVac']) {
      const level = env.engine ?? env.chamber;
      assert.ok(level.low[metric] <= level.nominal[metric] && level.nominal[metric] <= level.high[metric], `${id} ${metric}`);
      assert.ok(env.chamber.high[metric] <= env.ideal[metric] + 1e-9, `${id} ${metric} below ideal`);
    }
    assert.equal(Boolean(env.engine), record.cycle.cycle === 'gg');
    assert.ok(env.frozenIncluded);
    if (env.engine) assert.ok(env.engine.nominal.isp < env.chamber.nominal.isp);
  }
  const verdict = id => Object.fromEntries(judgePublic(records[id], envelope(records[id], losses), losses).map(row => [row.metric, row]));
  // With the report-1 branch fraction (β = 0.09) the TQ-12B public Isp sits within ~1 % of nominal.
  assert.equal(verdict('TQ-12B').isp.inside, true);
  assert.ok(Math.abs(verdict('TQ-12B').isp.deltaNominal) < 2);
  assert.equal(verdict('YF-100K').isp.inside, true);
  // TQ-15B 3500 m/s exceeds the envelope for ε = 50 and β = 0.09; the note says why.
  assert.equal(verdict('TQ-15B').ispVac.inside, false);
  assert.ok(verdict('TQ-15B').ispVac.deltaNominal > 0);
  assert.match(verdict('TQ-15B').ispVac.note, /面积比/u);
  // Thrust comparisons are flagged as not independent of the Isp comparison.
  assert.match(verdict('TQ-12B').thrust.note, /不算独立验证/u);
  assert.match(verdict('TQ-15B').thrustVac.note, /不算独立验证/u);
  assert.match(verdict('YF-100K').thrust.note, /不算独立验证/u);
});

test('solid and missing liquid mass flow keep availability explicit', async () => {
  const solid = make('TQ-12B', {
    kind:'solid', solid:'JPN', tin:298.15, pc:7e6, mdot:1, exitValue:12, points:25,
  });
  const massUnknown = make('TQ-12B', { mdot: 0, points: 25 });
  const [a,b] = await Promise.all([calculate(solid,false,presets),calculate(massUnknown,false,presets)]);
  checkCase(a,solid);
  checkCase(b,massUnknown);
  assert.equal(a.public.length,0);
  assert.equal(b.main.summary.thrust,null);
  assert.equal(b.public.find(row => row.metric === 'thrust').comparable,false);
  // Specific impulse does not depend on mass flow, so it stays comparable.
  assert.equal(b.public.find(row => row.metric === 'isp').comparable,true);
  assert.ok(Number.isFinite(b.main.summary.isp));
  const withFlow = nozzleGeometry(a.main.result);
  const withoutFlow = nozzleGeometry(b.main.result);
  for (const model of [withFlow, withoutFlow]) {
    assert.ok(model.points.length > 2);
    assert.equal(model.points[0].progress, 0);
    assert.equal(model.points[0].radiusRatio, 1);
    assert.equal(model.points.at(-1).progress, 1);
    assert.ok(model.points.slice(1).every((point,i) => point.progress > model.points[i].progress));
  }
  assert.equal(withoutFlow.throatRadius, null);
  assert.ok(Math.abs(withFlow.throatRadius * withFlow.points.at(-1).radiusRatio - a.main.result.performance.d_e_m / 2) < 1e-8);
  assert.equal(nozzleGeometry({ ...a.main.result, profile:[] }), null);
});

test('scientific notation and subscripts share the HTML and Canvas formula runs', () => {
  const small = numericSource(-4.6566e-10);
  assert.equal(small,'-4.657 × 10^{-10}');
  assert.match(mathHtml(small,true), /−4\.657 × 10<sup>−10<\/sup>/u);
  assert.match(mathHtml('p/p_{c}'), /<i>p<\/i>\/<i>p<\/i><sub>c<\/sub>/u);
  assert.equal(mathText(small,true).at(-1).level,'sup');
});

test('invalid numbers fail before reaching the C process', () => {
  assert.throws(()=>make('TQ-12B',{pc:-2000}),RangeError);
  assert.throws(()=>make('TQ-12B',{fuel:'LOX & other'}),RangeError);
  assert.throws(()=>make('TQ-12B',{points:9.5}),RangeError);
});
