// node --test: the Board's reply against public answers fetched on 2026-10-16 (test/fixtures):
// Open-Meteo's forecast for Berlin at 17:15 local (meteo_berlin.json), Frankfurter's series of
// USD, GBP, CHF and JPY against EUR from 2026-09-04 to 2026-10-16 (fx_eur.json), Nager.Date's
// public holidays of Germany for 2026 and 2027 (nager_de_*.json), and three made-up RSS feeds
// (feed_*.xml); and the Worker's cache and failure paths with a fake fetch and a fake Cache API.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  boardText, wmoWords, clean, localWhen, tzOffset, MAX_BYTES, fold, rainWords, sunTimes, dayWords, newsLines,
  NEWS_FRESH, geoLine, fxLine, hoursLine, change, addDays, localDate, shortDate, holidayLine, parseRates, fxLines,
  numfmtLine, rateDec, currencySymbol, SPARK_DAYS, soonWords, airLine, AIR_MAX_AGE,
} from '../src/board.js';
import { feedItems, ITEMS_MAX, parseFeeds } from '../src/news.js';
import { get, handle, newsUpstream, RETRY_S, FX_DAYS, FUTBOL_NONE, paced, FETCH_SLOTS } from '../src/index.js';
import { futbolLine, futbolText, FUTBOL_BYTES, FUT_MAX_AGE, dayName, tvCut } from '../src/futbol.js';

// A feed's 8 newest items as the Board takes them (feedItems).
const feed8 = (xml) => feedItems(xml, ITEMS_MAX);
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
  { label: 'BBC', weight: 4, r: okR(feed8(feedXml.world), 120) },
  { label: 'BBC', weight: 2, r: okR(feed8(feedXml.business), 120) },
  { label: 'BBC', weight: 2, r: okR(feed8(feedXml.tech), 120) },
];

// Made-up answers for the keys of Couch 0.5.0: Open-Meteo's air quality in its shape, and a day of
// fictional fixtures in futbol.js's form (the template has no fútbol source: only the tests feed it).
const airData = { current: { time: '2026-10-16T17:00', interval: 3600, us_aqi: 38, uv_index: 1.4 } };
const at = (hm) => Date.parse(`2026-10-16T${hm}:00+02:00`) / 1000;   // a wall time in Berlin
const game = (start, kind, status, home, away, homeShort, awayShort, sh, sa, tv) => ({ start: at(start), kind, status, home, away, homeShort, awayShort, sh, sa, tv });
const futDay = () => ({
  day: '2026-10-16',
  leagues: [
    { name: 'Spree League', country: 'Germany', games: [
      game('15:30', 'end', 'Final', 'FC Kiezblau', 'Union Mitte', 'Kiezblau', 'Mitte', 2, 1, 'Spree TV'),
      game('18:30', 'pre', '', 'Havel Rovers', 'SV Tegelsee', 'Rovers', 'Tegelsee', null, null, 'Spree TV, Kanal 9'),
      game('20:30', 'pre', '', 'Borussia Wedding', 'Dynamo Pankow', 'Wedding', 'Pankow', null, null, ''),
    ] },
    { name: 'Cup of the North', country: 'Europe', games: [
      game('16:00', 'end', 'Pens 4-3', 'Nordhafen', 'Ostkreuz United', 'Nordhafen', 'Ostkreuz', 1, 1, 'Kanal 9'),
      game('17:00', 'off', 'Postponed', 'Gr\u00fcnau 04', 'M\u00fcggel SC', 'Gr\u00fcnau', 'M\u00fcggel', null, null, ''),
    ] },
  ],
});

