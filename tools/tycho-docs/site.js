'use strict';
(function () {
  const themeButton = document.getElementById('tbtn');
  function theme() { return document.documentElement.dataset.theme || (matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'); }
  function setTheme(value) {
    document.documentElement.dataset.theme = value;
    const meta = document.querySelector('meta[name="theme-color"]');
    if (meta) meta.content = value === 'dark' ? '#000000' : '#ffffff';
    if (themeButton) {
      themeButton.textContent = value === 'dark' ? 'Light' : 'Dark';
      themeButton.setAttribute('aria-label', 'Switch to ' + (value === 'dark' ? 'light' : 'dark') + ' theme');
    }
  }
  setTheme(theme());
  if (themeButton) themeButton.addEventListener('click', () => {
    const value = theme() === 'dark' ? 'light' : 'dark';
    setTheme(value);
    try { localStorage.setItem('tycho-theme', value); } catch (_) {}
  });

  const menu = document.querySelector('.docs-nav');
  if (menu) {
    const small = matchMedia('(max-width: 760px)');
    function resizeMenu() { menu.open = !small.matches; }
    resizeMenu();
    small.addEventListener('change', resizeMenu);
  }

  const contents = document.querySelector('.contents-nav');
  if (contents) {
    const narrow = matchMedia('(max-width: 1100px)');
    function resizeContents() { contents.open = !narrow.matches; }
    resizeContents();
    narrow.addEventListener('change', resizeContents);
  }

  const box = document.getElementById('q');
  if (!box) return;
  const panel = document.getElementById('r');
  const status = document.getElementById('search-status');
  const list = document.getElementById('search-results');
  const clear = document.getElementById('search-clear');
  const root = box.dataset.root;
  let index = null, failure = false, loading = false;
  const decoder = document.createElement('textarea');
  function decode(text) { decoder.innerHTML = text; return decoder.value; }
  function normalized(text) { return text.normalize('NFKC').toLowerCase(); }
  function terms(text) { return [...new Set(normalized(text).match(/[\p{L}\p{N}_:$]+/gu) || [])]; }
  function node(tag, text, cls) {
    const el = document.createElement(tag);
    if (text !== undefined) el.textContent = text;
    if (cls) el.className = cls;
    return el;
  }
  function highlight(el, text, words) {
    const escaped = words.map(w => w.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')).sort((a,b) => b.length-a.length);
    const pattern = new RegExp(escaped.join('|'), 'gi');
    let at = 0;
    for (const match of text.matchAll(pattern)) {
      el.append(document.createTextNode(text.slice(at, match.index)), node('mark', match[0]));
      at = match.index + match[0].length;
    }
    el.append(document.createTextNode(text.slice(at)));
  }
  function render() {
    const query = box.value.trim(), words = terms(query);
    clear.hidden = !query;
    panel.hidden = !query;
    list.replaceChildren();
    if (!query) { status.textContent = ''; return; }
    if (!words.length) { status.textContent = 'Enter a word or a language construct to search.'; return; }
    if (failure) {
      status.textContent = 'Search could not load. You can still browse the sidebar. ';
      const retry = node('button', 'Retry search');
      retry.type = 'button'; retry.addEventListener('click', load); status.append(retry); return;
    }
    if (!index) { status.textContent = 'Loading search…'; return; }
    const phrase = normalized(query);
    const matches = index.flatMap(entry => {
      const title = normalized(entry.t), text = normalized(entry.x), path = normalized(entry.u);
      if (!words.every(word => title.includes(word) || text.includes(word) || path.includes(word))) return [];
      let score = (title === phrase ? 1000 : title.includes(phrase) ? 500 : 0);
      score += words.reduce((n,word) => n + (title.includes(word) ? 80 : 0) + (path.includes(word) ? 20 : 0), 0);
      if (entry.u.startsWith('reference/')) score += 10;
      if (entry.u.startsWith('internals/') || entry.u.startsWith('rfc/')) score -= 20;
      return [{entry, score}];
    }).sort((a,b) => b.score-a.score || a.entry.t.localeCompare(b.entry.t));
    status.textContent = matches.length ? matches.length + (matches.length === 1 ? ' matching page' : ' matching pages') + (matches.length > 15 ? ' · showing the top 15' : '') : 'No matches. Try fewer words or a different construct.';
    for (const {entry} of matches.slice(0,15)) {
      const link = node('a'); link.href = root + entry.u;
      const title = node('span', undefined, 'result-title'); highlight(title, entry.t, words);
      const path = node('span', entry.u.replace(/\.html$/, ''), 'result-path');
      const text = entry.x.replace(/\s+/g, ' ').trim();
      const lower = normalized(text);
      const found = words.map(w => lower.indexOf(w)).filter(i => i >= 0);
      const start = found.length ? Math.max(0, Math.min(...found)-65) : 0;
      const excerpt = node('span', undefined, 'excerpt');
      highlight(excerpt, (start ? '…' : '') + text.slice(start,start+210) + (text.length > start+210 ? '…' : ''), words);
      link.append(title,path,excerpt);
      const item = node('li'); item.append(link); list.append(item);
    }
  }
  async function load() {
    if (loading || index) return;
    loading = true; failure = false; render();
    try {
      const response = await fetch(root + 'search.json');
      if (!response.ok) throw new Error('Search HTTP ' + response.status);
      const entries = await response.json();
      if (!Array.isArray(entries) || !entries.every(e => typeof e.u === 'string' && typeof e.t === 'string' && typeof e.x === 'string' && /^[a-zA-Z0-9_\-/]+\.html$/.test(e.u) && !e.u.includes('..'))) throw new Error('Invalid search index');
      index = entries.map(e => ({...e, x:decode(e.x)}));
    } catch (_) { failure = true; }
    loading = false; render();
  }
  box.addEventListener('focus', load);
  box.addEventListener('input', render);
  clear.addEventListener('click', () => { box.value = ''; render(); box.focus(); });
  document.addEventListener('keydown', event => {
    const editing = event.target.closest('input,textarea,[contenteditable="true"]');
    if ((event.key === '/' && !editing) || (event.key.toLowerCase() === 'k' && (event.ctrlKey || event.metaKey))) {
      event.preventDefault(); box.focus(); box.select(); box.scrollIntoView({block:'center'});
    }
    if (event.key === 'Escape' && (event.target === box || panel.contains(event.target))) { box.value = ''; render(); box.focus(); }
    if (['ArrowDown','ArrowUp'].includes(event.key) && (event.target === box || list.contains(event.target))) {
      const links = [...list.querySelectorAll('a')]; if (!links.length) return;
      event.preventDefault();
      const at = links.indexOf(document.activeElement);
      const next = at < 0 ? (event.key === 'ArrowDown' ? 0 : links.length-1) : (at + (event.key === 'ArrowDown' ? 1 : links.length-1)) % links.length;
      links[next].focus();
    }
  });
})();
