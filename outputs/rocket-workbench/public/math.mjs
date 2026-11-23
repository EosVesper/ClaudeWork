export const mathFont = '"Formula","Cambria Math","STIX Two Math","Latin Modern Math",serif';

const uprightNames = new Set(['ln', 'exp', 'log', 'cos', 'sin', 'tan']);
const letters = 'A-Za-zΑ-Ωα-ωṁ';
const glyphs = new RegExp(`([_^])\\{([^{}]+)\\}|[${letters}]+|[^${letters}_^]+|[_^]`, 'gu');
const word = new RegExp(`^[${letters}]+$`, 'u');

function runs(expression, upright) {
  return Array.from(expression.matchAll(glyphs), ([token, script, content]) => ({
    text: (script ? content : token).replaceAll('-', '−'),
    level: script === '^' ? 'sup' : script === '_' ? 'sub' : '',
    italic: !upright && !script && word.test(token) && !uprightNames.has(token),
    formula: !upright,
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
  // Formulas use the math face throughout; upright data values use the UI
  // sans with tabular figures.
  return `<span class="math ${upright ? 'num' : 'sym'}">${body}</span>`;
}

export function mathText(expression, upright = false) {
  return runs(expression, upright).map(run => ({ ...run, math: true }));
}

// Plain text with $…$ segments typeset as formulas.
export function richHtml(text, escapeText) {
  return String(text ?? '').split(/(\$[^$]+\$)/u).map(part => part.startsWith('$') && part.endsWith('$') && part.length > 2
    ? mathHtml(part.slice(1, -1)) : escapeText(part)).join('');
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
