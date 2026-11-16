// ==UserScript==
// @name         Gemini Auto High Thinking
// @namespace    eosvesper.gemini-auto-high
// @version      5.1
// @description  打开 Gemini 网页 / 新建对话时，自动把 Thinking level 设为 High（适配 iPad Safari + Stay）
// @match        https://gemini.google.com/*
// @run-at       document-idle
// @grant        none
// ==/UserScript==

(function () {
  'use strict';

  // ============ 配置 ============
  const DEBUG = true;            // 第一次用保持 true：右下角会出现日志框。成功后改成 false
  const ONLY_NEW_CHAT = true;    // true：只在首次加载和“新对话”页面执行；false：每次切换对话都执行
  const THINK_LABEL = /^(thinking level|thinking|思考等级|思考级别|思考程度|思考水平)$/i;
  const TARGET = /^(high|高)$/i;
  const TARGET_HINT = /extra thorough|最(为)?(周全|全面|深入)/i;
  const MODEL_BTN_SELECTORS = [
    '[data-test-id="bard-mode-menu-button"]',
    '[data-test-id*="mode-menu"]',
    '[data-test-id*="model-selector"]',
    'bard-mode-switcher button',
  ];
  // 这些区域里的文字（聊天内容、侧边栏、输入框）一律不碰
  const EXCLUDE = 'message-content, model-response, user-query, .markdown, rich-textarea, .ql-editor, side-navigation-v2, bard-sidenav, mat-sidenav, #gah-log';
  const INTERACTIVE = 'button, a, [role="button"], [role="menuitem"], [role="menuitemradio"], [role="option"], [role="radio"], [role="tab"], [role="switch"], mat-expansion-panel-header, [aria-expanded], [tabindex]:not([tabindex="-1"])';
  const OVERLAY = '.cdk-overlay-pane, [role="menu"], [role="dialog"], [role="listbox"], mat-bottom-sheet-container';

  // ============ 日志面板 ============
  let logBox;
  function log(...a) {
    const msg = a.map(x => (x instanceof Element ? describe(x) : String(x))).join(' ');
    console.log('[Gemini-AutoHigh]', msg);
    if (!DEBUG) return;
    if (!logBox) {
      logBox = document.createElement('div');
      logBox.id = 'gah-log';
      logBox.style.cssText = 'position:fixed;right:8px;bottom:8px;z-index:2147483647;width:300px;max-height:40vh;overflow:auto;background:rgba(0,0,0,.82);color:#9f9;font:11px/1.35 monospace;padding:6px;border-radius:8px;white-space:pre-wrap;word-break:break-all';
      const bar = document.createElement('div');
      bar.style.cssText = 'display:flex;gap:6px;margin-bottom:4px';
      const mk = (t, fn) => { const b = document.createElement('button'); b.textContent = t; b.style.cssText = 'font:11px sans-serif;padding:2px 6px'; b.onclick = fn; bar.appendChild(b); };
      mk('复制诊断', copyDiag);
      mk('重试', () => run('manual'));
      mk('关闭', () => logBox.remove());
      logBox.appendChild(bar);
      document.body.appendChild(logBox);
    }
    const line = document.createElement('div');
    line.textContent = msg;
    logBox.appendChild(line);
    logBox.scrollTop = 1e9;
  }
  function describe(el) {
    const attrs = ['role', 'aria-label', 'aria-expanded', 'aria-checked', 'data-test-id']
      .map(k => el.getAttribute(k) != null ? `${k}="${el.getAttribute(k)}"` : '').filter(Boolean).join(' ');
    return `<${el.tagName.toLowerCase()}${el.className && typeof el.className === 'string' ? '.' + el.className.trim().split(/\s+/).slice(0, 3).join('.') : ''} ${attrs}> "${(el.innerText || '').trim().slice(0, 40).replace(/\s+/g, ' ')}"`;
  }
  let lastDiag = '';
  function copyDiag() {
    const panes = [...document.querySelectorAll(OVERLAY)].filter(visible);
    const html = (panes.length ? panes : [document.body]).map(p => p.outerHTML).join('\n\n')
      .replace(/<svg[\s\S]*?<\/svg>/g, '<svg/>').slice(0, 15000);
    const text = (logBox ? logBox.innerText : '') + '\n\n' + (lastDiag || html);
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.cssText = 'position:fixed;left:5vw;top:10vh;width:90vw;height:60vh;z-index:2147483647;font:11px monospace';
    document.body.appendChild(ta);
    ta.focus(); ta.select();
    try { navigator.clipboard.writeText(text); log('已复制到剪贴板（也可在弹出的文本框里手动全选复制，点空白处关闭）'); } catch (e) { log('请在文本框里手动全选复制'); }
    ta.addEventListener('blur', () => ta.remove());
  }

  // ============ 工具 ============
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  function visible(el) {
    if (!el || !el.isConnected) return false;
    const r = el.getBoundingClientRect();
    if (r.width === 0 && r.height === 0) return false;
    const s = getComputedStyle(el);
    return s.visibility !== 'hidden' && s.display !== 'none' && s.opacity !== '0';
  }
  async function waitFor(fn, timeout = 4000, interval = 100) {
    const end = Date.now() + timeout;
    while (Date.now() < end) {
      const v = fn();
      if (v) return v;
      await sleep(interval);
    }
    return null;
  }
  // 找“自身文字”匹配的最小元素（叶子级），而不是外层大容器
  function textEls(re, root = document.body) {
    const out = [];
    const w = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    let n;
    while ((n = w.nextNode())) {
      const t = n.nodeValue.trim();
      if (!t || !re.test(t)) continue;
      const el = n.parentElement;
      if (!el || el.closest(EXCLUDE) || !visible(el)) continue;
      if (!out.includes(el)) out.push(el);
    }
    return out;
  }
  // 从文字元素往上找真正绑定了点击的元素（不越过弹窗边界）
  function clickTarget(el, boundary) {
    let cur = el;
    while (cur && cur !== boundary && cur !== document.body) {
      if (cur.matches(INTERACTIVE)) return cur;
      // cursor 会继承，所以取“指针样式链”最外层那个元素
      const par = cur.parentElement;
      if (getComputedStyle(cur).cursor === 'pointer' && (!par || par === boundary || getComputedStyle(par).cursor !== 'pointer')) return cur;
      cur = cur.parentElement;
    }
    return el;
  }
  // 只点一次：完整的 pointer/mouse 序列 + 一个 click（不会重复触发而导致展开又收起）
  function tap(el) {
    const r = el.getBoundingClientRect();
    const o = { bubbles: true, cancelable: true, composed: true, view: window, clientX: r.left + r.width / 2, clientY: r.top + r.height / 2, button: 0 };
    const P = window.PointerEvent || MouseEvent;
    el.dispatchEvent(new P('pointerdown', { ...o, pointerType: 'touch', isPrimary: true }));
    el.dispatchEvent(new MouseEvent('mousedown', o));
    el.dispatchEvent(new P('pointerup', { ...o, pointerType: 'touch', isPrimary: true }));
    el.dispatchEvent(new MouseEvent('mouseup', o));
    el.dispatchEvent(new MouseEvent('click', o));
  }
  function findModelButton() {
    for (const s of MODEL_BTN_SELECTORS) {
      const el = [...document.querySelectorAll(s)].find(e => visible(e) && !e.closest(EXCLUDE));
      if (el) return el;
    }
    return [...document.querySelectorAll('button, [role="button"]')].find(b =>
      visible(b) && !b.closest(EXCLUDE) && !b.closest(OVERLAY) &&
      (b.hasAttribute('aria-haspopup') || b.querySelector('mat-icon, svg')) &&
      /gemini|hydrogen|flash|pro\b|thinking|fast|everyday/i.test(b.innerText || ''));
  }
  const findThink = () => textEls(THINK_LABEL).find(e => e.closest(OVERLAY)) || textEls(THINK_LABEL)[0];
  // 每次都重新查，防止页面重新渲染后拿着旧元素
  function ctx() {
    const think = findThink();
    if (!think) return null;
    const scope = think.closest(OVERLAY) || document.body;
    return { think, scope, header: clickTarget(think, scope) };
  }
  function findHigh() {
    const c0 = ctx();
    if (!c0) return null;
    const { scope, header } = c0;
    // High 选项是嵌在 Thinking level 菜单项里面的子菜单项，
    // 所以只排除“最近可点元素就是标题行本身”的文字，而不是标题行里所有东西
    const c = textEls(TARGET, scope).filter(e => clickTarget(e, scope) !== header);
    // 优先选旁边带 “Extra thorough” 说明的那个
    return c.find(e => TARGET_HINT.test(clickTarget(e, scope).innerText || '')) || c[0];
  }
  function closeMenu() {
    const bd = document.querySelector('.cdk-overlay-backdrop');
    if (bd && visible(bd)) { tap(bd); return; }
    const t = document.activeElement || document.body;
    t.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', code: 'Escape', keyCode: 27, bubbles: true }));
  }

  // ============ 主流程（顺序执行，带锁，绝不并发） ============
  let running = false;
  async function run(reason) {
    if (running) return;
    running = true;
    try {
      log('开始 (' + reason + ')', location.pathname);
      const modelBtn = await waitFor(findModelButton, 15000, 250);
      if (!modelBtn) return log('✗ 没找到模型切换按钮');
      log('模型按钮:', modelBtn);

      if (!findThink()) {
        tap(modelBtn);
        await waitFor(findThink, 3000);
      }
      let c = ctx();
      if (!c) { log('✗ 打开菜单后没看到 "Thinking level"（当前模型可能不支持档位）'); closeMenu(); return; }
      log('Thinking 行:', c.header);

      // 标题行上直接显示当前档位为 High（折叠状态）→ 直接收起
      const headerText = (c.header.innerText || '').replace(c.think.innerText, '').trim();
      if (/^(high|高)(\b|$)/i.test(headerText) && !findHigh()) {
        log('✓ 当前已经是 High'); closeMenu(); return;
      }
      const isExpanded = el => el.getAttribute('aria-expanded') === 'true' || el.classList.contains('expanded');

      let high = findHigh();
      // 依次尝试不同的点击目标，每次只点一个，点完等结果，避免“展开又收起”
      // 候选：标签所在行本身 → 行里的展开按钮/箭头 → 文字本身
      const candidates = k => {
        const list = [k.header, k.think.closest('[aria-expanded]')];
        // “行” = 从文字往上，第一个含有其他可点元素/图标的祖先（不越过弹窗）
        let row = k.think.parentElement;
        for (let i = 0; row && row !== k.scope && i < 4; i++, row = row.parentElement) {
          if (row.querySelector('[aria-expanded], button, [role="button"], mat-icon, svg')) break;
        }
        if (row && row !== k.scope) {
          list.push(...row.querySelectorAll('[aria-expanded]'), ...row.querySelectorAll('button, [role="button"]'));
          row.querySelectorAll('mat-icon, svg, [class*="arrow"], [class*="chevron"], [class*="expand"]').forEach(i => list.push(clickTarget(i, k.scope)));
          list.push(row);
        }
        list.push(k.think);
        return list.filter((e, i) => e && visible(e) && list.indexOf(e) === i);
      };
      const tried = [];
      for (let n = 0; n < 8 && !high; n++) {
        c = ctx();
        if (!c) { log('菜单关了，重新打开'); tap(modelBtn); await waitFor(findThink, 2000); c = ctx(); if (!c) break; }
        if (isExpanded(c.header)) {
          // 已经展开了就绝不再点（再点会收起），只等选项出现
          log('Thinking 行已展开，等待选项');
          high = await waitFor(findHigh, 2000);
          break;
        }
        const t = candidates(c).find(e => !tried.some(x => x.isSameNode(e) || (x.outerHTML === e.outerHTML && x.innerText === e.innerText)));
        if (!t) break;
        tried.push(t);
        log('点 Thinking 行 →', t);
        tap(t);
        high = await waitFor(findHigh, 1500);
      }
      if (!high) {
        const scope = (ctx() || { scope: document.body }).scope;
        lastDiag = scope.outerHTML.replace(/<svg[\s\S]*?<\/svg>/g, '<svg/>').slice(0, 15000);
        log('✗ 没展开出 High 选项。请点“复制诊断”把内容发给我');
        return;
      }
      const highBtn = clickTarget(high, (ctx() || { scope: document.body }).scope);
      const isChecked = el => el.getAttribute('aria-checked') === 'true' || el.classList.contains('selected');
      if (isChecked(highBtn)) { log('✓ 当前已经是 High'); closeMenu(); return; }
      log('点 High →', highBtn);
      tap(highBtn);
      await sleep(600);
      const again = findHigh();
      const ok = !again || isChecked(clickTarget(again, (ctx() || { scope: document.body }).scope));
      log(ok ? '✓ 完成，已切到 High' : '? 点了 High，但没看到选中标记，请看一下菜单');
      if (findThink()) closeMenu();
    } catch (e) {
      log('✗ 出错:', e && e.message);
    } finally {
      running = false;
    }
  }

  // ============ 触发时机 ============
  const isNewChat = () => /^\/(u\/\d+\/)?app\/?$/.test(location.pathname) || /^\/(u\/\d+\/)?gem\/[^/]+\/?$/.test(location.pathname);
  // 书签版重复点击时只重跑一次，不重复挂监听
  if (window.__geminiAutoHigh) { window.__geminiAutoHigh('manual'); return; }
  window.__geminiAutoHigh = run;
  run('load');
  let lastUrl = location.href;
  setInterval(() => {
    if (location.href === lastUrl) return;
    lastUrl = location.href;
    if (!ONLY_NEW_CHAT || isNewChat()) setTimeout(() => run('route'), 600);
  }, 500);
})();
