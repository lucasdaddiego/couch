// node --test: article.js's extraction with the real HTMLRewriter, in workerd through miniflare
// (wrangler's dependency: dev.zsh's npm ci installs it). CI runs node --test with no npm install,
// so there these tests are skipped (the reason names the missing module). The pages are the
// publishers' of 2026-10-14 for the items of the news_*.xml fixtures, trimmed: the head, the
// scripts, styles, SVGs, iframes, images, sources, links, metas and comments removed, the page
// cut about 12 KB after its last body paragraph (article_ambito.html 53 KB of 129 KB,
// article_clarin_politica.html 36 of 333, article_clarin_mundo.html 38 of 335, article_bbc.html
// 39 of 398). The paragraph counts were read on them with an HTML parser (Python's html.parser),
// the same selectors.
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
  why = `no miniflare (${(e && e.code) || e}): zsh couch/worker/dev.zsh test runs these`;
}
const skip = Miniflare ? false : why;
const root = fileURLToPath(new URL('..', import.meta.url));
const page = (f) => readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8');
let mf = null;

before(async () => {
  if (!Miniflare) return;
  const mod = (p) => ({ type: 'ESModule', path: join(root, p), contents: readFileSync(join(root, p), 'utf8') });
  // Miniflare 5 (wrangler 4.149's) takes the Worker in a `workers` array.
  mf = new Miniflare({
    workers: [{
      name: 'extract',
      modules: [mod('test/extract.worker.js'), mod('src/article.js'), mod('src/news.js'), mod('src/board.js')],
      modulesRoot: root,
      compatibilityDate: '2026-09-01',
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

test('workerd: Ambito\'s body paragraphs, not its lead, caption or paywall note', { skip }, async () => {
  const r = await run(page('article_ambito.html'), 'https://www.ambito.com/economia/organizaciones-cuestionan-el-acuerdo-el-fmi-su-impacto-social-y-ambiental-n6333767');
  assert.deepEqual([r.how, r.paras.length, r.stopped], ['ambito', 10, false]);
  assert.ok(r.paras[0].startsWith('Cinco entidades de la sociedad civil cuestionaron el programa económico acordado entre el Gobierno argentino'));
  assert.ok(r.paras[1].startsWith('El documento, titulado “Sostenibilidad de la deuda y el rol del FMI'));
  assert.ok(r.paras[9].startsWith('Las cinco entidades pidieron al FMI revisar el programa'));
  for (const s of ['Un informe presentado', 'Crecen las críticas', 'El programa con el FMI suma', 'exclusivo para suscriptores', 'Ya superaste']) assert.ok(!has(r.paras, s), s);
  assert.ok(r.paras.every((p) => p === p.trim() && !/[|\n<>]|&[a-z#0-9]+;/.test(p)));
  // UTF-8 split by the chunks: 7-byte chunks cut accents, quotes and tags, the same paragraphs
  const small = await run(page('article_ambito.html'), 'https://www.ambito.com/a', 7);
  assert.deepEqual([small.paras, small.pulls, small.chunks], [r.paras, small.chunks, small.chunks]);
});

test('workerd: Clarin\'s text blocks (politics, world), not the tweet, the newsletter or the initials', { skip }, async () => {
  const p = await run(page('article_clarin_politica.html'), 'https://www.clarin.com/politica/santiago-caputo-dijo-argentina-debe-recuperar-malvinas-reino-unido-colonizado-islam_0_8nCdK9IRgB.html');
  assert.deepEqual([p.how, p.paras.length], ['clarin', 13]);
  assert.ok(p.paras[0].startsWith('El asesor presidencial Santiago Caputo dio un paso más en la escalada diplomática argentina por Malvinas'));
  assert.ok(p.paras[12].startsWith('En el mensaje que posteó Gordo Dan y replicó Milei'));
  for (const s of ['TOMAR NOTA', 'Recibí en tu mail', 'Para disfrutar los contenidos']) assert.ok(!has(p.paras, s), s);
  const w = await run(page('article_clarin_mundo.html'), 'https://www.clarin.com/mundo/masiva-protesta-colombia-miles-personas-marchan-gobierno-abelardo-espriella_0_bWBYE3I85B.html');
  assert.deepEqual([w.how, w.paras.length, w.paras[21]], ['clarin', 22, 'Fuente: AFP y EFE']);   // "PB" (2 code points) dropped
  assert.ok(w.paras[0].startsWith('Miles de personas marchan este miércoles en las principales ciudades de Colombia'));
  assert.ok(!has(w.paras, 'Foto EFE'));                                                 // a picture's caption
});

test('workerd: BBC\'s text blocks, entities decoded, not the video notes, captions or promos', { skip }, async () => {
  const r = await run(page('article_bbc.html'), 'https://www.bbc.co.uk/news/articles/cwx28r0zvm6lo?at_medium=RSS&at_campaign=rss');
  assert.deepEqual([r.how, r.paras.length], ['bbc', 16]);
  assert.equal(r.paras[0], 'Pope Leo XIV\'s recovery from surgery to remove a lower lobe of his lung is "progressing as expected", his doctors have said.');
  assert.ok(r.paras[15].startsWith('Some adenocarcinomas are picked up early - before symptoms develop'));
  for (const s of ['This video can not be played', 'Watch:', 'What we know about the Pope', 'Pope Leo announced on Sunday']) assert.ok(!has(r.paras, s), s);
  // the same page under bbc.com (a redirect's host)
  assert.equal((await run(page('article_bbc.html'), 'https://www.bbc.com/news/articles/cwx28r0zvm6lo')).how, 'bbc');
});

test('workerd: the generic rule for another host or a changed layout; none for a page with no body', { skip }, async () => {
  const g = await run(page('article_clarin_politica.html'), 'https://example.org/nota');
  assert.ok(g.how === 'generic' && g.paras.length >= 10 && g.paras[0].startsWith('El asesor presidencial Santiago Caputo'));
  assert.ok(g.paras.every((p) => Array.from(p).length >= 40));
  const moved = await run(page('article_bbc.html'), 'https://www.ambito.com/nota');        // Ambito's rule finds nothing
  assert.ok(moved.how === 'generic' && has(moved.paras, 'Pope Leo XIV\'s recovery from surgery'));
  const none = await run('<html><body><article><p>Corto.</p></article><p>Fuera de un artículo, con más de cuarenta letras en total.</p></body></html>', 'https://www.ambito.com/x');
  assert.deepEqual([none.how, none.paras], ['none', []]);
});

test('workerd: the read stops QUIET_BYTES after the body, at ENOUGH_CHARS, at PAGE_BYTES; the rest is cancelled', { skip }, async () => {
  const filler = (n) => '<div class="relleno">relleno relleno relleno</div>'.repeat(Math.ceil(n / 50));
  const body = `<article class="article-body"><p>${'Texto del cuerpo, con acentos: país. '.repeat(10)}</p></article>`;
  const quiet = await run(`<html><body>${body}${filler(400000)}</body></html>`, 'https://www.ambito.com/x', 4096);
  assert.deepEqual([quiet.how, quiet.paras.length, quiet.stopped, quiet.cancelled], ['ambito', 1, true, true]);
  // the stop is checked as the input is read, the paragraphs are seen as the rewriter gets there:
  // a few chunks of slack
  assert.ok(quiet.pulls < quiet.chunks && quiet.bytes >= QUIET_BYTES && quiet.bytes < QUIET_BYTES + 160000, `${quiet.bytes}`);
  const long = `<html><body><article class="article-body">${`<p>${'palabra '.repeat(50)}</p>`.repeat(400)}</article></body></html>`;
  const enough = await run(long, 'https://www.ambito.com/x', 4096);
  assert.ok(enough.stopped && enough.cancelled && enough.paras.length < 400 && enough.paras.join('').length >= ENOUGH_CHARS * 0.9, `${enough.paras.length}`);
  const big = await run(`<html><body>${filler(PAGE_BYTES + 200000)}</body></html>`, 'https://example.org/x', 16384);
  assert.deepEqual([big.how, big.stopped, big.cancelled], ['none', true, true]);
  assert.ok(big.bytes >= PAGE_BYTES && big.bytes < PAGE_BYTES + 8 * 16384, `${big.bytes}`);
});