// The first test's reply is the host check's (tools/hostcheck/run.zsh takes this array, line for
// line) and the preview's body_ok: it keeps its text as UTF-8, with no escapes. REPLY_BYTES is its
// size.
const REPLY_BYTES = 1475;
test('the reply of 2026-10-16 in Berlin, every upstream, line for line', () => {
  const t = boardText({
    now: NOW, env, fx: okR(fxData, 600), wx: okR(wxData, 300), news: news(), hol: okR(holData, 3600), air: okR(airData, 600), fut: okR(futDay(), 120),
  });
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
    'wx_now=19°|18°|57%|Partly cloudy|8 km/h|day',
    'wx_fx=2|70|0|172|8|23',
    'day=Today|Fog|19°|7°|0%',
    'day=Sat|Rain|12°|10°|55%',
    'day=Sun|Overcast|13°|9°|5%',
    'days_fx=45|61|3',
    'wx_rain=dry|No rain in the next 12 h',
    'wx_sun=07:33|18:09',
    'wx_hours=18|18 17 15 14 14 13 12 12 11 11 11 11|0 0 0 0 0 0 0 0 0 0 0 0|2 3 3 3 2 0 0 1 2 1 3 3',
    'wx_age=300',
    'wx_state=ok',
    'air=38|1',
    'holiday=2026-12-25|Erster Weihnachtstag|70',
    'futbol=0|Next: Rovers–Tegelsee 18:30',
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
  assert.ok(lines.includes('offset=7200') && !lines.some((l) => l.startsWith('wx_moon=')));   // from the time zone; no moon (Couch works it out)
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

test('the sky: the rain of the next 12 h, the sun of today, the next hours, wx_fx', () => {
  const p = localDate(NOW, env.TZ);
  assert.deepEqual(sunTimes(wxData.daily, p), { rise: wxData.daily.sunrise[0].slice(11), set: wxData.daily.sunset[0].slice(11) });
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
  // an item that two feeds carry (the same id: the same link) counts once
  const it = (id, title, t) => ({ id, title, at: t });
  const a = [it('00000001', 'One', T - 10), it('00000002', 'Two', T - 20)], b = [it('00000001', 'One', T - 10), it('00000003', 'Three', T - 30)];
  assert.deepEqual(newsLines([{ label: 'A', r: okR(a) }, { label: 'B', r: okR(b) }], T, 8).filter((x) => x.startsWith('news=')),
    ['news=A|10|One', 'news=A|20|Two', 'news=B|30|Three']);
});

test('tvCut keeps whole channel names and drops their notes', () => {
  assert.equal(tvCut('Spree TV, Kanal 9 (Fu\u00dfball-Paket)', 40), 'Spree TV, Kanal 9');
  assert.equal(tvCut('Spree TV Premium, Kanal 9 Sport, Havel Stream Plus', 40), 'Spree TV Premium, Kanal 9 Sport');
  assert.equal(tvCut('A channel name that is far too long to fit', 10), 'A channel');
  assert.equal(tvCut('', 40), '-');
  const g = futDay();
  g.leagues[0].games[1].tv = 'Spree TV (Abo), Kanal 9 (Fu\u00dfball-Paket)';
  assert.ok(futbolText({ now: NOW, tz: env.TZ, d: 0, r: okR(g) }).includes('\ngame=18:30|Havel Rovers|SV Tegelsee|-|-|Spree TV, Kanal 9|pre\n'));
});

test('paced: at most FETCH_SLOTS fetches at once, in order, each timer from its own start', async () => {
  const started = [], ends = [];
  let open = 0, most = 0;
  const f = paced((url, o) => {
    started.push([url, !!o.signal, 'timeout' in o]);
    most = Math.max(most, ++open);
    return new Promise((r) => ends.push(() => { open--; r(new Response(url)); }));
  }, 2);
  const all = ['a', 'b', 'c', 'd'].map((u) => f(u, { timeout: 4000, headers: {} }));
  await new Promise((r) => setTimeout(r, 0));
  assert.deepEqual(started, [['a', true, false], ['b', true, false]]);               // c and d wait, their timers not made
  ends.shift()();
  await new Promise((r) => setTimeout(r, 0));
  assert.deepEqual(started.map((x) => x[0]), ['a', 'b', 'c']);
  while (ends.length) { ends.shift()(); await new Promise((r) => setTimeout(r, 0)); }
  assert.deepEqual(await Promise.all(all.map(async (p) => (await p).text())), ['a', 'b', 'c', 'd']);
  assert.deepEqual([most, FETCH_SLOTS], [2, 6]);
  const bad = paced(() => { throw new TypeError('no route'); });
  await assert.rejects(bad('x', {}), /no route/);                                      // a throw frees its slot too
});

// ---- the keys of Couch 0.5.0: the rain of the next 2 h, the air, the fútbol -------------

// Open-Meteo's minutely_15 from 17:15 local (the stamp of 17:00 to 17:15), 24 steps: wet at the
// stamps given (each the end of its 15 minutes). NOW is 17:20 in Berlin.
const m15 = (wet, n = 24, from = '2026-10-16T17:15') => {
  const t0 = Date.parse(`${from}:00Z`), time = [], precipitation = [];
  for (let k = 0; k < n; k++) {
    const t = new Date(t0 + k * 900000).toISOString().slice(0, 16);
    time.push(t);
    precipitation.push(wet.includes(t.slice(11)) ? 0.4 : 0);
  }
  return { time, precipitation };
};

test('wx_soon: rain from, rain until, rain for the next 2 h, none when dry or not covered; after wx_rain', () => {
  assert.equal(soonWords(m15([]), NOW, env.TZ), null);
  assert.equal(soonWords(m15(['18:45', '19:00']), NOW, env.TZ), 'Rain from 18:30');          // 18:30 to 18:45 is the first wet one
  assert.equal(soonWords(m15(['17:30', '17:45']), NOW, env.TZ), 'Rain until 17:45');         // now's 15 minutes (17:15 to 17:30) are wet
  assert.equal(soonWords(m15(m15([]).time.map((t) => t.slice(11))), NOW, env.TZ), 'Rain for the next 2 h');
  assert.equal(soonWords(m15(['19:30']), NOW, env.TZ), 'Rain from 19:15');                   // starts before 19:20: in the window
  assert.equal(soonWords(m15(['19:45']), NOW, env.TZ), null);                                // starts at 19:30, after now + 2 h
  assert.equal(soonWords({ time: m15([]).time, precipitation: [0, 0.05, 0.1] }, NOW, env.TZ), 'Rain from 17:30');   // from 0.1 mm
  assert.equal(soonWords(m15(['17:45'], 6), NOW, env.TZ), null);                              // the values end at 18:30: not covered
  assert.equal(soonWords(m15(['19:00'], 24, '2026-10-16T17:45'), NOW, env.TZ), null);         // they start after now
  assert.equal(soonWords(undefined, NOW, env.TZ), null);
  const t = boardText({ now: NOW, env, wx: okR({ ...wxData, minutely_15: m15(['18:45']) }, 300) }).split('\n');
  assert.equal(t[t.findIndex((l) => l.startsWith('wx_rain=')) + 1], 'wx_soon=Rain from 18:30');
  assert.ok(!boardText({ now: NOW, env, wx: okR(wxData, 300) }).includes('wx_soon='));
});

test('air: the US AQI and the UV index, whole; "-" for one missing; none when both are missing or the data is over 3 h old', () => {
  assert.equal(airLine(okR(airData, 600)), 'air=38|1');
  assert.equal(airLine(okR({ current: { us_aqi: 151.6, uv_index: 7.5 } })), 'air=152|8');
  assert.equal(airLine(okR({ current: { us_aqi: null, uv_index: 3 } })), 'air=-|3');
  assert.equal(airLine(okR({ current: { us_aqi: 12 } })), 'air=12|-');
  assert.equal(airLine(okR({ current: {} })), null);
  assert.equal(airLine({ state: 'stale', reason: 'x', age: AIR_MAX_AGE, data: airData }), 'air=38|1');
  assert.equal(airLine({ state: 'stale', reason: 'x', age: AIR_MAX_AGE + 1, data: airData }), null);
  assert.equal(airLine(NONE), null);
  const t = boardText({ now: NOW, env, wx: okR(wxData, 300), air: okR(airData) }).split('\n');
  assert.equal(t[t.findIndex((l) => l.startsWith('wx_state=')) + 1], 'air=38|1');
});

test('futbol: the live games, else the next one, else no more; none for no game, old data or another day', () => {
  assert.equal(futbolLine(okR(futDay(), 120), NOW, env.TZ), 'futbol=0|Next: Rovers–Tegelsee 18:30');
  const live = futDay();
  live.leagues[0].games[1] = { ...live.leagues[0].games[1], kind: 'live', status: "67'", sh: 0, sa: 1 };
  live.leagues[1].games[1] = { ...live.leagues[1].games[1], kind: 'live', status: 'HT', sh: 0, sa: 0 };
  assert.equal(futbolLine(okR(live), NOW, env.TZ), 'futbol=2|2 live');
  const over = futDay();
  over.leagues[0].games = over.leagues[0].games.slice(0, 1);
  assert.equal(futbolLine(okR(over), NOW, env.TZ), 'futbol=0|No more games today');
  const tie = futDay();                               // the same start: the first in the list
  tie.leagues[0].games[2].start = tie.leagues[0].games[1].start;
  assert.equal(futbolLine(okR(tie), NOW, env.TZ), 'futbol=0|Next: Rovers–Tegelsee 18:30');
  assert.equal(futbolLine({ state: 'stale', reason: 'x', age: FUT_MAX_AGE, data: futDay() }, NOW, env.TZ), 'futbol=0|Next: Rovers–Tegelsee 18:30');
  assert.equal(futbolLine({ state: 'stale', reason: 'x', age: FUT_MAX_AGE + 1, data: futDay() }, NOW, env.TZ), null);
  assert.equal(futbolLine(okR({ ...futDay(), day: '2026-10-15' }), NOW, env.TZ), null);
  assert.equal(futbolLine(okR({ day: '2026-10-16', leagues: [] }), NOW, env.TZ), null);
  assert.equal(futbolLine(NONE, NOW, env.TZ), null);
  assert.equal(dayName('2026-10-17'), 'Sat 17 Oct');
});

test('the /futbol reply: the day, a league line and its games, the state; line for line', () => {
  assert.equal(futbolText({ now: NOW, tz: env.TZ, d: 0, r: okR(futDay(), 120) }), [
    'couch-futbol 1',
    'now=1792164000',
    'day=Today, Fri 16 Oct',
    'league=Spree League|Germany',
    'game=Final|FC Kiezblau|Union Mitte|2|1|Spree TV|end',
    'game=18:30|Havel Rovers|SV Tegelsee|-|-|Spree TV, Kanal 9|pre',
    'game=20:30|Borussia Wedding|Dynamo Pankow|-|-|-|pre',
    'league=Cup of the North|Europe',
    'game=Pens 4-3|Nordhafen|Ostkreuz United|1|1|Kanal 9|end',
    'game=Postponed|Grünau 04|Müggel SC|-|-|-|off',
    'futbol_age=120',
    'futbol_state=ok',
    'end',
    '',
  ].join('\n'));
  const live = futDay();
  live.leagues[0].games[1] = { ...live.leagues[0].games[1], kind: 'live', status: "45+2'", sh: 0, sa: 1, home: 'A|B\u0000C' };
  assert.ok(futbolText({ now: NOW, tz: env.TZ, d: 0, r: okR(live) }).includes("\ngame=45+2'|A B C|SV Tegelsee|0|1|Spree TV, Kanal 9|live\n"));
  // tomorrow, an old entry, none
  const tomorrow = { ...futDay(), day: '2026-10-17' };
  assert.ok(futbolText({ now: NOW, tz: env.TZ, d: 1, r: { state: 'stale', reason: 'x.example: HTTP 503', age: 900, data: tomorrow } })
    .startsWith('couch-futbol 1\nnow=1792164000\nday=Tomorrow, Sat 17 Oct\nleague=Spree League|Germany\n'));
  assert.ok(futbolText({ now: NOW, tz: env.TZ, d: 1, r: { state: 'stale', reason: 'x.example: HTTP 503', age: 900, data: tomorrow } })
    .endsWith('\nfutbol_age=900\nfutbol_state=stale|x.example: HTTP 503\nend\n'));
  assert.equal(futbolText({ now: NOW, tz: env.TZ, d: 0, r: { state: 'none', reason: 'a | b', age: 0, data: null } }),
    'couch-futbol 1\nnow=1792164000\nday=Today, Fri 16 Oct\nfutbol_age=0\nfutbol_state=none|a b\nend\n');
});

test('the /futbol reply\'s cap: whole leagues from the start, the rest counted in more=, UTF-8 whole', () => {
  const long = (k) => `${'ü'.repeat(20)}€${k}${'é'.repeat(40)}`;   // cut to 28 code points
  const big = { day: '2026-10-16', leagues: Array.from({ length: 40 }, (_, i) => ({
    name: `League ${i} ${'ä'.repeat(60)}`, country: 'Österreich'.repeat(5),
    games: Array.from({ length: 6 }, (_, j) => game('19:00', 'end', 'Final', long(j), long(j + 1), 'a', 'b', 3, 2, 'ñ'.repeat(50))) })) };
  const t = futbolText({ now: NOW, tz: env.TZ, d: 0, r: okR(big) });
  const lines = t.trimEnd().split('\n'), n = lines.filter((l) => l.startsWith('league=')).length;
  assert.ok(new TextEncoder().encode(t).length <= FUTBOL_BYTES);
  assert.ok(n > 5 && n < 40);
  assert.equal(lines.filter((l) => l.startsWith('game=')).length, n * 6);              // whole leagues
  assert.equal(lines.at(-4), `more=${40 - n}|${(40 - n) * 6}`);
  assert.deepEqual(lines.slice(-3), ['futbol_age=0', 'futbol_state=ok', 'end']);
  const g0 = lines.find((l) => l.startsWith('game=')).split('|');
  assert.deepEqual([Array.from(g0[1]).length, Array.from(g0[5]).length, Array.from(lines[3].split('|')[0].slice(7)).length], [28, 40, 48]);
  assert.ok(!t.includes('�'));
  // a day that fits: no more= line
  assert.ok(!futbolText({ now: NOW, tz: env.TZ, d: 0, r: okR(futDay()) }).includes('\nmore='));
});

test('the 4 KB guard with the keys of 0.5.0: the news, then the air, then the fútbol, the holiday and the series, the hours, ...', () => {
  const groups = [['news='], ['air='], ['futbol=', 'holiday=', 'spark='], ['wx_hours='], ['rate_chg='], ['wx_rain=', 'wx_soon=', 'wx_sun=', 'wx_fx=', 'days_fx=']];
  const seen = new Set();
  for (let k = 0; k <= 520; k += 4) {
    const e = { ...env, RATES: ['USD', 'GBP', 'CHF', 'JPY'].map((c, i) => `${c}:${'LMNO'[i].repeat(k)}`).join(',') };
    const t = boardText({ now: NOW, env: e, fx: okR(fxData), wx: okR({ ...wxData, minutely_15: m15(['18:45']) }), news: news(), hol: okR(holData),
      air: okR(airData), fut: okR(futDay()) });
    assert.ok(new TextEncoder().encode(t).length <= MAX_BYTES && t.endsWith('\nend\n'));
    const gone = groups.map((keys) => !t.split('\n').some((l) => keys.some((x) => l.startsWith(x))));
    assert.ok(gone.every((g, i) => !g || gone.slice(0, i).every(Boolean)), `k=${k}: ${gone}`);   // a group goes only after the ones before it
    seen.add(gone.join(''));
  }
  assert.ok(seen.has([true, false, false, false, false, false].join('')) && seen.has([true, true, false, false, false, false].join('')));
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
  'air-quality-api.open-meteo.com': { body: airData },
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
  assert.ok(body.includes('\nwx_state=ok\nair=38|1\n') && !body.includes('\nfutbol='));   // the template has no fútbol source
  // 7 fetches (the forecast, the air, the rates, this year's holidays, 3 feeds), 7 cache reads and 7 writes
  await Promise.all(d.waits);
  assert.deepEqual([d.calls.length, d.cache.ops], [7, 14]);
  assert.ok(d.calls.some((u) => u.startsWith('https://api.open-meteo.com/') && u.includes('minutely_15=precipitation') && u.includes('forecast_minutely_15=24')));
  assert.ok(d.calls.includes('https://air-quality-api.open-meteo.com/v1/air-quality?latitude=52.52&longitude=13.40&timezone=Europe%2FBerlin&current=us_aqi%2Cuv_index'));
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

test('handle: /futbol answers the format with no source: state none, today or tomorrow, always 200', async () => {
  const d = deps({ answers: ok });
  const r = await handle(new Request('http://couch.test/futbol?d=0'), env, d);
  assert.equal(r.status, 200);
  assert.equal(r.headers.get('content-type'), 'text/plain; charset=utf-8');
  assert.equal(await r.text(), `couch-futbol 1\nnow=1792164000\nday=Today, Fri 16 Oct\nfutbol_age=0\nfutbol_state=none|${FUTBOL_NONE}\nend\n`);
  assert.ok((await (await handle(new Request('http://couch.test/futbol?d=1'), env, d)).text()).includes('\nday=Tomorrow, Sat 17 Oct\n'));
  assert.ok((await (await handle(new Request('http://couch.test/futbol?d=x'), env, d)).text()).includes('\nday=Today, Fri 16 Oct\n'));
  assert.equal(d.calls.length, 0);
  assert.equal(FUTBOL_NONE, 'no f\u00fatbol source in this Worker');
});
