// node --test: the Board's reply against public answers fetched on 2026-10-16 (test/fixtures):
// Open-Meteo's forecast for Berlin at 17:15 local (meteo_berlin.json), Frankfurter's series of
// USD, GBP, CHF and JPY against EUR from 2026-09-04 to 2026-10-16 (fx_eur.json), Nager.Date's
// public holidays of Germany for 2026 and 2027 (nager_de_*.json), and three made-up RSS feeds
// (feed_*.xml); and the Worker's cache and failure paths with a fake fetch and a fake Cache API.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  boardText, wmoWords, clean, localWhen, tzOffset, MAX_BYTES, fold, rainWords, sunTimes, moonPhase, dayWords, newsLines,
  NEWS_FRESH, geoLine, fxLine, hoursLine, change, addDays, localDate, shortDate, holidayLine, parseRates, fxLines,
  numfmtLine, rateDec, currencySymbol, SPARK_DAYS,
} from '../src/board.js';
import { parseFeed, parseFeeds } from '../src/news.js';
import { get, handle, newsUpstream, RETRY_S, FX_DAYS } from '../src/index.js';

const text = (f) => readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8');
const fixture = (f) => JSON.parse(text(f));
const wxData = fixture('meteo_berlin.json');
const fxRaw = fixture('fx_eur.json');
const hol26 = fixture('nager_de_2026.json'), hol27 = fixture('nager_de_2027.json');
const feedXml = { world: text('feed_world.xml'), business: text('feed_business.xml'), tech: text('feed_tech.xml') };
// wrangler.jsonc's vars: its comment lines (each starts with //) removed, then JSON.
const wranglerVars = JSON.parse(text('../../wrangler.jsonc').split('\n').filter((l) => !l.trimStart().startsWith('//')).join('\n')).vars;
const env = { ...wranglerVars };
const NOW = 1792164000; // 2026-10-16 15:20 UTC, 17:20 in Berlin (a Friday): the time of the fixtures
const okR = (data, age = 0) => ({ state: 'ok', age, data });
const NONE = { state: 'none', reason: 'x', age: 0, data: null };

// Frankfurter's answer as index.js's readFx caches it: EUR per one unit of each code.
const fxData = (() => {
  const dates = Object.keys(fxRaw.rates).sort(), codes = Object.keys(fxRaw.rates[dates[0]]);
  return { dates, rates: Object.fromEntries(codes.map((c) => [c, dates.map((d) => 1 / fxRaw.rates[d][c])])) };
})();
const holData = [...hol26, ...hol27].filter((x) => x.global && x.types.includes('Public')).map((x) => ({ fecha: x.date, nombre: x.localName }));
const news = () => [
  { label: 'BBC', weight: 4, r: okR(parseFeed(feedXml.world), 120) },
  { label: 'BBC', weight: 2, r: okR(parseFeed(feedXml.business), 120) },
  { label: 'BBC', weight: 2, r: okR(parseFeed(feedXml.tech), 120) },
];

