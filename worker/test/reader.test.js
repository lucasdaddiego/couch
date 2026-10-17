// node --test: the news reader (GET /news, GET /article?id=) against three made-up RSS feeds
// (test/fixtures/feed_*.xml: 13, 10 and 8 items, newest first) with a fake fetch and a fake Cache
// API as board.test.js's. Node has no HTMLRewriter: here /article runs with a stand-in extraction
// (every <p> of a small page); the real extraction is article.workerd.test.js's (in workerd,
// through miniflare).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { clean, MAX_BYTES } from '../src/board.js';
import { CATEGORY_RE, decodeEntities, feedItems, FEED_ITEMS, ITEMS_MAX, itemId, parseFeeds, SORT_ITEMS, SUMMARY_MAX, TITLE_MAX, FULL_MAX } from '../src/news.js';
import { articleText, ARTICLE_BYTES, capParas, dateWords, LINK_BYTES, NEWS_BYTES, NEWS_LIST, newsText, PARA_MAX, quotas, TEXT_BYTES } from '../src/reader.js';
import { get, handle, LISTED_MAX, newsUpstream, RETRY_S } from '../src/index.js';

const text = (f) => readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8');
const feeds = { world: text('feed_world.xml'), business: text('feed_business.xml'), tech: text('feed_tech.xml') };
const NOW = 1792164000; // 2026-10-16 15:20 UTC: each feed's newest item is 10 min older
const wranglerVars = JSON.parse(text('../../wrangler.jsonc').split('\n').filter((l) => !l.trimStart().startsWith('//')).join('\n')).vars;
const env = { ...wranglerVars };
const bytesOf = (s) => new TextEncoder().encode(s).length;
const utf8ok = (s) => new TextDecoder('utf-8', { fatal: true }).decode(new TextEncoder().encode(s)) === s;

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

function deps({ cache = fakeCache(), answers = {}, now = NOW, extract = fakeExtract } = {}) {
  const calls = [];
  return {
    calls, cache, waits: [], extract,
    now: () => now,
    waitUntil(p) { this.waits.push(p); },
    async fetch(url) {
      calls.push(url);
      const u = new URL(url);
      const a = answers[u.hostname + u.pathname] ?? answers[u.hostname];
      if (a instanceof Error) throw a;
      if (!a) throw new TypeError('no route');
      if (a.response) return a.response;
      return new Response(a.body, { status: a.status || 200, headers: { 'content-type': 'text/html; charset=utf-8' } });
    },
  };
}

const feedRoutes = {
  'feeds.bbci.co.uk/news/world/rss.xml': { body: feeds.world },
  'feeds.bbci.co.uk/news/business/rss.xml': { body: feeds.business },
  'feeds.bbci.co.uk/news/technology/rss.xml': { body: feeds.tech },
};
const isFeed = (u) => u.includes('feeds.bbci.co.uk');
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
const first = feedItems(feeds.world)[0];

// ---- the feeds: ids, links, summaries, categories --------------------------------------------------

