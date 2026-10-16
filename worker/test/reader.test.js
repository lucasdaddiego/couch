// node --test: the news reader (GET /news, GET /article?id=, since 2026-10-14) against the feeds
// of NEWS_FEEDS of 2026-10-14 20:01 UTC (NEWS_NOW: news_ambito.xml, 20 items; news_clarin_politica.xml
// and news_clarin_mundo.xml, 10 each; news_bbc.xml, 26, not in order; each whole, as the feed gave
// it), with a fake fetch and a fake Cache API as board.test.js's. Node has no HTMLRewriter: here
// /article runs with a stand-in extraction (every <p> of a small page); the publishers' pages and
// the real extraction are article.workerd.test.js's (in workerd, through miniflare).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync } from 'node:fs';
import { clean, MAX_BYTES } from '../src/board.js';
import { CATEGORY_RE, decodeEntities, feedItems, FEED_ITEMS, itemId, parseFeed, parseFeeds, SUMMARY_MAX, TITLE_MAX, FULL_MAX } from '../src/news.js';
import { articleText, ARTICLE_BYTES, capParas, dateWords, LINK_BYTES, NEWS_BYTES, NEWS_LIST, newsText, PARA_MAX, quotas, TEXT_BYTES } from '../src/reader.js';
import { get, handle, newsUpstream, RETRY_S } from '../src/index.js';

