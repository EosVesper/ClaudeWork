import { execFile } from 'node:child_process';
import { copyFile, mkdir, mkdtemp, readFile, rm, rmdir, stat } from 'node:fs/promises';
import { basename, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { promisify } from 'node:util';
import { audit, benchmarkContext, comparePublic, engineCycles, impliedFlow, perfectGasReference, referenceCheck, sampledBounds, summarize } from './analysis.mjs';

const execute = promisify(execFile);
const solverDirectory = fileURLToPath(new URL('./solver/', import.meta.url));
const executable = join(solverDirectory, 'rocket_thermo.exe');
const runtime = join(solverDirectory, 'runtime');
const oxidizers = ['LOX', 'N2O4', 'H2O2'];
const fuels = ['LCH4', 'LH2', 'RP1', 'MMH', 'UDMH', 'N2H4', 'CH3OH', 'C2H5OH'];
const solids = ['531', '518', 'JPN'];
const models = {
  eq: '化学平衡', fc: '组分冻结', fa: '完全冻结', sf: '突然冻结',
};
const stages = ['scan', 'brent', 'final'];

// The solver is a Windows x64 build. On Linux/macOS it runs through Wine
// (set ROCKET_WINE to override the launcher); the C code is unchanged.
const launcher = process.platform === 'win32' ? null : (process.env.ROCKET_WINE || 'wine');

function capture(args, cwd = solverDirectory) {
  const [file, argv] = launcher ? [launcher, [executable, ...args]] : [executable, args];
  return execute(file, argv, {
    cwd, encoding: 'utf8', timeout: 45000, maxBuffer: 2 * 1024 * 1024, windowsHide: true,
  });
}

export async function loadPresets() {
  const { stdout } = await capture(['--list-engine-data']);
  const presets = stdout.trim().split(/\r?\n/).map(line => {
    const [id,name,oxidizer,fuel,tox,tfuel,of,pc,eps,pa,mdot,source] = line.split('\t');
    if (!source || !Number.isFinite(Number(pc))) throw new Error('求解器预设清单格式无效。');
    return { id, name, oxidizer, fuel, tox: +tox, tfuel: +tfuel, of: +of,
      pc: +pc, eps: +eps, pa: +pa, mdot: +mdot, source };
  });
  return { presets, oxidizers, fuels, solids, models, benchmarkContext, engineCycles };
}

function selection(value, choices, label) {
  if (!choices.includes(value)) throw new RangeError(`${label}选项无效。`);
  return value;
}

function number(value, min, max, label, integer = false) {
  if (!Number.isFinite(value) || value < min || value > max || (integer && !Number.isInteger(value))) {
    throw new RangeError(`${label}须为 ${min} 至 ${max} 的${integer ? '整数' : '数值'}。`);
  }
  return value;
}

export function normalize(raw, presets) {
  if (!raw || typeof raw !== 'object' || Array.isArray(raw)) throw new RangeError('工况格式无效。');
  const kind = selection(raw.kind, ['liquid', 'solid'], '推进剂类型');
  const liquid = kind === 'liquid';
  const preset = selection(raw.preset, ['', ...presets.map(item => item.id)], '发动机');
  const exitMode = selection(raw.exitMode, ['eps', 'pe', 'pr', 'mach'], '出口条件');
  const pc = number(raw.pc, 1e3, 1e9, '燃烧室绝对压强（Pa）');
  const exitLimits = { eps: [1, 500], pe: [1, pc * (1 - 1e-8)], pr: [1e-8, 1 - 1e-8], mach: [1, 30] };
  return {
    kind, preset: liquid ? preset : '', pc, exitMode,
    exitValue: number(raw.exitValue, ...exitLimits[exitMode], '出口参数'),
    pa: number(raw.pa, 0, pc, '环境绝对压强（Pa）'),
    mdot: number(raw.mdot, liquid ? 0 : 0.001, 1e6, '质量流量（kg/s）'),
    model: selection(raw.model, Object.keys(models), '膨胀模型'),
    throat: selection(raw.throat, ['sonic', 'gamma', 'massflux'], '喉部判据'),
    points: number(raw.points, 9, 151, '剖面点数', true),
    heat: liquid ? number(raw.heat, 0, 2500, '基准热损失（kJ/kg）') : 0,
    heatMax: liquid ? number(raw.heatMax, 0, 2500, '热损失上限（kJ/kg）') : 0,
    ...(liquid ? {
      oxidizer: selection(raw.oxidizer, oxidizers, '氧化剂'),
      fuel: selection(raw.fuel, fuels, '燃料'),
      tox: number(raw.tox, 1, 2000, '氧化剂入口温度（K）'),
      tfuel: number(raw.tfuel, 1, 2000, '燃料入口温度（K）'),
      of: number(raw.of, 0.01, 100, '氧燃质量比'),
    } : {
      solid: selection(raw.solid, solids, '固体配方'),
      tin: number(raw.tin, 1, 2000, '固体初温（K）'),
    }),
  };
}

function cliArgs(input, model, heat) {
  const head = input.kind === 'liquid'
    ? [input.oxidizer, input.fuel, input.tox, input.tfuel, input.mdot, input.of, input.pc]
    : ['--solid', input.solid, input.tin, input.pc, '--mdot', input.mdot];
  const loss = heat > 0 ? ['--case', '3', heat * 1000] : [];
  return [...head, ...loss, `--${input.exitMode}`, input.exitValue, '--pa', input.pa,
    '--model', model, '--throat', input.throat, '--profile', input.points,
    '--trace', '--json', 'result.json'].map(String);
}

// Each invocation has its own input/cache directory so parallel runs can
// normalize the database without sharing a write target or output filename.
async function runCase(input, { id, role, model, heat }) {
  await mkdir(runtime, { recursive: true });
  const path = await mkdtemp(join(runtime, 'case-'));
  const started = performance.now();
  try {
    for (const file of ['thermo.inp', 'trans.inp', 'thermo_canonical.inp']) {
      await copyFile(join(solverDirectory, file), join(path, file));
    }
    const args = cliArgs(input, model, heat);
    const { stdout, stderr } = await capture(args, path);
    const result = JSON.parse(await readFile(join(path, 'result.json'), 'utf8'));
    if (result.schema_version !== '1.2' || !result.chamber || !result.exit || !Array.isArray(result.profile)) {
      throw new Error('无法识别 C 求解器输出。');
    }
    const traces = [];
    const warnings = stderr.trim() ? [stderr.trim()] : [];
    for (const line of stdout.split(/\r?\n/)) {
      if (line.startsWith('TRACE\t')) {
        const [,evaluation,stage,temperature,residual] = line.split('\t');
        traces.push({ evaluation: +evaluation, stage: stages[+stage], temperature: +temperature, residual: +residual });
      } else if (/^(注意|补充物性提示)：/.test(line)) {
        warnings.push(line);
      }
    }
    if (traces.length !== result.chamber.temperature_evaluations || traces.at(-1)?.stage !== 'final') {
      warnings.push('燃烧室收敛记录不完整。');
    }
    if (result.profile.length !== input.points) warnings.push('剖面点不足，图表仅使用有效输出。');
    return {
      id, role, model, modelLabel: models[model], heat, result, traces, warnings,
      summary: summarize(result), checks: audit(input, result, traces),
      seconds: (performance.now() - started) / 1000,
      command: [basename(executable), ...args],
    };
  } catch (error) {
    const diagnostic = error.stderr?.trim();
    if (diagnostic) throw new Error(diagnostic.slice(0, 500));
    if (error.killed) throw new Error('C 求解器超过 45 秒。请检查输入条件。');
    throw error;
  } finally {
    // Only the three copied inputs and this invocation's result live here.
    for (const file of ['thermo.inp', 'trans.inp', 'thermo_canonical.inp', 'result.json']) {
      await rm(join(path, file), { force: true });
    }
    await rmdir(path);
  }
}

export async function calculate(input, withRange, presets) {
  await stat(executable);
  const configs = [{ id: 'main', role: 'main', model: input.model, heat: input.heat }];
  if (withRange) {
    const heatMap = input.heatMax > 0 ? [[0, 'edge'], [input.heatMax / 2, 'probe'], [input.heatMax, 'edge']] : [[0, 'edge']];
    for (const model of ['eq', 'fc']) {
      for (const [heat, role] of heatMap) {
        configs.push({ id: `${model}-${role}-${heat}`, role, model, heat });
      }
    }
  }
  const outcomes = [];
  const jobs = new Map();
  for (let i = 0; i < configs.length; i += 2) {
    const pair = configs.slice(i, i + 2);
    const results = await Promise.allSettled(pair.map(async config => {
      const key = `${config.model}:${config.heat}`;
      if (!jobs.has(key)) jobs.set(key, runCase(input, config));
      return { ...await jobs.get(key), ...config };
    }));
    for (let j = 0; j < results.length; j++) outcomes.push({ config: pair[j], ...results[j] });
  }
  if (outcomes[0].status === 'rejected') throw outcomes[0].reason;
  const main = outcomes[0].value;
  const cases = outcomes.slice(1).filter(item => item.status === 'fulfilled').map(item => item.value);
  const failed = outcomes.slice(1).filter(item => item.status === 'rejected')
    .map(({ config, reason }) => ({ ...config, message: reason.message }));
  const preset = presets.find(item => item.id === input.preset);
  const bounds = withRange ? sampledBounds(cases, configs.filter(item => item.role === 'edge').length, main) : [];
  return { input, main, cases, failed, bounds,
    analytical: perfectGasReference(input, main.result.chamber),
    public: comparePublic(input, preset, main, bounds),
    implied: input.kind === 'liquid' ? impliedFlow(input.preset, input.mdot) : null,
    reference: preset ? referenceCheck(input, preset, main) : null,
    cycle: engineCycles[input.preset] ?? null,
    createdAt: new Date().toISOString(),
  };
}