// The first test's reply is the host check's (tools/hostcheck/run.zsh takes this array, line for
// line) and the preview's body_ok: it keeps its text as UTF-8, with no escapes. REPLY_BYTES is its
// size.
const REPLY_BYTES = 1467;
test('the reply of 2026-10-16 in Berlin, every upstream, line for line', () => {
  const t = boardText({ now: NOW, env, fx: okR(fxData, 600), wx: okR(wxData, 300), news: news(), hol: okR(holData, 3600) });
  assert.equal(t, [
    'couch-board 1',
    'now=1792164000',
    'offset=7200',
    'city=Berlin',
    'numfmt=.,',
    'geo=52.52|13.4',
    'rate=USD|-|0,8887|16 Oct',
    'rate=GBP|-|1,1850|16 Oct',
    'rate=CHF|-|1,0785|16 Oct',
    'rate=1000 JPY|-|5,6237|16 Oct',
    'feature=USD|$',
    'rate_chg=USD|down|-0,4%|-0,0035|15 Oct',
    'rate_chg=GBP|up|+0,3%|+0,0031|15 Oct',
    'rate_chg=CHF|up|+0,4%|+0,0038|15 Oct',
    'rate_chg=1000 JPY|down|-0,4%|-0,0241|15 Oct',
    'spark=USD|2026-09-17|8710 8726 - - 8703 8724 8763 8797 8770 - - 8789 8807 8807 8851 8909 - - 8925 8874 8947 8940 8924 - - 8944 8938 8933 8922 8887|4',
    'rate_age=600',
    'rate_state=ok',
    'wx_now=19°|18°|57%|Partly cloudy|8 km/h|day|Open-Meteo',
    'wx_fx=2|70|0|172|8|23',
    'day=Today|Fog|19°|7°|0%',
    'day=Sat|Rain|12°|10°|55%',
    'day=Sun|Overcast|13°|9°|5%',
    'days_fx=45|61|3',
    'wx_rain=dry|No rain in the next 12 h',
    'wx_sun=07:33|18:09',
    'wx_moon=Waxing crescent moon',
    'wx_hours=18|18 17 15 14 14 13 12 12 11 11 11 11|0 0 0 0 0 0 0 0 0 0 0 0|2 3 3 3 2 0 0 1 2 1 3 3',
    'wx_age=300',
    'wx_state=ok',
    'holiday=2026-12-25|Erster Weihnachtstag|70',
    'news=BBC|600|Leaders meet to agree on a new plan for river shipping',
    'news=BBC|600|Central bank keeps its rate unchanged',
    'news=BBC|600|A phone maker shows a screen that folds three times',
    'news=BBC|3000|Floods ease in the north as the rain moves east',
    'news=BBC|3700|Shares rise as chip makers report strong sales',
    'news=BBC|5400|A museum reopens after five years of repairs',
    'news=BBC|5600|New rules for app stores take effect',
    'news=BBC|7800|Talks on fishing rights resume after a short break',
    'news_age=120',
    'news_state=ok',
    'end',
    '',
  ].join('\n'));
  assert.equal(new TextEncoder().encode(t).length, REPLY_BYTES);  // the 4 KB guard drops nothing
});

test('stale and none: the state lines, the old data with its age, no data lines without data', () => {
  const t = boardText({ now: NOW, env, fx: { state: 'stale', reason: 'frankfurter.dev: HTTP 503', age: 90000, data: fxData },
    wx: { state: 'none', reason: 'open-meteo: no answer in 4 s', age: 0, data: null } });
  const lines = t.trimEnd().split('\n');
  assert.ok(lines.includes('rate_state=stale|frankfurter.dev: HTTP 503') && lines.includes('rate_age=90000'));
  assert.equal(lines.filter((l) => l.startsWith('rate=')).length, 4);
  assert.ok(lines.includes('wx_state=none|open-meteo: no answer in 4 s'));
  assert.equal(lines.filter((l) => l.startsWith('wx_now=') || l.startsWith('day=')).length, 0);
  assert.ok(lines.includes('offset=7200') && lines.includes('wx_moon=Waxing crescent moon'));   // from the time zone and the date, with no forecast
  assert.equal(lines.at(-1), 'end');
  // no RATES: no rate lines, no state line; no COUNTRY: no holiday
  const bare = boardText({ now: NOW, env: { ...env, RATES: '' }, wx: NONE });
  assert.ok(!bare.includes('\nrate') && !bare.includes('\nholiday='));
});

test('the helpers', () => {
  assert.equal(wmoWords(0), 'Clear');
  assert.equal(wmoWords(82), 'Rain showers');
  assert.equal(wmoWords(4), 'Unknown');
  assert.equal(clean('a|b\nc\u0000d  e'), 'a b c d e');
  assert.equal(fold('München'), 'munchen');
  assert.equal(localWhen(NOW - 3600, NOW, env.TZ), '16:20');
  assert.equal(localWhen(NOW - 20 * 3600, NOW, env.TZ), 'Thu 21:20');
  assert.equal(tzOffset(NOW, env.TZ), 7200);
  assert.equal(tzOffset(NOW, 'UTC'), 0);
  assert.equal(geoLine(env), 'geo=52.52|13.4');
  assert.equal(geoLine({ LAT: '91', LON: '0' }), null);
  assert.deepEqual([addDays('2026-10-16', -32), localDate(NOW, env.TZ), shortDate('2026-10-15'), dayWords('2026-10-16', NOW, env.TZ)],
    ['2026-09-14', '2026-10-16', '15 Oct', 'Today']);
});