test('feedItems: the id, the link, the whole title and the summary of each item, the newest first', () => {
  const w = feedItems(feeds.world), b = feedItems(feeds.business), t = feedItems(feeds.tech);
  assert.deepEqual([w.length, b.length, t.length], [13, 10, 8]);
  for (const x of [...w, ...b, ...t]) {
    assert.match(x.id, /^[0-9a-f]{8}$/);
    assert.ok(x.link.startsWith('https://news.example.org/') && Number.isFinite(x.at));
    assert.ok(Array.from(x.title).length <= TITLE_MAX && Array.from(x.full).length <= FULL_MAX && Array.from(x.summary).length <= SUMMARY_MAX);
    assert.ok(!/[|\n\r<>]/.test(x.title + x.full + x.summary));
  }
  assert.equal(first.id, itemId(first.link));
  assert.deepEqual([first.title, first.at, first.summary], ['Leaders meet to agree on a new plan for river shipping', NOW - 600,
    'A short summary of the story: leaders meet to agree on a new plan for river shipping. The full text is on the page.']);
  assert.equal(new Set([...w, ...b, ...t].map((x) => x.id)).size, 31);
  assert.deepEqual(feedItems(feeds.world, ITEMS_MAX), w.slice(0, 8));                // the Board takes the first ITEMS_MAX
  // an id stays while its item is in the feed; no link: the title gives the id
  const later = feeds.world.replace('<item>', '<item><title>New</title><link>https://news.example.org/world/new</link><pubDate>Fri, 16 Oct 2026 15:15:00 GMT</pubDate></item><item>');
  const after = feedItems(later);
  assert.deepEqual([after[0].title, after[1].id], ['New', first.id]);
  const none = feedItems('<rss><item><title>No link</title><link>javascript:alert(1)</link></item></rss>')[0];
  assert.deepEqual([none.link, none.id], ['', itemId('', 'No link')]);
  assert.ok(FEED_ITEMS >= 13);
});

test('feedItems: a feed that is not newest first: the newest by date, only they parsed, as a parse of every item gives', () => {
  // 110 items (more than SORT_ITEMS) from the fixtures' 31, each with its own link and a date out of
  // order; some with the date of the item before, some with no date, some with an empty title
  const base = Object.values(feeds).flatMap((x) => x.match(/<item\b[\s\S]*?<\/item>/g));
  let seed = 7, prev = '';
  const xs = Array.from({ length: 110 }, (_, k) => {
    seed = (seed * 1103515245 + 12345) % 2147483648;
    const date = k % 13 === 12 ? prev : new Date((NOW - (seed % 86400)) * 1000).toUTCString();
    prev = date;
    const x = base[k % base.length].replace(/<link\b[^>]*>[\s\S]*?<\/link>/, `<link>https://news.example.org/x/${k}</link>`)
      .replace(/<pubDate\b[^>]*>[\s\S]*?<\/pubDate>/, k % 17 === 5 ? '' : `<pubDate>${date}</pubDate>`);
    return k % 19 === 3 ? x.replace(/<title\b[^>]*>[\s\S]*?<\/title>/, '<title> </title>') : x;
  });
  const xml = `<rss><channel>${xs.join('\n')}</channel></rss>`;
  // the reference: each of the first SORT_ITEMS items parsed alone, the empty titles skipped, then a
  // stable sort by date (the newest first, no date last)
  const each = xs.slice(0, SORT_ITEMS).map((x) => feedItems(`<rss>${x}</rss>`, 1)[0]).filter(Boolean)
    .sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity));
  assert.ok(each.length > 90 && each.some((x) => x.at === null) && new Set(each.map((x) => x.at)).size < each.length);
  for (const max of [ITEMS_MAX, FEED_ITEMS, SORT_ITEMS]) assert.deepEqual(feedItems(xml, max), each.slice(0, max), `max ${max}`);
  // only the kept items are parsed (each parse makes one URL, feedLink's), not every item read
  const RealURL = globalThis.URL, empties = xs.filter((x) => x.includes('<title> </title>')).length;
  let made = 0;
  globalThis.URL = class extends RealURL {
    constructor(...a) {
      super(...a);
      made++;
    }
  };
  try {
    feedItems(xml, FEED_ITEMS);
  } finally {
    globalThis.URL = RealURL;
  }
  assert.ok(made >= FEED_ITEMS && made <= FEED_ITEMS + empties, `${made} items parsed`);
});

