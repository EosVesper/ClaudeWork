import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { execFile } from 'node:child_process';
import { calculate, loadPresets, normalize } from './engine.mjs';

const port = Number(process.env.ROCKET_WORKBENCH_PORT ?? 8732);
const host = '127.0.0.1';
const catalog = await loadPresets();
const assets = {
  '/': ['index.html', 'text/html'],
  '/app.mjs': ['app.mjs', 'text/javascript'],
  '/charts.mjs': ['charts.mjs', 'text/javascript'],
  '/math.mjs': ['math.mjs', 'text/javascript'],
  '/losses.mjs': ['losses.mjs', 'text/javascript'],
  '/style.css': ['style.css', 'text/css'],
  '/icon.svg': ['icon.svg', 'image/svg+xml'],
  '/fonts/stix-two-text.woff2': ['fonts/stix-two-text.woff2', 'font/woff2'],
  '/fonts/stix-two-text-italic.woff2': ['fonts/stix-two-text-italic.woff2', 'font/woff2'],
  '/fonts/stix-two-math-ops.woff2': ['fonts/stix-two-math-ops.woff2', 'font/woff2'],
  '/fonts/space-grotesk.woff2': ['fonts/space-grotesk.woff2', 'font/woff2'],
  '/plume.mjs': ['plume.mjs', 'text/javascript'],
};
let active = 0;

function writeJson(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(body));
}

async function readJson(req) {
  if (req.headers['content-type']?.split(';')[0].trim() !== 'application/json') {
    throw new RangeError('请求须为 JSON。');
  }
  let size = 0;
  const parts = [];
  for await (const part of req) {
    size += part.length;
    if (size > 16384) throw new RangeError('工况输入过长。');
    parts.push(part);
  }
  try {
    return JSON.parse(Buffer.concat(parts).toString('utf8'));
  } catch {
    throw new RangeError('无法解析工况输入。');
  }
}

const server = createServer(async (req, res) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'no-referrer');
  res.setHeader('Content-Security-Policy', "default-src 'self'; img-src 'self' data:; style-src 'self'; frame-ancestors 'none'; base-uri 'none'");
  const localHosts = [`${host}:${port}`, `localhost:${port}`];
  if (!localHosts.includes(req.headers.host)) return writeJson(res, 403, { error: '仅允许从本机访问。' });
  const path = (req.url ?? '/').split('?')[0];
  try {
    if (req.method === 'GET' && Object.hasOwn(assets, path)) {
      const [file, type] = assets[path];
      const data = await readFile(new URL(`./public/${file}`, import.meta.url));
      const charset = type.startsWith('font/') ? '' : '; charset=utf-8';
      res.writeHead(200, { 'Content-Type': `${type}${charset}`, 'Cache-Control': 'no-cache' });
      res.end(data);
      return;
    }
    if (path === '/api/presets' && req.method === 'GET') return writeJson(res, 200, catalog);
    if (path === '/api/calculate' && req.method === 'POST') {
      const origin = req.headers.origin;
      if (origin && origin !== `http://${req.headers.host}`) return writeJson(res, 403, { error: '请求来源无效。' });
      const body = await readJson(req);
      const input = normalize(body?.input, catalog.presets);
      if (active >= 2) return writeJson(res, 429, { error: '正在计算，请稍后再试。' });
      active++;
      try {
        const outcome = await calculate(input, body?.range === true, catalog.presets);
        return writeJson(res, 200, outcome);
      } finally {
        active--;
      }
    }
    writeJson(res, 404, { error: '找不到页面或接口。' });
  } catch (error) {
    const status = error instanceof RangeError ? 400 : path === '/api/calculate' ? 422 : 500;
    if (status === 500) console.error(error);
    writeJson(res, status, { error: status === 500 ? '本地服务发生错误。' : error.message });
  }
});

server.on('error', error => {
  if (error.code === 'EADDRINUSE') console.error(`端口 ${port} 已被占用，可设置 ROCKET_WORKBENCH_PORT 后重试。`);
  else console.error(error);
  process.exitCode = 1;
});
server.listen(port, host, () => {
  const url = `http://${host}:${port}/`;
  console.log(`热力计算工作台 ${url}`);
  if (process.platform === 'win32' && process.argv.includes('--open')) {
    execFile('rundll32.exe', ['url.dll,FileProtocolHandler', url], { windowsHide: true },
      error => { if (error) console.error(`请在浏览器打开 ${url}`); });
  }
});