test('the rates: RATES, the decimals, the symbol, the number format of LOCALE', () => {
  assert.deepEqual(parseRates('usd, GBP:Pound,,XX,JPY:Yen|x'), [
    { code: 'USD', label: 'USD' }, { code: 'GBP', label: 'Pound' }, { code: 'JPY', label: 'Yen x' }]);
  assert.equal(parseRates('A1C,' + 'USD,'.repeat(10)).length, 8);
  assert.deepEqual([rateDec(0.0055), rateDec(0.86), rateDec(12.5), rateDec(1450)], [4, 4, 3, 2]);
  assert.deepEqual([currencySymbol('USD'), currencySymbol('EUR'), currencySymbol('XYZ')], ['$', '€', '']);
  assert.deepEqual(['de-DE', 'en-US', 'es-AR', 'de-CH', 'fr-FR', 'ja-JP'].map(numfmtLine),
    ['numfmt=.,', 'numfmt=,.', 'numfmt=.,', "numfmt='.", 'numfmt=_,', 'numfmt=,.']);
});

test('fxLines: a rate per code, the feature, the change against the day before, the 30 days\' series', () => {
  const l = fxLines({ fx: okR(fxData, 600), env: { RATES: 'USD:Dollar,GBP,CHF,JPY,BRL', FEATURE: 'gbp' }, locale: 'en-US' });
  const last = fxData.dates.length - 1, usd = fxData.rates.USD;
  assert.equal(l[0], `rate=Dollar|-|${usd[last].toFixed(4)}|16 Oct`);
  assert.equal(l.filter((x) => x.startsWith('rate=')).length, 4);                     // BRL is not in the answer
  assert.ok(l.includes('feature=GBP|£'));
  const chg = l.filter((x) => x.startsWith('rate_chg='));
  assert.equal(chg.length, 4);
  assert.ok(chg.every((x) => /^rate_chg=[^|]+\|(up|down|flat)\|[+-]?\d+\.\d+%\|[+-]?[\d.,]+\|15 Oct$/.test(x)), chg.join('\n'));
  const sp = l.find((x) => x.startsWith('spark=')).split('|');
  assert.equal(sp[0], 'spark=GBP');
  assert.equal(sp[1], addDays('2026-10-16', 1 - SPARK_DAYS));
  const v = sp[2].split(' ');
  assert.equal(v.length, SPARK_DAYS);
  assert.equal(sp[3], '4');
  assert.equal(v.at(-1), String(Math.round(fxData.rates.GBP[last] * 1e4)));
  assert.equal(v.filter((x) => x === '-').length, 8);                                   // the four weekends of the 30 days
  assert.deepEqual(l.slice(-2), ['rate_age=600', 'rate_state=ok']);
  // no FEATURE: the first rate; a feature with no symbol known: an empty symbol
  assert.ok(fxLines({ fx: okR(fxData), env: { RATES: 'JPY,USD' }, locale: 'en-US' }).includes('feature=1000 JPY|¥'));
  assert.ok(fxLines({ fx: okR(fxData), env: { RATES: 'CHF' }, locale: 'en-US' }).includes('feature=CHF|Fr'));
  // a change: up, down, flat at the shown decimals, the sign in the locale
  assert.deepEqual(change(1.2346, 1.2345, 4, 'de-DE'), { dir: 'up', pct: '+0,01%', abs: '+0,0001' });
  assert.deepEqual(change(155, 160, 2, 'en-US'), { dir: 'down', pct: '-3.1%', abs: '-5.00' });
  assert.equal(change(1, null, 2, 'en-US'), null);
});

test('the holidays: the next one on or after today, its days; none after the list ends', () => {
  assert.equal(holidayLine(okR(holData), NOW, env.TZ), 'holiday=2026-12-25|Erster Weihnachtstag|70');
  assert.equal(holidayLine(okR([{ fecha: '2026-10-16', nombre: 'Heute | test' }]), NOW, env.TZ), 'holiday=2026-10-16|Heute test|0');
  assert.equal(holidayLine(okR([{ fecha: '2026-10-15', nombre: 'Gestern' }]), NOW, env.TZ), null);
  assert.equal(holidayLine(NONE, NOW, env.TZ), null);
});

