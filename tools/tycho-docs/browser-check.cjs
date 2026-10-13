// Run with Puppeteer against the generated website; see README.md.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const puppeteer = require('puppeteer');
const base = process.env.SITE_URL || 'http://127.0.0.1:8765';
const output = process.env.SHOTS;
(async () => {
  const browser = await puppeteer.launch({executablePath:process.env.CHROMIUM || '/usr/bin/chromium-browser',args:['--no-sandbox','--disable-dev-shm-usage']});
  try {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', error => errors.push(String(error)));
    let searchMode = 'normal';
    await page.setRequestInterception(true);
    page.on('request', async request => {
      if (request.url().endsWith('/search.json')) {
        if (searchMode === 'fail') return request.respond({status:503,body:'Unavailable'});
        if (searchMode === 'unsafe') return request.respond({status:200,contentType:'application/json',body:JSON.stringify([{u:'javascript:alert(1)',t:'bad',x:'bad'}])});
        if (searchMode === 'text') return request.respond({status:200,contentType:'application/json',body:JSON.stringify([{u:'tutorial.html',t:'<img src=x onerror="window.injected=true">',x:'img text'}])});
        if (searchMode === 'delay') await new Promise(resolve => setTimeout(resolve,800));
      }
      await request.continue();
    });
    if (output) fs.mkdirSync(output,{recursive:true});
    async function shot(name) { if (output) await page.screenshot({path:output+'/'+name+'.png'}); }
    async function go(route) { await page.goto(base+route,{waitUntil:'networkidle0'}); await page.evaluate(() => document.fonts.ready); }
    for (const width of [320,390,768,1024,1440]) for (const theme of ['light','dark']) {
      await page.setViewport({width,height:1000});
      await go('/'); await page.evaluate(t => localStorage.setItem('tycho-theme',t),theme); await go('/');
      const shell = await page.evaluate(() => ({
        header:document.querySelector('.masthead').innerText,
        footer:document.querySelector('.site-footer').innerText,
        width:document.querySelector('.page').getBoundingClientRect().width,
        left:document.querySelector('.page').getBoundingClientRect().left,
        max:getComputedStyle(document.querySelector('.page')).maxWidth,
        links:[...document.querySelectorAll('.masthead a,.site-footer a')].map(a => a.href)
      }));
      for (const route of ['/docs/','/docs/tutorial.html','/docs/reference/corelib.html']) {
        await go(route);
        const info = await page.evaluate(() => ({
          overflow:document.documentElement.scrollWidth>innerWidth,
          header:document.querySelector('.masthead').innerText,
          footer:document.querySelector('.site-footer').innerText,
          width:document.querySelector('.page').getBoundingClientRect().width,
          left:document.querySelector('.page').getBoundingClientRect().left,
          max:getComputedStyle(document.querySelector('.page')).maxWidth,
          links:[...document.querySelectorAll('.masthead a,.site-footer a')].map(a => a.href),
          theme:document.documentElement.dataset.theme,
          menu:document.querySelector('.docs-nav').open,
          toc:document.querySelector('.contents-nav')?.open,
          code:[...document.querySelectorAll('pre code')].map(c => getComputedStyle(c).color)
        }));
        assert(!info.overflow,`${route} overflow at ${width}`);
        for (const key of ['header','footer','width','left','max','links']) assert.deepEqual(info[key],shell[key],`${route} shared ${key} at ${width}`);
        assert.equal(info.theme,theme);
        assert.equal(info.menu,width>760);
        if (info.toc !== undefined) assert.equal(info.toc,width>1100);
        assert(info.code.every(c => c!=='rgb(0, 0, 0)'));
        if ([390,1440].includes(width)) await shot((route==='/docs/'?'docs':route.includes('corelib')?'corelib':'tutorial')+'-'+width+'-'+theme);
      }
    }
    await page.setViewport({width:320,height:900}); await go('/docs/');
    const index = await page.evaluate(async () => (await fetch('search.json')).json());
    assert(index.some(entry => entry.x.length>20000),'complete bodies indexed');
    for (const entry of index) {
      await page.goto(base+'/docs/'+entry.u,{waitUntil:'domcontentloaded'});
      assert(await page.evaluate(() => document.documentElement.scrollWidth<=innerWidth),entry.u);
      assert.equal(await page.$eval('.docs-nav [aria-current="page"]',a=>a.getAttribute('href').split('/').pop()),entry.u.split('/').pop());
    }
    await go('/docs/tutorial.html');
    await page.click('.docs-nav>summary'); assert(await page.$eval('.docs-nav',e=>e.open));
    await page.click('.docs-nav>summary'); assert(!(await page.$eval('.docs-nav',e=>e.open)));
    await page.click('.contents-nav>summary'); assert(await page.$eval('.contents-nav',e=>e.open));
    await page.click('.contents a'); assert(await page.evaluate(()=>location.hash.length>1));
    await page.setViewport({width:1440,height:1000}); await go('/docs/');
    await page.type('#q','strings'); await page.waitForSelector('#search-results a');
    assert((await page.$eval('#search-results a',a=>a.href)).endsWith('/reference/strings.html'));
    assert(await page.$('#search-results mark')); assert(await page.$eval('#search-results .excerpt',e=>e.textContent.length>20));
    await shot('search-desktop');
    await page.focus('#q'); await page.keyboard.press('ArrowDown'); assert.equal(await page.evaluate(()=>document.activeElement.tagName),'A');
    await page.keyboard.press('Escape'); assert(await page.$eval('#r',e=>e.hidden));
    await page.focus('body'); await page.keyboard.press('/'); assert.equal(await page.evaluate(()=>document.activeElement.id),'q');
    await page.type('#q','arrays slices'); await page.waitForSelector('#search-results a');
    assert((await page.$eval('#search-results a',a=>a.href)).endsWith('/reference/arrays-slices.html'));
    await page.click('#search-clear'); await page.type('#q','no_such_tycho_construct_9999');
    assert((await page.$eval('#search-status',e=>e.textContent)).startsWith('No matches'));
    searchMode='delay'; await page.goto(base+'/docs/',{waitUntil:'domcontentloaded'}); await page.type('#q','concurrency');
    await page.waitForSelector('#search-results a'); assert((await page.$eval('#search-results a',a=>a.href)).endsWith('/reference/concurrency.html'));
    searchMode='fail'; await go('/docs/'); await page.type('#q','arena'); await page.waitForSelector('#search-status button');
    searchMode='normal'; await page.click('#search-status button'); await page.waitForSelector('#search-results a');
    searchMode='unsafe'; await go('/docs/'); await page.type('#q','bad'); await page.waitForSelector('#search-status button');
    searchMode='text'; await go('/docs/'); await page.type('#q','img'); await page.waitForSelector('#search-results a');
    assert.equal(await page.$('#search-results img'),null); assert.equal(await page.evaluate(()=>window.injected),undefined);
    searchMode='normal'; await go('/docs/tutorial.html'); await page.click('#tbtn');
    const theme=await page.evaluate(()=>document.documentElement.dataset.theme); await go('/'); assert.equal(await page.evaluate(()=>document.documentElement.dataset.theme),theme);
    await go('/docs/tutorial.html'); await page.keyboard.press('Tab'); assert.equal(await page.evaluate(()=>document.activeElement.className),'skip');
    await page.keyboard.press('Enter'); assert.equal(await page.evaluate(()=>location.hash),'#main');
    await page.setJavaScriptEnabled(false); await go('/docs/tutorial.html'); assert(await page.$eval('.docs-nav',e=>e.open));
    await page.setJavaScriptEnabled(true); await page.setViewport({width:1440,height:1000});
    for (const route of ['/','/docs/tutorial.html']) {
      await go(route); await page.$eval('.site-footer',e=>e.scrollIntoView({block:'start',behavior:'instant'}));
      await shot(route==='/'?'home-footer':'docs-footer');
    }
    assert.deepEqual(errors,[]);
    console.log('PASS: shared shell/width at 320–1440px in both themes; all docs mobile overflow/active nav; sidebar and contents disclosures; ranked multi-term search, excerpts, keyboard, early typing, retry, URL validation/XSS; theme and no-JS navigation.');
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exit(1); });
