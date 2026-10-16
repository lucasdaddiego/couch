// node --test: article.js's extraction with the real HTMLRewriter, in workerd through miniflare
// (wrangler's dependency: dev.zsh's npm ci installs it). CI runs node --test with no npm install,
// so there these tests are skipped (the reason names the missing module). The pages are made up
// (test/fixtures/article_*.html): a page with BBC's text blocks, and one with plain <article> <p>.
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { PAGE_BYTES, QUIET_BYTES, ENOUGH_CHARS } from '../src/article.js';

let Miniflare = null, why = '';
try {
  const m = await import('miniflare');                 // CommonJS: the named export, else default's
  Miniflare = m.Miniflare || (m.default && m.default.Miniflare) || null;
  if (!Miniflare) why = 'miniflare has no Miniflare export';
} catch (e) {
  why = `no miniflare (${(e && e.code) || e}): zsh worker/dev.zsh test runs these`;
}
const skip = Miniflare ? false : why;
const root = fileURLToPath(new URL('..', import.meta.url));
const page = (f) => readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8');
let mf = null;

before(async () => {
  if (!Miniflare) return;
  // Miniflare 5 (wrangler 4.149's): each Worker is { config: { name, compatibilityDate, manifest } },
  // the manifest's modules a record of paths under modulesRoot.
  const files = ['test/extract.worker.js', 'src/article.js', 'src/news.js', 'src/board.js'];
  const modules = Object.fromEntries(files.map((p) => [p, { type: 'esm', contents: readFileSync(join(root, p), 'utf8') }]));
  mf = new Miniflare({
    workers: [{
      config: {
        name: 'extract',
        compatibilityDate: '2026-09-01',
        manifest: { mainModule: 'test/extract.worker.js', modulesRoot: root, modules },
      },
    }],
  });
});
after(async () => {
  if (mf) await mf.dispose();
});

async function run(html, url, chunk = 65536) {
  const r = await mf.dispatchFetch('http://extract.test/', { method: 'POST', body: JSON.stringify({ html, url, chunk }) });
  const t = await r.text();
  assert.equal(r.status, 200, t);
  return JSON.parse(t);
}
const has = (ps, s) => ps.some((p) => p.includes(s));

test('workerd: BBC\'s text blocks, not the captions, the header or the footer', { skip }, async () => {
  const r = await run(page('article_bbc.html'), 'https://www.bbc.co.uk/news/articles/x?at_medium=RSS');
  assert.equal(r.how, 'bbc');
  assert.equal(r.paras.length, 6);
  assert.ok(r.paras.every((p, i) => p.endsWith(`Paragraph ${i + 1} of the body.`)));
  assert.ok(!has(r.paras, 'caption') && !has(r.paras, 'Footer') && !has(r.paras, 'Skip to content'));
  assert.equal((await run(page('article_bbc.html'), 'https://www.bbc.com/news/articles/x')).how, 'bbc');
});

test('workerd: the generic rule for another host or a changed layout; none for a page with no body', { skip }, async () => {
  const g = await run(page('article_generic.html'), 'https://example.org/story');
  assert.deepEqual([g.how, g.paras.length], ['generic', 5]);                             // "Short." is under 40 code points
  assert.ok(g.paras.every((p) => Array.from(p).length >= 40) && !has(g.paras, 'Menu'));
  const moved = await run(page('article_generic.html'), 'https://www.bbc.co.uk/news/x');  // BBC's rule finds nothing
  assert.deepEqual([moved.how, moved.paras.length], ['generic', 5]);
  const none = await run('<html><body><article><p>Short.</p></article><p>Outside of an article, with more than forty letters.</p></body></html>', 'https://example.org/x');
  assert.deepEqual([none.how, none.paras], ['none', []]);
});

test('workerd: the read stops QUIET_BYTES after the body, at ENOUGH_CHARS, at PAGE_BYTES; the rest is cancelled', { skip }, async () => {
  const filler = (n) => '<div class="filler">filler filler filler filler</div>'.repeat(Math.ceil(n / 50));
  const body = `<div data-block="text"><p>${'Body text, with accents: caf\u00e9. '.repeat(10)}</p></div>`;
  const quiet = await run(`<html><body>${body}${filler(400000)}</body></html>`, 'https://www.bbc.co.uk/x', 4096);
  assert.deepEqual([quiet.how, quiet.paras.length, quiet.stopped, quiet.cancelled], ['bbc', 1, true, true]);
  // the stop is checked as the input is read, the paragraphs are seen as the rewriter gets there:
  // a few chunks of slack
  assert.ok(quiet.pulls < quiet.chunks && quiet.bytes >= QUIET_BYTES && quiet.bytes < QUIET_BYTES + 160000, `${quiet.bytes}`);
  const long = `<html><body><div data-block="text">${`<p>${'word '.repeat(80)}</p>`.repeat(400)}</div></body></html>`;
  const enough = await run(long, 'https://www.bbc.co.uk/x', 4096);
  assert.ok(enough.stopped && enough.cancelled && enough.paras.length < 400 && enough.paras.join('').length >= ENOUGH_CHARS * 0.9, `${enough.paras.length}`);
  const big = await run(`<html><body>${filler(PAGE_BYTES + 200000)}</body></html>`, 'https://example.org/x', 16384);
  assert.deepEqual([big.how, big.stopped, big.cancelled], ['none', true, true]);
  assert.ok(big.bytes >= PAGE_BYTES && big.bytes < PAGE_BYTES + 8 * 16384, `${big.bytes}`);
});