test('the sky: the rain of the next 12 h, the sun of today, the moon, the next hours, wx_fx', () => {
  const p = localDate(NOW, env.TZ);
  assert.deepEqual(sunTimes(wxData.daily, p), { rise: wxData.daily.sunrise[0].slice(11), set: wxData.daily.sunset[0].slice(11) });
  assert.equal(moonPhase(NOW), 'Waxing crescent moon');
  const r = rainWords(wxData.hourly, NOW, env.TZ);
  assert.ok(r && typeof r.wet === 'boolean' && r.words.length > 0);
  const h = hoursLine(wxData.hourly, NOW, env.TZ).split('|');
  assert.equal(h[0], 'wx_hours=18');                                                   // the next whole hour, local
  assert.equal(h[1].split(' ').length, 12);
  assert.match(fxLine(wxData.current), /^wx_fx=\d+\|\d+\|[\d.]+\|\d+\|\d+\|\d+$/);
  assert.equal(fxLine(null), null);
});

test('the offset is the time zone\'s at now, not the cached forecast\'s (a DST change)', () => {
  const now = Date.UTC(2026, 9, 25, 1, 5) / 1000; // Berlin left summer time at 01:00 UTC
  const t = boardText({ now, env, wx: okR({ ...wxData, utc_offset_seconds: 7200 }, 600) });
  assert.ok(t.split('\n').includes('offset=3600'));
});

test('the 4 KB guard: a long city is cut; long lines drop from the end, the news first', () => {
  const t = boardText({ now: NOW, env: { ...env, CITY: 'x'.repeat(4100) }, fx: okR(fxData), wx: okR(wxData) });
  assert.ok(new TextEncoder().encode(t).length <= MAX_BYTES && t.includes(`\ncity=${'x'.repeat(100)}\n`) && t.endsWith('\nend\n'));
  const long = news().map((f) => ({ ...f, r: okR(f.r.data.map((x) => ({ ...x, title: '€'.repeat(110) }))) }));
  const e = { ...env, RATES: 'USD:' + 'L'.repeat(300) + ',GBP:' + 'M'.repeat(300) + ',CHF:' + 'N'.repeat(300) + ',JPY:' + 'O'.repeat(300) };
  const g = boardText({ now: NOW, env: e, fx: okR(fxData), wx: okR(wxData), news: long, hol: okR(holData) });
  const lines = g.trimEnd().split('\n');
  assert.ok(new TextEncoder().encode(g).length <= MAX_BYTES);
  assert.equal(lines.filter((l) => l.startsWith('news=')).length, 0);
  assert.ok(lines.includes('rate_state=ok') && lines.includes('wx_state=ok') && lines.includes('news_state=ok') && lines.at(-1) === 'end');
});

test('news: NEWS_FEEDS, the share per feed by weight, the newest first, old ones only fill', () => {
  const l = newsLines(news(), NOW, 8).filter((x) => x.startsWith('news='));
  assert.equal(l.length, 8);
  const ages = l.map((x) => +x.split('|')[1]);
  assert.ok(ages.every((a, i) => i === 0 || ages[i - 1] <= a));
  const T = 100000, W = (p, n) => Array.from({ length: n }, (_, i) => ({ title: `${p}${i}`, at: T - 60 * i }));
  const stale = [{ title: 'S0', at: T - NEWS_FRESH - 1 }, { title: 'S1', at: null }];
  assert.deepEqual(newsLines([{ label: 'A', r: okR(W('A', 10)) }, { label: 'S', weight: 3, r: okR(stale) }], T, 4)
    .filter((x) => x.startsWith('news=')).map((x) => x.split('|')[2]), ['A0', 'A1', 'A2', 'A3']);
  assert.deepEqual(newsLines([{ label: 'A', r: okR(W('A', 2)) }, { label: 'S', weight: 3, r: okR(stale) }], T, 8)
    .filter((x) => x.startsWith('news=')).map((x) => x.split('|')[2]), ['A0', 'A1', 'S0', 'S1']);
  assert.deepEqual(newsLines([{ label: 'A', r: { state: 'none', reason: 'a.example: no items', age: 0, data: null } }], T, 8),
    ['news_age=0', 'news_state=none|a.example: no items']);
  assert.deepEqual(parseFeeds(env.NEWS_FEEDS).map((f) => [f.label, f.weight, f.category]), [['BBC', 4, 'world'], ['BBC', 2, 'business'], ['BBC', 2, 'tech']]);
});