test('NEWS_FEEDS: the category after the weight; the older forms stay valid; a bad category skips the entry', () => {
  assert.deepEqual(parseFeeds('Sur*3@economy=https://a.example/rss,BBC @ World=https://b.example/,C=https://c.example/,D*2=https://d.example/'), [
    { label: 'Sur', url: 'https://a.example/rss', weight: 3, category: 'economy' },
    { label: 'BBC', url: 'https://b.example/', weight: 1, category: 'world' },          // case not counted
    { label: 'C', url: 'https://c.example/', weight: 1, category: '' },
    { label: 'D', url: 'https://d.example/', weight: 2, category: '' },
  ]);
  assert.deepEqual(parseFeeds('A@=https://a.example/,B@econ1=https://b.example/,C@a b=https://c.example/,D*2@politics=https://d.example/?q=a=b'),
    [{ label: 'D', url: 'https://d.example/?q=a=b', weight: 2, category: 'politics' }]);
  assert.ok(CATEGORY_RE.test('x'.repeat(16)) && !CATEGORY_RE.test('x'.repeat(17)));
});

test('quotas: each feed\'s share by weight, largest remainder, ties to the earlier feed', () => {
  assert.deepEqual(quotas([3, 3, 1, 1], 30), [11, 11, 4, 4]);
  assert.deepEqual(quotas([4, 2, 2], 8), [4, 2, 2]);
  assert.deepEqual(quotas([1, 1, 1], 8), [3, 3, 2]);
  assert.deepEqual(quotas([undefined, 0, 2], 4), [1, 1, 2]);        // no weight: 1
  assert.deepEqual(quotas([2, 1], 0), [0, 0]);
});

// ---- GET /news ----------------------------------------------------------------------------------

test('/news: the headlines of the feeds by weight, the newest first; then from the cache', async () => {
  const d = deps({ answers: feedRoutes });
  const t = await body(d, '/news');
  await Promise.all(d.waits);
  assert.deepEqual([d.calls.length, d.cache.ops], [3, 8]);                               // cold: 3 fetches, 3 reads, 3 writes, the listed items' read and write
  assert.deepEqual(t.split('\n').slice(0, 2), ['couch-news 1', `now=${NOW}`]);
  assert.ok(t.endsWith('\nnews_age=0\nnews_state=ok\nend\n'));
  const f = itemLines(t).map((l) => l.slice(5).split('|'));
  assert.equal(f.length, NEWS_LIST);
  assert.deepEqual(f[0], [first.id, 'BBC', 'world', '600', first.title]);             // no summary since 0.5.0's review
  assert.ok(f.every((x) => x.length === 5 && /^[0-9a-f]{8}$/.test(x[0]) && /^\d+$/.test(x[3])));
  assert.ok(f.every((x, i) => i === 0 || +f[i - 1][3] <= +x[3]));
  assert.ok(utf8ok(t) && bytesOf(t) <= NEWS_BYTES);
  const w = deps({ cache: d.cache, answers: feedRoutes, now: NOW + 60 });
  const again = await body(w, '/news');
  await Promise.all(w.waits);
  assert.deepEqual([w.calls.length, w.cache.ops - 8], [0, 4]);                           // nothing new: no write
  assert.equal(itemLines(again)[0].split('|')[3], '660');
});

test('/news and /board share the feed cache: a /board after a /news fetches no feed', async () => {
  const cache = fakeCache();
  await body(deps({ cache, answers: feedRoutes }), '/news');
  const d = deps({ cache, answers: feedRoutes, now: NOW + 30 });
  const t = await body(d, '/board');
  assert.ok(t.startsWith('couch-board 1\n') && bytesOf(t) <= MAX_BYTES);
  assert.deepEqual(d.calls.filter(isFeed), []);
  assert.equal(t.split('\n').filter((l) => l.startsWith('news=')).length, 8);
  assert.ok(t.includes(`\nnews=BBC|630|${first.title}\n`));
});

