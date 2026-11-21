export const mathFont = '"Cambria Math","STIX Two Math","Latin Modern Math",serif';

const uprightNames = new Set(['ln', 'exp', 'log']);
const glyphs = /([_^])\{([^{}]+)\}|[A-Za-zΑ-Ωα-ω]+|[^A-Za-zΑ-Ωα-ω_^]+|[_^]/gu;

function runs(expression, upright) {
  return Array.from(expression.matchAll(glyphs), ([token, script, content]) => ({
    text: (script ? content : token).replaceAll('-', '−'),
    level: script === '^' ? 'sup' : script === '_' ? 'sub' : '',
    italic: !upright && !script && /^[A-Za-zΑ-Ωα-ω]+$/u.test(token) && !uprightNames.has(token),
  }));
}

const escape = text => text.replace(/[&<>"']/g, c => ({
  '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
})[c]);

export function mathHtml(expression, upright = false) {
  const body = runs(expression, upright).map(({ text, level, italic }) => {
    if (level) return `<${level}>${escape(text)}</${level}>`;
    return italic ? `<i>${escape(text)}</i>` : escape(text);
  }).join('');
  return `<span class="math">${body}</span>`;
}

export function mathText(expression, upright = false) {
  return runs(expression, upright).map(run => ({ ...run, math: true }));
}

export function unitSource(unit) {
  return unit.replaceAll('²', '^{2}').replaceAll('³', '^{3}');
}

export function numericSource(value, digits = 4) {
  if (!Number.isFinite(value)) return '—';
  if (value !== 0 && (Math.abs(value) < 0.001 || Math.abs(value) >= 1e7)) {
    const [mantissa, exponent] = value.toExponential(3).split('e');
    return `${mantissa} × 10^{${Number(exponent)}}`;
  }
  return value.toLocaleString('zh-CN', { maximumFractionDigits: digits });
}