const text = (f) => (!existsSync(new URL(`./fixtures/${f}`, import.meta.url)) ? '' : readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8'));
// the news fixtures (publishers' text) stay private: without them their tests are skipped
const NO_NEWS = existsSync(new URL('./fixtures/news_bbc.xml', import.meta.url)) ? false : "the publishers' feeds and pages are not in the public repo (test/fixtures/README.md)";
const feeds = {
  ambito: text('news_ambito.xml'), clPol: text('news_clarin_politica.xml'), clMun: text('news_clarin_mundo.xml'), bbc: text('news_bbc.xml'),
};
const NEWS_NOW = 1792008077; // 2026-10-14 20:01:17 UTC, 17:01 in Junin: the time of the news_*.xml fixtures
const wranglerVars = JSON.parse(readFileSync(new URL('../wrangler.jsonc', import.meta.url), 'utf8')
  .split('\n').filter((l) => !l.trimStart().startsWith('//')).join('\n')).vars;
const env = { ...wranglerVars };
const bytesOf = (s) => new TextEncoder().encode(s).length;
const utf8ok = (s) => new TextDecoder('utf-8', { fatal: true }).decode(new TextEncoder().encode(s)) === s;

// The four items whose pages article.workerd.test.js reads, by link.
const LINKS = {
  ambito: 'https://www.ambito.com/economia/organizaciones-cuestionan-el-acuerdo-el-fmi-su-impacto-social-y-ambiental-n6333767',
  clPol: 'https://www.clarin.com/politica/santiago-caputo-dijo-argentina-debe-recuperar-malvinas-reino-unido-colonizado-islam_0_8nCdK9IRgB.html',
  clMun: 'https://www.clarin.com/mundo/masiva-protesta-colombia-miles-personas-marchan-gobierno-abelardo-espriella_0_bWBYE3I85B.html',
  bbc: 'https://www.bbc.co.uk/news/articles/cwx28r0zvm6lo?at_medium=RSS&at_campaign=rss',
};

function fakeCache() {
  const m = new Map();
  return {
    m, ops: 0,
    async match(k) { this.ops++; return m.has(k) ? new Response(m.get(k)) : undefined; },
    async put(k, r) { this.ops++; m.set(k, await r.text()); },
  };
}

// Node has no HTMLRewriter: every <p> of the page, its tags stripped (the real rules: article.js).
async function fakeExtract(r) {
  const html = await r.text();
  return { paras: [...html.matchAll(/<p\b[^>]*>([\s\S]*?)<\/p>/g)].map((m) => clean(decodeEntities(m[1].replace(/<[^>]*>/g, ' ')))).filter(Boolean), how: 'fake' };
}

function deps({ cache = fakeCache(), answers = {}, now = NEWS_NOW, extract = fakeExtract } = {}) {
  const calls = [];
  return {
    calls, cache, waits: [], extract,
    now: () => now,
    waitUntil(p) { this.waits.push(p); },
    async fetch(url) {
      calls.push(url);
      const u = new URL(url);
      const a = answers[u.hostname + u.pathname + u.search] ?? answers[u.hostname + u.pathname] ?? answers[u.hostname];
      if (a instanceof Error) throw a;
      if (!a) throw new TypeError('no route');
      if (a.response) return a.response;
      return new Response(a.body, { status: a.status || 200, headers: { 'content-type': 'text/html; charset=utf-8' } });
    },
  };
}

const feedRoutes = {
  'www.ambito.com/rss/pages/economia.xml': { body: feeds.ambito },
  'www.clarin.com/rss/politica/': { body: feeds.clPol },
  'www.clarin.com/rss/mundo/': { body: feeds.clMun },
  'feeds.bbci.co.uk': { body: feeds.bbc },
};
const FEED_HOSTS = ['www.ambito.com/rss/', 'www.clarin.com/rss/', 'feeds.bbci.co.uk'];
const isFeed = (u) => FEED_HOSTS.some((h) => u.includes(h));
const pathOf = (link) => { const u = new URL(link); return u.hostname + u.pathname; };
const req = (path) => new Request(`http://couch.test${path}`);
const body = async (d, path, e = env) => {
  const r = await handle(req(path), e, d);
  assert.equal(r.status, 200);
  assert.equal(r.headers.get('content-type'), 'text/plain; charset=utf-8');
  const t = await r.text();
  await Promise.all(d.waits);
  return t;
};
const itemLines = (t) => t.split('\n').filter((l) => l.startsWith('item='));

// ---- the feeds: ids, links, summaries, categories --------------------------------------------------

test('feedItems: the id, the link, the whole title and the summary of each item, the newest first', { skip: NO_NEWS }, () => {
  const a = feedItems(feeds.ambito), p = feedItems(feeds.clPol), w = feedItems(feeds.clMun), b = feedItems(feeds.bbc);
  assert.deepEqual([a.length, p.length, w.length, b.length], [20, 10, 10, FEED_ITEMS]);    // BBC: its 20 newest of 26
  for (const x of [...a, ...p, ...w, ...b]) {
    assert.match(x.id, /^[0-9a-f]{8}$/);
    assert.ok(x.link.startsWith('https://') && Number.isFinite(x.at));
    assert.ok(Array.from(x.title).length <= TITLE_MAX && Array.from(x.full).length <= FULL_MAX && Array.from(x.summary).length <= SUMMARY_MAX);
    assert.ok(!/[|\n\r<>]/.test(x.title + x.full + x.summary));
  }
  assert.ok(b.every((x, i) => i === 0 || b[i - 1].at >= x.at));                         // BBC's feed is not in order: sorted
  // FNV-1a of the link's UTF-8 bytes, 8 hex digits
  assert.equal(itemId(LINKS.ambito), '565500e1');
  assert.deepEqual(a[0], {
    id: '565500e1', title: 'Organizaciones cuestionan el acuerdo con el FMI por su impacto social y ambiental',
    full: 'Organizaciones cuestionan el acuerdo con el FMI por su impacto social y ambiental', at: 1792006920, link: LINKS.ambito,
    summary: 'Un informe presentado por cinco entidades analiza las consecuencias del plan económico en la producción y la estabilidad del país. Además, advierte sobre posibles incumplimientos de compromisos internacionales en materia de derechos humanos y acción climática.',
  });
  // Clarin's summary is a <ul> of <li> in CDATA: the items' text with a space between them. Its
  // title has 125 code points: title is cut for the lists, full stays whole for /article.
  const caputo = p.find((x) => x.link === LINKS.clPol);
  assert.equal(caputo.id, '2c9e1345');
  assert.equal(caputo.summary, 'El asesor presidencial insistió que el Gobierno de Milei debe "proyectarse con firmeza hacia el Atlántico Sur". El mensaje fue replicado en redes por el Presidente.');
  assert.equal(caputo.full, 'Santiago Caputo dijo que Argentina debe recuperar las Malvinas porque el Reino Unido está "siendo colonizado por el Islam"');
  assert.ok(caputo.title.endsWith('...') && caputo.full.startsWith(caputo.title.slice(0, -3)));
  // BBC writes the link's "&" as "&amp;"
  assert.equal(b.find((x) => x.id === '2398f1f7').link, LINKS.bbc);
  assert.equal(w.find((x) => x.link === LINKS.clMun).id, 'f29dae8f');
  // every id differs over the four feeds; the Board's parseFeed keeps its shape and its 8
  assert.equal(new Set([...a, ...p, ...w, ...b].map((x) => x.id)).size, 60);
  assert.deepEqual(parseFeed(feeds.ambito), a.slice(0, 8).map(({ title, at }) => ({ title, at })));
});

test('feedItems: an id stays while its item is in the feed (a new item first, the oldest gone); no link: the title', { skip: NO_NEWS }, () => {
  const items = feeds.clMun.match(/<item\b[\s\S]*?<\/item>/g);
  const fresh = '<item><title>Nueva</title><link>https://www.clarin.com/mundo/nueva_0_x.html</link><pubDate>Wed, 14 Oct 2026 20:30:00 +0000</pubDate></item>';
  const later = feeds.clMun.replace(items[0], () => fresh + items[0]).replace(items.at(-1), () => '');
  const before = new Map(feedItems(feeds.clMun).map((x) => [x.link, x.id]));
  const after = feedItems(later);
  assert.equal(after.length, 10);
  assert.equal(after[0].title, 'Nueva');
  for (const x of after.slice(1)) assert.equal(x.id, before.get(x.link));
  const none = feedItems('<rss><item><title>Sin enlace</title><link>javascript:alert(1)</link></item></rss>')[0];
  assert.deepEqual([none.link, none.id], ['', itemId('', 'Sin enlace')]);
  assert.notEqual(none.id, itemId('Sin enlace'));
});

test('NEWS_FEEDS: the category after the weight; the older forms stay valid; a bad category skips the entry', () => {
  assert.deepEqual(parseFeeds('Ámbito*3@economy=https://a.example/rss,BBC @ World=https://b.example/,C=https://c.example/,D*2=https://d.example/'), [
    { label: 'Ámbito', url: 'https://a.example/rss', weight: 3, category: 'economy' },
    { label: 'BBC', url: 'https://b.example/', weight: 1, category: 'world' },          // case not counted
    { label: 'C', url: 'https://c.example/', weight: 1, category: '' },
    { label: 'D', url: 'https://d.example/', weight: 2, category: '' },
  ]);
  assert.deepEqual(parseFeeds('A@=https://a.example/,B@econ1=https://b.example/,C@a b=https://c.example/,D*2@politics=https://d.example/?q=a=b'),
    [{ label: 'D', url: 'https://d.example/?q=a=b', weight: 2, category: 'politics' }]);
  assert.ok(CATEGORY_RE.test('x'.repeat(16)) && !CATEGORY_RE.test('x'.repeat(17)));
  assert.deepEqual(parseFeeds(wranglerVars.NEWS_FEEDS).map((f) => [f.label, f.weight, f.category]),
    [['Ámbito', 3, 'economy'], ['Clarín', 3, 'politics'], ['Clarín', 1, 'world'], ['BBC', 1, 'world']]);
});

test('quotas: each feed\'s share by weight, largest remainder, ties to the earlier feed', () => {
  assert.deepEqual(quotas([3, 3, 1, 1], 30), [11, 11, 4, 4]);       // 3/8, 3/8, 2/8 of 30
  assert.deepEqual(quotas([3, 3, 1, 1], 8), [3, 3, 1, 1]);          // the Board's mix
  assert.deepEqual(quotas([1, 1, 1], 8), [3, 3, 2]);
  assert.deepEqual(quotas([undefined, 0, 2], 4), [1, 1, 2]);        // no weight: 1
  assert.deepEqual(quotas([2, 1], 0), [0, 0]);
});

// ---- GET /news ----------------------------------------------------------------------------------

test('/news: 30 headlines of the four feeds, 3/8 economy, 3/8 politics, 2/8 world, the newest first', { skip: NO_NEWS }, async () => {
  const d = deps({ answers: feedRoutes });
  const t = await body(d, '/news');
  // a cold cache: 4 fetches, 4 cache reads and 4 writes, 12 of the free plan's 50 subrequests
  assert.deepEqual([d.calls.length, d.cache.ops], [4, 8]);
  const lines = t.split('\n');
  assert.deepEqual(lines.slice(0, 2), ['couch-news 1', `now=${NEWS_NOW}`]);
  assert.ok(t.endsWith('\nnews_age=0\nnews_state=ok\nend\n'));
  const items = itemLines(t);
  assert.equal(items.length, NEWS_LIST);
  assert.equal(items[0], 'item=565500e1|Ámbito|economy|1157|Organizaciones cuestionan el acuerdo con el FMI por su impacto social y ambiental|' +
    'Un informe presentado por cinco entidades analiza las consecuencias del plan económico en la producción y la estabilidad del país. Además, advierte sobre posibles incumplimientos de compromisos internacionales en materia de derechos humanos y acción climática.');
  const f = items.map((l) => l.slice(5).split('|'));
  assert.ok(f.every((x) => x.length === 6 && /^[0-9a-f]{8}$/.test(x[0]) && /^\d+$/.test(x[3])));
  assert.equal(new Set(f.map((x) => x[0])).size, 30);
  assert.ok(f.every((x, i) => i === 0 || +f[i - 1][3] <= +x[3]));                    // the age grows: the newest first
  const count = (k, v) => f.filter((x) => x[k] === v).length;
  // quotas 11, 11, 4, 4; Clarin's politics feed has 10 items, so its 11th goes to Ambito (the next in turn)
  assert.deepEqual([count(2, 'economy'), count(2, 'politics'), count(2, 'world')], [12, 10, 8]);
  assert.deepEqual([count(1, 'Ámbito'), count(1, 'Clarín'), count(1, 'BBC')], [12, 14, 4]);
  assert.ok(utf8ok(t) && bytesOf(t) <= NEWS_BYTES && bytesOf(t) > 8000 && bytesOf(t) < 12000);   // 10205 bytes (a Python port)
  // warm: 4 cache reads, no fetch
  const w = deps({ cache: d.cache, answers: feedRoutes, now: NEWS_NOW + 60 });
  const again = await body(w, '/news');
  assert.deepEqual([w.calls.length, w.cache.ops - 8], [0, 4]);
  assert.equal(itemLines(again)[0], items[0].replace('|1157|', '|1217|'));
});

test('/news and /board share the feed cache: a /board after a /news fetches no feed', { skip: NO_NEWS }, async () => {
  const cache = fakeCache();
  await body(deps({ cache, answers: feedRoutes }), '/news');
  const d = deps({ cache, answers: feedRoutes, now: NEWS_NOW + 30 });
  const t = await body(d, '/board');
  assert.ok(t.startsWith('couch-board 1\n') && bytesOf(t) <= MAX_BYTES);
  assert.deepEqual(d.calls.filter(isFeed), []);
  assert.equal(t.split('\n').filter((l) => l.startsWith('news=')).length, 8);
  assert.ok(t.includes('\nnews=Ámbito|1187|Organizaciones cuestionan el acuerdo con el FMI por su impacto social y ambiental\n'));
});

test('/news: a dead feed leaves its share to the others, stale with its reason; no NEWS_FEEDS: none', { skip: NO_NEWS }, async () => {
  const d = deps({ answers: { ...feedRoutes, 'www.clarin.com/rss/politica/': { status: 503, body: 'x' } } });
  const t = await body(d, '/news');
  const f = itemLines(t).map((l) => l.split('|'));
  assert.equal(f.length, 30);
  assert.ok(!f.some((x) => x[2] === 'politics'));
  assert.ok(t.endsWith('\nnews_state=stale|clarin.com: HTTP 503\nend\n'));
  assert.equal(await body(deps(), '/news', { ...env, NEWS_FEEDS: '' }), `couch-news 1\nnow=${NEWS_NOW}\nnews_age=0\nnews_state=none|no NEWS_FEEDS\nend\n`);
  const dead = await body(deps(), '/news');                                             // every feed down, no cache
  assert.ok(itemLines(dead).length === 0 && /\nnews_state=none\|ambito\.com: no route\nend\n$/.test(dead));
});

test('/news: the size cap drops the oldest items; 3-byte text stays whole UTF-8', { skip: NO_NEWS }, () => {
  const many = Array.from({ length: 6 }, (_, j) => ({
    label: `Fuente${j}`, category: 'world', weight: 1,
    r: { state: 'ok', age: 0, data: Array.from({ length: 20 }, (_, i) => ({
      id: (j * 100 + i).toString(16).padStart(8, '0'), title: '€'.repeat(TITLE_MAX), summary: '€'.repeat(SUMMARY_MAX), at: NEWS_NOW - j - 10 * i,
    })) },
  }));
  const t = newsText({ now: NEWS_NOW, feeds: many });
  const n = itemLines(t).length;
  assert.ok(bytesOf(t) <= NEWS_BYTES && n >= 25 && n < 30 && utf8ok(t), `${bytesOf(t)} bytes, ${n} items`);
  const ages = itemLines(t).map((l) => +l.split('|')[3]);
  assert.ok(ages.every((x, i) => i === 0 || ages[i - 1] <= x) && ages.at(-1) < 60);    // the newest stay
  assert.ok(t.endsWith('\nnews_state=ok\nend\n'));
  // the same link in two feeds lists once (its first feed's)
  const dup = newsText({ now: NEWS_NOW, feeds: [many[0], { ...many[0], label: 'Otra' }].map((f) => ({ ...f, r: { ...f.r, data: f.r.data.slice(0, 2) } })) });
  assert.deepEqual(itemLines(dup).map((l) => l.split('|')[1]), ['Fuente0', 'Fuente0']);
});

// ---- GET /article?id= ---------------------------------------------------------------------------

const PAGE = '<html><body><p>Primer párrafo con acentos: económico, país.</p><div><p>Segundo | párrafo   con\n espacios.</p></div></body></html>';

test('/article: the item of a feed, its page\'s paragraphs, then the cache (1 h); 15 subrequests cold', { skip: NO_NEWS }, async () => {
  const d = deps({ answers: { ...feedRoutes, [pathOf(LINKS.ambito)]: { body: PAGE } } });
  const t = await body(d, '/article?id=565500e1');
  assert.equal(t, [
    'couch-article 1',
    `now=${NEWS_NOW}`,
    'id=565500e1',
    'source=Ámbito',
    'category=economy',
    'title=Organizaciones cuestionan el acuerdo con el FMI por su impacto social y ambiental',
    'date=Wed 14 Oct 16:42|1157',
    `link=${LINKS.ambito}`,
    'state=ok',
    'p=Primer párrafo con acentos: económico, país.',
    'p=Segundo párrafo con espacios.',
    'end',
    '',
  ].join('\n'));
  // 4 feeds and the page; 5 cache reads and 5 writes
  assert.deepEqual([d.calls.length, d.calls.at(-1), d.cache.ops], [5, LINKS.ambito, 10]);
  assert.ok([...d.cache.m.keys()].includes(`https://couch.internal/article?u=${encodeURIComponent(LINKS.ambito)}`));
  const w = deps({ cache: d.cache, answers: {}, now: NEWS_NOW + 3599 });
  assert.equal(await body(w, '/article?id=565500e1'), t.replace(`now=${NEWS_NOW}`, `now=${NEWS_NOW + 3599}`).replace('|1157\n', `|${1157 + 3599}\n`));
  // the feeds are old (NEWS_TTL 600 s) and fail (no route): their old items, 4 reads and 4 failure
  // marks; the article is not old: 1 read, no fetch
  assert.deepEqual([w.calls.filter((u) => !isFeed(u)), w.cache.ops - 10], [[], 9]);
  // the whole title on /article (125 code points), the cut one on the lists
  const c = deps({ answers: { ...feedRoutes, [pathOf(LINKS.clPol)]: { body: PAGE } } });
  const ct = await body(c, '/article?id=2c9e1345');
  assert.ok(ct.includes('\ntitle=Santiago Caputo dijo que Argentina debe recuperar las Malvinas porque el Reino Unido está "siendo colonizado por el Islam"\n'));
  assert.ok(ct.includes('\nsource=Clarín\ncategory=politics\n') && ct.includes('\ndate=Wed 14 Oct 15:42|4741\n'));
});

test('/article: a blocked page answers the summary at once, remembered (negative cache), tried again after RETRY_S', { skip: NO_NEWS }, async () => {
  const blocked = { ...feedRoutes, [pathOf(LINKS.bbc)]: { status: 403, body: 'Forbidden' } };
  const d = deps({ answers: blocked });
  const t = await body(d, '/article?id=2398f1f7');
  assert.equal(t, [
    'couch-article 1',
    `now=${NEWS_NOW}`,
    'id=2398f1f7',
    'source=BBC',
    'category=world',
    "title=Pope walking and recovering 'as expected' after surgery, doctors say",
    'date=Wed 14 Oct 12:12|17311',
    `link=${LINKS.bbc}`,
    'state=summary|bbc.co.uk: HTTP 403',
    'p=The 71-year-old Pope was diagnosed with lung cancer after the 90-minute procedure on Tuesday.',
    'end',
    '',
  ].join('\n'));
  const again = deps({ cache: d.cache, answers: blocked, now: NEWS_NOW + 60 });
  assert.ok((await body(again, '/article?id=2398f1f7')).includes('\nstate=summary|bbc.co.uk: HTTP 403\n'));
  assert.deepEqual(again.calls, []);                                                    // not waited for, not tried
  // after RETRY_S: the summary at once, one try after the reply; the page is back, so the next has it
  const back = { ...feedRoutes, [pathOf(LINKS.bbc)]: { body: PAGE } };
  const later = deps({ cache: d.cache, answers: back, now: NEWS_NOW + RETRY_S + 1 });
  assert.ok((await body(later, '/article?id=2398f1f7')).includes('\nstate=summary|bbc.co.uk: HTTP 403\n'));
  assert.deepEqual(later.calls, [LINKS.bbc]);
  const next = deps({ cache: d.cache, answers: {}, now: NEWS_NOW + RETRY_S + 2 });
  assert.ok((await body(next, '/article?id=2398f1f7')).includes('\nstate=ok\np=Primer párrafo con acentos: económico, país.\n'));
  // a page with no paragraphs, and the Worker's own extraction in Node (no HTMLRewriter): the summary
  const empty = deps({ answers: { ...feedRoutes, [pathOf(LINKS.clMun)]: { body: '<html><body>nada</body></html>' } } });
  assert.ok((await body(empty, '/article?id=f29dae8f')).includes('\nstate=summary|clarin.com: no paragraphs\np=Sindicatos, estudiantes'));
  const node = deps({ answers: { ...feedRoutes, [pathOf(LINKS.clMun)]: { body: PAGE } }, extract: null });
  assert.ok((await body(node, '/article?id=f29dae8f')).includes('\nstate=summary|clarin.com: no HTMLRewriter\n'));
  const slow = deps({ answers: { ...feedRoutes, [pathOf(LINKS.clMun)]: Object.assign(new Error('x'), { name: 'TimeoutError' }) } });
  assert.ok((await body(slow, '/article?id=f29dae8f')).includes('\nstate=summary|clarin.com: no answer in 8 s\n'));
});

test('/article: no open proxy: only an id of a cached feed item is fetched', { skip: NO_NEWS }, async () => {
  const evil = 'https://evil.example/x';
  const d = deps({ answers: { ...feedRoutes, 'evil.example': { body: PAGE } } });
  const t = await body(d, `/article?id=${itemId(evil)}`);
  assert.equal(t, `couch-article 1\nnow=${NEWS_NOW}\nstate=none|unknown id\nend\n`);
  assert.deepEqual(d.calls.filter((u) => !isFeed(u)), []);                              // only the 4 feeds
  for (const q of [`?id=${encodeURIComponent(evil)}`, '?url=https://evil.example/x', '', '?id=565500E1', '?id=565500e10']) {
    const b = deps({ answers: { ...feedRoutes, 'evil.example': { body: PAGE } } });
    assert.equal(await body(b, `/article${q}`), `couch-article 1\nnow=${NEWS_NOW}\nstate=none|bad id\nend\n`, q);
    assert.deepEqual(b.calls, []);
  }
  // other methods and paths: 404
  assert.equal((await handle(new Request('http://couch.test/article?id=565500e1', { method: 'POST' }), env, deps())).status, 404);
  assert.equal((await handle(req('/news/'), env, deps())).status, 404);
});

test('/article: the size caps (12 KB of paragraphs, whole ones, each 1500 code points), UTF-8 whole', { skip: NO_NEWS }, async () => {
  const big = Array.from({ length: 40 }, (_, i) => `${i} ${'€€€€€ '.repeat(400)}`);
  const d = deps({ answers: { ...feedRoutes, [pathOf(LINKS.clMun)]: { body: 'x' } }, extract: async () => ({ paras: big }) });
  const t = await body(d, '/article?id=f29dae8f');
  const ps = t.split('\n').filter((l) => l.startsWith('p='));
  assert.ok(t.includes('\nstate=cut\n') && ps.length >= 2 && utf8ok(t) && bytesOf(t) <= ARTICLE_BYTES);
  assert.ok(ps.reduce((n, l) => n + bytesOf(l) + 1, 0) <= TEXT_BYTES);
  assert.ok(ps.every((l, i) => l.startsWith(`p=${i} `) && l.endsWith('...') && Array.from(l.slice(2)).length <= PARA_MAX));
  // capParas: whole paragraphs in order, "|" and LF made spaces, empty ones dropped
  assert.deepEqual(capParas(['a|b', '', '  ', 'c\nd']), { paras: ['a b', 'c d'], cut: false });
  const fit = capParas(['x'.repeat(1000), 'y'.repeat(PARA_MAX + 50)]);
  assert.deepEqual([fit.paras.length, Array.from(fit.paras[1]).length <= PARA_MAX, fit.cut], [2, true, false]);
});

test('articleText: the lines in order; a long link goes; the date in TZ', () => {
  const item = { id: 'abcdef01', title: 'T...', full: 'T | entero', at: 1792000000, link: 'https://x.example/a|b', summary: 'S' };
  assert.equal(articleText({ now: 1792000600, tz: 'UTC', item, feed: { label: 'L', category: '' }, a: { state: 'cut', paras: ['uno', 'dos'] } }),
    'couch-article 1\nnow=1792000600\nid=abcdef01\nsource=L\ncategory=-\ntitle=T entero\ndate=Wed 14 Oct 17:46|600\nlink=https://x.example/a%7Cb\nstate=cut\np=uno\np=dos\nend\n');
  const long = articleText({ now: 1, item: { ...item, at: null, link: `https://x.example/${'a'.repeat(LINK_BYTES)}` }, feed: null, a: { state: 'summary', reason: 'x: HTTP 403', paras: [] } });
  assert.equal(long, 'couch-article 1\nnow=1\nid=abcdef01\nsource=-\ncategory=-\ntitle=T entero\ndate=-|-\nstate=summary|x: HTTP 403\nend\n');
  assert.equal(dateWords(1792006920, 'America/Argentina/Buenos_Aires'), 'Wed 14 Oct 16:42');
});

test('get: a feed\'s cache entry holds the items /news and /article need, under news2', { skip: NO_NEWS }, async () => {
  const d = deps({ answers: feedRoutes });
  const f = parseFeeds(env.NEWS_FEEDS)[3];
  const r = await get(newsUpstream(f), env, NEWS_NOW, d);
  await Promise.all(d.waits);
  assert.deepEqual([r.state, r.data.length, Object.keys(r.data[0]).sort()], ['ok', FEED_ITEMS, ['at', 'full', 'id', 'link', 'summary', 'title']]);
  assert.deepEqual([...d.cache.m.keys()], [`https://couch.internal/news2?u=${encodeURIComponent('https://feeds.bbci.co.uk/news/world/rss.xml')}`]);
});