test('/news: a dead feed leaves its share to the others, stale with its reason; no NEWS_FEEDS: none', async () => {
  const d = deps({ answers: { ...feedRoutes, 'feeds.bbci.co.uk/news/business/rss.xml': { status: 503, body: 'x' } } });
  const t = await body(d, '/news');
  const f = itemLines(t).map((l) => l.split('|'));
  assert.equal(f.length, 21);                                                            // 13 + 8: every item of the two others
  assert.ok(!f.some((x) => x[2] === 'business'));
  assert.ok(t.endsWith('\nnews_state=stale|feeds.bbci.co.uk: HTTP 503\nend\n'));
  assert.equal(await body(deps(), '/news', { ...env, NEWS_FEEDS: '' }), `couch-news 1\nnow=${NOW}\nnews_age=0\nnews_state=none|no NEWS_FEEDS\nend\n`);
  const dead = await body(deps(), '/news');
  assert.ok(itemLines(dead).length === 0 && /\nnews_state=none\|feeds\.bbci\.co\.uk: no route\nend\n$/.test(dead));
});

test('/news: the size cap drops the oldest items; 3-byte text stays whole UTF-8', () => {
  const many = Array.from({ length: 6 }, (_, j) => ({
    label: `Source${j}`, category: 'world', weight: 1,
    r: { state: 'ok', age: 0, data: Array.from({ length: 20 }, (_, i) => ({
      id: (j * 100 + i).toString(16).padStart(8, '0'), title: '€'.repeat(TITLE_MAX), summary: '€'.repeat(SUMMARY_MAX), at: NOW - j - 10 * i,
    })) },
  }));
  // 120 items of 110 three-byte code points (no summary since 0.5.0's review: 30 such items fit)
  const t = newsText({ now: NOW, feeds: many, max: 120 });
  const n = itemLines(t).length;
  assert.ok(bytesOf(t) <= NEWS_BYTES && n >= 80 && n < 120 && utf8ok(t), `${bytesOf(t)} bytes, ${n} items`);
  const ages = itemLines(t).map((l) => +l.split('|')[3]);
  const all = many.flatMap((f) => f.r.data.map((x) => NOW - x.at)).sort((a, b) => a - b);
  assert.deepEqual(ages, all.slice(0, n));                                             // the newest stay
  assert.ok(t.endsWith('\nnews_state=ok\nend\n'));
  const dup = newsText({ now: NOW, feeds: [many[0], { ...many[0], label: 'Other' }].map((f) => ({ ...f, r: { ...f.r, data: f.r.data.slice(0, 2) } })) });
  assert.deepEqual(itemLines(dup).map((l) => l.split('|')[1]), ['Source0', 'Source0']);   // the same item lists once
});

// ---- GET /article?id= ---------------------------------------------------------------------------

const PAGE = '<html><body><p>First paragraph: café, naïve.</p><div><p>Second | paragraph   with\n spaces.</p></div></body></html>';

test('/article: the item of a feed, its page\'s paragraphs, then the cache (1 h)', async () => {
  const d = deps({ answers: { ...feedRoutes, [pathOf(first.link)]: { body: PAGE } } });
  const t = await body(d, `/article?id=${first.id}`);
  assert.equal(t, [
    'couch-article 1', `now=${NOW}`, `id=${first.id}`, 'source=BBC', 'category=world', `title=${first.title}`,
    'date=Fri 16 Oct 17:10|600', `link=${first.link}`, 'state=ok', 'p=First paragraph: café, naïve.', 'p=Second paragraph with spaces.', 'end', '',
  ].join('\n'));
  assert.deepEqual([d.calls.length, d.calls.at(-1), d.cache.ops], [4, first.link, 8]);   // 3 feeds and the page
  const w = deps({ cache: d.cache, answers: {}, now: NOW + 3599 });
  assert.equal(await body(w, `/article?id=${first.id}`), t.replace(`now=${NOW}`, `now=${NOW + 3599}`).replace('|600\n', `|${600 + 3599}\n`));
  assert.deepEqual(w.calls.filter((u) => !isFeed(u)), []);
});