// ---- the Worker with fakes --------------------------------------------------------

// ops counts the calls: the Cache API shares the free plan's 50 subrequests with fetch().
function fakeCache() {
  const m = new Map();
  return {
    m, ops: 0,
    async match(k) { this.ops++; return m.has(k) ? new Response(m.get(k)) : undefined; },
    async put(k, r) { this.ops++; m.set(k, await r.text()); },
  };
}

function deps({ cache = fakeCache(), answers = {}, now = NOW } = {}) {
  const calls = [];
  return {
    calls, cache, waits: [],
    now: () => now,
    waitUntil(p) { this.waits.push(p); },
    async fetch(url) {
      calls.push(url);
      const u = new URL(url);
      const a = answers[u.hostname + u.pathname] ?? answers[u.hostname];
      if (a instanceof Error) throw a;
      if (!a) throw new TypeError('no route');
      if (a.response) return a.response;
      const body = typeof a.body === 'string' ? a.body : JSON.stringify(a.body);
      return new Response(body, { status: a.status || 200 });
    },
  };
}

const ok = {
  'api.open-meteo.com': { body: wxData },
  'api.frankfurter.dev': { body: fxRaw },
  'date.nager.at/api/v3/PublicHolidays/2026/DE': { body: hol26 },
  'date.nager.at/api/v3/PublicHolidays/2027/DE': { body: hol27 },
  'feeds.bbci.co.uk/news/world/rss.xml': { body: feedXml.world },
  'feeds.bbci.co.uk/news/business/rss.xml': { body: feedXml.business },
  'feeds.bbci.co.uk/news/technology/rss.xml': { body: feedXml.tech },
};

test('get fx: the series inverted to CURRENCY per unit, from FX_DAYS days back, cached under the day', async () => {
  const d = deps({ answers: ok });
  const r = await get('fx', env, NOW, d);
  assert.equal(r.state, 'ok');
  assert.deepEqual(r.data.dates, fxData.dates);
  assert.ok(Math.abs(r.data.rates.USD.at(-1) - fxData.rates.USD.at(-1)) < 1e-12);
  const u = new URL(d.calls[0]);
  assert.equal(u.pathname, `/v1/${addDays('2026-10-16', -FX_DAYS)}..`);
  assert.deepEqual([u.searchParams.get('base'), u.searchParams.get('symbols')], ['EUR', 'USD,GBP,CHF,JPY']);
  await Promise.all(d.waits);
  assert.deepEqual([...d.cache.m.keys()], ['https://couch.internal/fx?d=2026-10-16&b=EUR&s=USD,GBP,CHF,JPY']);
  const bad = await get('fx', env, NOW, deps({ answers: { 'api.frankfurter.dev': { body: { rates: 'x' } } } }));
  assert.deepEqual([bad.state, bad.reason], ['none', 'frankfurter.dev: bad JSON']);
});

test('get hol: the nationwide public holidays, local names, sorted; the next year when none is left', async () => {
  const d = deps({ answers: ok });
  const r = await get('hol', env, NOW, d);
  assert.equal(r.state, 'ok');
  assert.deepEqual(r.data.filter((x) => x.fecha >= '2026-10-16').map((x) => x.nombre), ['Erster Weihnachtstag', 'Zweiter Weihnachtstag']);
  assert.ok(!r.data.some((x) => x.nombre === 'Heilige Drei Könige'));                   // not nationwide
  assert.equal(d.calls.length, 1);
  const dec = Date.UTC(2026, 11, 28, 12) / 1000;
  const n = deps({ answers: ok, now: dec });
  const r2 = await get('hol', env, dec, n);
  assert.equal(n.calls.length, 2);
  assert.equal(holidayLine(r2, dec, env.TZ), 'holiday=2027-01-01|Neujahr|4');
  const en = await get('hol', { ...env, HOLIDAY_NAMES: 'english' }, NOW, deps({ answers: ok }));
  assert.ok(en.data.some((x) => x.nombre === 'Christmas Day'));
});