test('/article: a blocked page answers the summary at once, remembered (negative cache), tried again after RETRY_S', async () => {
  const blocked = { ...feedRoutes, [pathOf(first.link)]: { status: 403, body: 'Forbidden' } };
  const d = deps({ answers: blocked });
  const t = await body(d, `/article?id=${first.id}`);
  assert.ok(t.includes(`\nstate=summary|news.example.org: HTTP 403\np=${first.summary}\nend\n`));
  const again = deps({ cache: d.cache, answers: blocked, now: NOW + 60 });
  assert.ok((await body(again, `/article?id=${first.id}`)).includes('\nstate=summary|news.example.org: HTTP 403\n'));
  assert.deepEqual(again.calls, []);
  const back = { ...feedRoutes, [pathOf(first.link)]: { body: PAGE } };
  const later = deps({ cache: d.cache, answers: back, now: NOW + RETRY_S + 1 });
  assert.ok((await body(later, `/article?id=${first.id}`)).includes('\nstate=summary|news.example.org: HTTP 403\n'));
  assert.deepEqual(later.calls, [first.link]);
  const next = deps({ cache: d.cache, answers: {}, now: NOW + RETRY_S + 2 });
  assert.ok((await body(next, `/article?id=${first.id}`)).includes('\nstate=ok\np=First paragraph'));
  const node = deps({ answers: { ...feedRoutes, [pathOf(first.link)]: { body: PAGE } }, extract: null });
  assert.ok((await body(node, `/article?id=${first.id}`)).includes('\nstate=summary|news.example.org: no HTMLRewriter\n'));
  const slow = deps({ answers: { ...feedRoutes, [pathOf(first.link)]: Object.assign(new Error('x'), { name: 'TimeoutError' }) } });
  assert.ok((await body(slow, `/article?id=${first.id}`)).includes('\nstate=summary|news.example.org: no answer in 8 s\n'));
});

test('/article: a headline that left its feed after /news listed it still opens (the listed items)', async () => {
  const d = deps({ answers: feedRoutes });
  await body(d, '/news');
  const listed = JSON.parse(d.cache.m.get('https://couch.internal/listed')).items;
  assert.ok(listed.length <= LISTED_MAX && listed.some((x) => x.id === first.id && x.label === 'BBC' && x.category === 'world'));
  // 11 min later the world feed no longer holds the item (NEWS_TTL 600 s: fetched again)
  const item0 = feeds.world.match(/<item\b[\s\S]*?<\/item>/g).find((x) => x.includes(first.link.replace(/&/g, '&amp;')) || x.includes(first.link));
  const gone = { ...feedRoutes, 'feeds.bbci.co.uk/news/world/rss.xml': { body: feeds.world.replace(item0, '') }, [pathOf(first.link)]: { body: PAGE } };
  const l = deps({ cache: d.cache, answers: gone, now: NOW + 660 });
  const t = await body(l, `/article?id=${first.id}`);
  assert.ok(t.includes('\nsource=BBC\ncategory=world\n') && t.includes('\nstate=ok\np=First paragraph: café, naïve.\n'), t);
  assert.equal(l.calls.at(-1), first.link);
  assert.equal(await body(deps({ cache: d.cache, answers: gone, now: NOW + 660 }), `/article?id=${itemId('https://evil.example/x')}`),
    `couch-article 1\nnow=${NOW + 660}\nstate=none|unknown id\nend\n`);
});