test('get: an old entry and a failed upstream give stale with the real age; no entry gives none', async () => {
  const cache = fakeCache();
  const d1 = deps({ cache, answers: ok });
  await get('wx', env, NOW, d1);
  await Promise.all(d1.waits);
  const d2 = deps({ cache, answers: { 'api.open-meteo.com': { status: 503, body: 'down' } } });
  const s = await get('wx', env, NOW + 2000, d2);
  assert.deepEqual([s.state, s.reason, s.age], ['stale', 'open-meteo: HTTP 503', 2000]);
  const timeout = Object.assign(new Error('timed out'), { name: 'TimeoutError' });
  const n = await get('fx', env, NOW, deps({ answers: { 'api.frankfurter.dev': timeout } }));
  assert.deepEqual([n.state, n.reason, n.data], ['none', 'frankfurter.dev: no answer in 4 s', null]);
  const broke = { ok: true, status: 200, json: async () => { throw new TypeError('terminated'); } };
  const b = await get('fx', env, NOW, deps({ answers: { 'api.frankfurter.dev': { response: broke } } }));
  assert.deepEqual([b.state, b.reason], ['none', 'frankfurter.dev: terminated']);
});

test('get: the negative cache: a failed upstream is not waited for, and is tried again after the reply each RETRY_S', async () => {
  const cache = fakeCache();
  const down = { 'api.frankfurter.dev': { status: 503, body: 'x' } };
  const d = deps({ cache, answers: down });
  assert.equal((await get('fx', env, NOW, d)).reason, 'frankfurter.dev: HTTP 503');
  await Promise.all(d.waits);
  const again = deps({ cache, answers: ok, now: NOW + 60 });
  assert.equal((await get('fx', env, NOW + 60, again)).reason, 'frankfurter.dev: HTTP 503');
  assert.deepEqual(again.calls, []);
  const later = deps({ cache, answers: ok, now: NOW + RETRY_S + 1 });
  assert.equal((await get('fx', env, NOW + RETRY_S + 1, later)).state, 'none');           // not waited for
  await Promise.all(later.waits);
  assert.equal(later.calls.length, 1);
  assert.equal((await get('fx', env, NOW + RETRY_S + 2, deps({ cache, answers: {} }))).state, 'ok');
});

test('handle: /board on a cold cache: every upstream once; the reply as the first test\'s; 404 elsewhere', async () => {
  const d = deps({ answers: ok });
  const r = await handle(new Request('http://couch.test/board'), env, d);
  assert.equal(r.status, 200);
  assert.equal(r.headers.get('content-type'), 'text/plain; charset=utf-8');
  assert.equal(r.headers.get('cache-control'), 'no-store');
  const body = await r.text();
  assert.ok(body.startsWith('couch-board 1\nnow=1792164000\noffset=7200\ncity=Berlin\nnumfmt=.,\ngeo=52.52|13.4\nrate=USD|'));
  assert.ok(body.includes('\nholiday=2026-12-25|Erster Weihnachtstag|70\n') && body.endsWith('\nnews_state=ok\nend\n'));
  // 6 fetches (the forecast, the rates, this year's holidays, 3 feeds), 6 cache reads and 6 writes
  await Promise.all(d.waits);
  assert.deepEqual([d.calls.length, d.cache.ops], [6, 12]);
  assert.equal((await handle(new Request('http://couch.test/'), env, d)).status, 404);
  assert.equal((await handle(new Request('http://couch.test/board', { method: 'POST' }), env, d)).status, 404);
  // a bad TZ: still 200, the states none with the error
  const bad = await (await handle(new Request('http://couch.test/board'), { ...env, TZ: 'Not/A_Zone' }, deps({ answers: ok }))).text();
  assert.match(bad, /\nrate_state=none\|worker: [^\n|]+\nwx_state=none\|worker: [^\n|]+\n/);
  // the feeds share the cache with /news: a dead feed leaves the others
  const one = deps({ answers: { ...ok, 'feeds.bbci.co.uk/news/business/rss.xml': { status: 500, body: '' } } });
  const t = await (await handle(new Request('http://couch.test/board'), env, one)).text();
  assert.ok(t.includes('\nnews_state=stale|feeds.bbci.co.uk: HTTP 500\n'));
  assert.ok(newsUpstream(parseFeeds(env.NEWS_FEEDS)[0]).key().startsWith('https://couch.internal/news2?u='));
});