test('/article: no open proxy: only an id of a cached feed item is fetched', async () => {
  const evil = 'https://evil.example/x';
  const d = deps({ answers: { ...feedRoutes, 'evil.example': { body: PAGE } } });
  assert.equal(await body(d, `/article?id=${itemId(evil)}`), `couch-article 1\nnow=${NOW}\nstate=none|unknown id\nend\n`);
  assert.deepEqual(d.calls.filter((u) => !isFeed(u)), []);
  for (const q of [`?id=${encodeURIComponent(evil)}`, '?url=https://evil.example/x', '', `?id=${first.id.toUpperCase()}`, `?id=${first.id}0`]) {
    const b = deps({ answers: { ...feedRoutes, 'evil.example': { body: PAGE } } });
    assert.equal(await body(b, `/article${q}`), `couch-article 1\nnow=${NOW}\nstate=none|bad id\nend\n`, q);
    assert.deepEqual(b.calls, []);
  }
  assert.equal((await handle(new Request(`http://couch.test/article?id=${first.id}`, { method: 'POST' }), env, deps())).status, 404);
  assert.equal((await handle(req('/news/'), env, deps())).status, 404);
});

test('/article: the size caps (12 KB of paragraphs, whole ones, each 1500 code points), UTF-8 whole', async () => {
  const big = Array.from({ length: 40 }, (_, i) => `${i} ${'€€€€€ '.repeat(400)}`);
  const d = deps({ answers: { ...feedRoutes, [pathOf(first.link)]: { body: 'x' } }, extract: async () => ({ paras: big }) });
  const t = await body(d, `/article?id=${first.id}`);
  const ps = t.split('\n').filter((l) => l.startsWith('p='));
  assert.ok(t.includes('\nstate=cut\n') && ps.length >= 2 && utf8ok(t) && bytesOf(t) <= ARTICLE_BYTES);
  assert.ok(ps.reduce((n, l) => n + bytesOf(l) + 1, 0) <= TEXT_BYTES);
  assert.ok(ps.every((l, i) => l.startsWith(`p=${i} `) && l.endsWith('...') && Array.from(l.slice(2)).length <= PARA_MAX));
  assert.deepEqual(capParas(['a|b', '', '  ', 'c\nd']), { paras: ['a b', 'c d'], cut: false });
  const fit = capParas(['x'.repeat(1000), 'y'.repeat(PARA_MAX + 50)]);
  assert.deepEqual([fit.paras.length, Array.from(fit.paras[1]).length <= PARA_MAX, fit.cut], [2, true, false]);
});

test('articleText: the lines in order; a long link goes; the date in TZ', () => {
  const item = { id: 'abcdef01', title: 'T...', full: 'T | whole', at: 1792000000, link: 'https://x.example/a|b', summary: 'S' };
  assert.equal(articleText({ now: 1792000600, tz: 'UTC', item, feed: { label: 'L', category: '' }, a: { state: 'cut', paras: ['one', 'two'] } }),
    'couch-article 1\nnow=1792000600\nid=abcdef01\nsource=L\ncategory=-\ntitle=T whole\ndate=Wed 14 Oct 17:46|600\nlink=https://x.example/a%7Cb\nstate=cut\np=one\np=two\nend\n');
  const long = articleText({ now: 1, item: { ...item, at: null, link: `https://x.example/${'a'.repeat(LINK_BYTES)}` }, feed: null, a: { state: 'summary', reason: 'x: HTTP 403', paras: [] } });
  assert.equal(long, 'couch-article 1\nnow=1\nid=abcdef01\nsource=-\ncategory=-\ntitle=T whole\ndate=-|-\nstate=summary|x: HTTP 403\nend\n');
  assert.equal(dateWords(NOW, 'Europe/Berlin'), 'Fri 16 Oct 17:20');
});

test('get: a feed\'s cache entry holds the items /news and /article need, under news2', async () => {
  const d = deps({ answers: feedRoutes });
  const f = parseFeeds(env.NEWS_FEEDS)[0];
  const r = await get(newsUpstream(f), env, NOW, d);
  await Promise.all(d.waits);
  assert.deepEqual([r.state, r.data.length, Object.keys(r.data[0]).sort()], ['ok', 13, ['at', 'full', 'id', 'link', 'summary', 'title']]);
  assert.deepEqual([...d.cache.m.keys()], [`https://couch.internal/news2?u=${encodeURIComponent(f.url)}`]);
});
