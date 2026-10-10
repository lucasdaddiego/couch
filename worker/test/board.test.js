// node --test: the reply's format against the upstream answers of 2026-10-10 (test/fixtures),
// and the Worker's cache and failure paths with a fake fetch and a fake Cache API.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { boardText, wmoWords, clean, money, localWhen, tzOffset, parseCasas, MAX_BYTES } from '../src/board.js';
import { get, handle } from '../src/index.js';

const fixture = (f) => JSON.parse(readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8'));
const usdData = fixture('dolares.json');
const wxData = fixture('meteo.json');
const env = {
  CITY: 'Buenos Aires', LAT: '-34.6037', LON: '-58.3816', TZ: 'America/Argentina/Buenos_Aires',
  CASAS: 'oficial:Oficial,blue:Blue,bolsa:MEP,contadoconliqui:CCL,cripto:Cripto,tarjeta:Tarjeta',
  LOCALE: 'es-AR', USD_TTL: '300', WX_TTL: '900',
};
const NOW = 1791614218; // 2026-10-10 06:36:58 UTC, 03:36 in Buenos Aires (a Saturday)

test('the reply of the design example, line for line', () => {
  const t = boardText({ now: NOW, env, usd: { state: 'ok', age: 42, data: usdData }, wx: { state: 'ok', age: 310, data: wxData } });
  assert.equal(t, [
    'couch-board 1', 'now=1791614218', 'offset=-10800', 'city=Buenos Aires',
    'usd=Oficial|1.485,00|1.535,00|Fri 15:00', 'usd=Blue|1.520,00|1.540,00|Fri 18:00',
    'usd=MEP|1.535,90|1.542,90|Fri 18:00', 'usd=CCL|1.605,50|1.606,60|Fri 18:00',
    'usd=Cripto|1.601,81|1.606,88|Fri 18:00', 'usd=Tarjeta|1.930,50|1.995,50|Fri 15:00',
    'usd_age=42', 'usd_state=ok',
    'wx_now=11°|10°|92%|Overcast|9 km/h|night',
    'day=Today|Rain showers|16°|11°|96%', 'day=Sun|Overcast|16°|9°|2%', 'day=Mon|Drizzle|16°|10°|22%',
    'wx_age=310', 'wx_state=ok', 'end', '',
  ].join('\n'));
});

test('stale and none: the state lines, the old data with its age, no data lines without data', () => {
  const t = boardText({
    now: NOW, env,
    usd: { state: 'stale', reason: 'dolarapi.com: HTTP 503', age: 4000, data: usdData },
    wx: { state: 'none', reason: 'open-meteo: no answer in 4 s', age: 0, data: null },
  });
  const lines = t.trimEnd().split('\n');
  assert.ok(lines.includes('usd_state=stale|dolarapi.com: HTTP 503'));
  assert.ok(lines.includes('usd_age=4000'));
  assert.equal(lines.filter((l) => l.startsWith('usd=')).length, 6);
  assert.ok(lines.includes('wx_state=none|open-meteo: no answer in 4 s'));
  assert.equal(lines.filter((l) => l.startsWith('wx_now=') || l.startsWith('day=')).length, 0);
  assert.ok(lines.includes('offset=-10800'));                  // from the time zone, also with no forecast
  assert.equal(lines.at(-1), 'end');
});

test('the helpers', () => {
  assert.equal(wmoWords(0), 'Clear');
  assert.equal(wmoWords(82), 'Rain showers');
  assert.equal(wmoWords(99), 'Thunderstorm');
  assert.equal(wmoWords(4), 'Unknown');
  assert.equal(clean('a|b\nc\u0000d  e'), 'a b c d e');
  assert.equal(money(1535, 'es-AR'), '1.535,00');
  assert.equal(money(null, 'es-AR'), '-');
  assert.equal(localWhen(NOW - 3600, NOW, env.TZ), '02:36');
  assert.equal(localWhen(NOW - 6 * 3600, NOW, env.TZ), 'Fri 21:36');
  assert.equal(tzOffset(NOW, env.TZ), -10800);
  assert.equal(tzOffset(NOW, 'UTC'), 0);
  assert.deepEqual(parseCasas('oficial:Oficial,,blue:Bl|ue'), [{ casa: 'oficial', label: 'Oficial' }, { casa: 'blue', label: 'Bl ue' }]);
});

test('the day lines start on now\'s local date, also from a forecast fetched the day before', () => {
  const sun = 1791687840; // 2026-10-11 00:04 in Buenos Aires: the fixture's daily starts on Saturday
  const day = (t) => t.trimEnd().split('\n').filter((l) => l.startsWith('day='));
  assert.deepEqual(day(boardText({ now: sun, env, usd: { state: 'none', reason: 'x', age: 0, data: null }, wx: { state: 'ok', age: 840, data: wxData } })),
    ['day=Today|Overcast|16°|9°|2%', 'day=Mon|Drizzle|16°|10°|22%']);
  const d4 = structuredClone(wxData); // a 4-day forecast (forecast_days=4) still gives three lines
  d4.daily.time.push('2026-10-13');
  for (const k of ['weather_code', 'temperature_2m_max', 'temperature_2m_min', 'precipitation_probability_max']) d4.daily[k].push(d4.daily[k][2]);
  assert.deepEqual(day(boardText({ now: sun, env, usd: { state: 'none', reason: 'x', age: 0, data: null }, wx: { state: 'stale', reason: 'x', age: 72000, data: d4 } })),
    ['day=Today|Overcast|16°|9°|2%', 'day=Mon|Drizzle|16°|10°|22%', 'day=Tue|Drizzle|16°|10°|22%']);
  const bad = structuredClone(wxData); // a value that is not a date is skipped, not thrown
  bad.daily.time[1] = 'soon';
  assert.deepEqual(day(boardText({ now: NOW, env, usd: { state: 'none', reason: 'x', age: 0, data: null }, wx: { state: 'ok', age: 0, data: bad } })),
    ['day=Today|Rain showers|16°|11°|96%', 'day=Mon|Drizzle|16°|10°|22%']);
});

test('the offset is the time zone\'s at now, not the cached forecast\'s (a DST change)', () => {
  const now = Date.UTC(2026, 9, 25, 1, 5) / 1000; // Madrid left summer time at 01:00 UTC
  const t = boardText({ now, env: { ...env, TZ: 'Europe/Madrid' }, usd: { state: 'none', reason: 'x', age: 0, data: null },
    wx: { state: 'ok', age: 600, data: { ...wxData, utc_offset_seconds: 7200 } } });
  assert.ok(t.split('\n').includes('offset=3600'));
});

test('a rate list with a bad element keeps its good rates', () => {
  const t = boardText({ now: NOW, env, usd: { state: 'ok', age: 0, data: [null, 7, ...usdData] }, wx: { state: 'none', reason: 'x', age: 0, data: null } });
  assert.equal(t.split('\n').filter((l) => l.startsWith('usd=')).length, 6);
});

test('a long city is cut, so the reply stays within 4 KB with its end line', () => {
  const t = boardText({ now: NOW, env: { ...env, CITY: 'x'.repeat(4100) }, usd: { state: 'ok', age: 0, data: usdData }, wx: { state: 'ok', age: 0, data: wxData } });
  assert.ok(new TextEncoder().encode(t).length <= MAX_BYTES);
  assert.ok(t.split('\n').includes(`city=${'x'.repeat(100)}`));
  assert.ok(t.endsWith('\nend\n'));
});

test('a long reply drops lines from the end to stay within 4 KB', () => {
  const many = Array.from({ length: 8 }, (_, i) => ({ casa: `c${i}`, compra: 1, venta: 2, fechaActualizacion: '2026-10-10T06:00:00Z' }));
  const e = { ...env, CASAS: many.map((d) => `${d.casa}:${'L'.repeat(500)}`).join(',') };
  const t = boardText({ now: NOW, env: e, usd: { state: 'ok', age: 0, data: many }, wx: { state: 'ok', age: 0, data: wxData } });
  const lines = t.trimEnd().split('\n');
  assert.ok(new TextEncoder().encode(t).length <= MAX_BYTES);
  assert.ok(lines.filter((l) => l.startsWith('usd=')).length < 8);
  assert.equal(lines.at(-1), 'end');
  assert.ok(lines.includes('usd_state=ok') && lines.includes('wx_state=ok'));
});

// ---- the Worker with fakes --------------------------------------------------------

function fakeCache() {
  const m = new Map();
  return {
    m,
    async match(k) { return m.has(k) ? new Response(m.get(k)) : undefined; },
    async put(k, r) { m.set(k, await r.text()); },
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
      const a = answers[new URL(url).hostname];
      if (a instanceof Error) throw a;
      if (!a) throw new TypeError('no route');
      if (a.response) return a.response;
      return new Response(typeof a.body === 'string' ? a.body : JSON.stringify(a.body), { status: a.status || 200 });
    },
  };
}

const ok = { 'dolarapi.com': { body: usdData }, 'api.open-meteo.com': { body: wxData } };

test('get: a fresh fetch is ok and cached; the next call within the TTL makes no fetch', async () => {
  const d = deps({ answers: ok });
  const a = await get('usd', env, NOW, d);
  assert.equal(a.state, 'ok');
  assert.equal(a.age, 0);
  await Promise.all(d.waits);
  assert.equal(d.calls.length, 1);
  const b = await get('usd', env, NOW + 100, d);
  assert.equal(b.state, 'ok');
  assert.equal(b.age, 100);
  assert.equal(d.calls.length, 1);
});

test('get: an old entry and a failed upstream give stale with the real age; no entry gives none', async () => {
  const cache = fakeCache();
  const d1 = deps({ cache, answers: ok });
  await get('wx', env, NOW, d1);
  await Promise.all(d1.waits);
  const d2 = deps({ cache, answers: { 'api.open-meteo.com': { status: 503, body: 'down' } } });
  const s = await get('wx', env, NOW + 2000, d2);
  assert.deepEqual([s.state, s.reason, s.age], ['stale', 'open-meteo: HTTP 503', 2000]);
  assert.ok(s.data.current);
  const timeout = Object.assign(new Error('timed out'), { name: 'TimeoutError' });
  const n = await get('usd', env, NOW, deps({ answers: { 'dolarapi.com': timeout } }));
  assert.deepEqual([n.state, n.reason, n.data], ['none', 'dolarapi.com: no answer in 4 s', null]);
  const bad = await get('usd', env, NOW, deps({ answers: { 'dolarapi.com': { body: '{"not": "a list"}' } } }));
  assert.deepEqual([bad.state, bad.reason], ['none', 'dolarapi.com: bad JSON']);
});

test('get: a body that stalls is no answer, a body that breaks names its error, a bad element is bad JSON', async () => {
  const timeout = Object.assign(new Error('timed out'), { name: 'TimeoutError' });
  const stall = { ok: true, status: 200, json: async () => { throw timeout; } };
  const s = await get('usd', env, NOW, deps({ answers: { 'dolarapi.com': { response: stall } } }));
  assert.deepEqual([s.state, s.reason], ['none', 'dolarapi.com: no answer in 4 s']);
  const broke = { ok: true, status: 200, json: async () => { throw new TypeError('terminated'); } };
  const b = await get('usd', env, NOW, deps({ answers: { 'dolarapi.com': { response: broke } } }));
  assert.deepEqual([b.state, b.reason], ['none', 'dolarapi.com: terminated']);
  const d = deps({ answers: { 'dolarapi.com': { body: '[null]' } } });
  const n = await get('usd', env, NOW, d);
  await Promise.all(d.waits);
  assert.deepEqual([n.state, n.reason, d.cache.m.size], ['none', 'dolarapi.com: bad JSON', 0]);
});

test('handle: a reply that cannot be formatted is still 200 with both states none', async () => {
  const d = deps({ answers: ok });
  const r = await handle(new Request('http://couch.test/board'), { ...env, TZ: 'Not/A_Zone' }, d);
  assert.equal(r.status, 200);
  const body = await r.text();
  assert.ok(body.startsWith('couch-board 1\nnow=1791614218\n'));
  assert.match(body, /\nusd_state=none\|worker: [^\n|]+\nwx_state=none\|worker: [^\n|]+\nend\n$/);
  assert.ok(d.calls.some((u) => u.includes('forecast_days=4')));
});

test('handle: /board is the reply, any other path or method is 404', async () => {
  const d = deps({ answers: ok });
  const r = await handle(new Request('http://couch.test/board'), env, d);
  assert.equal(r.status, 200);
  assert.equal(r.headers.get('content-type'), 'text/plain; charset=utf-8');
  assert.equal(r.headers.get('cache-control'), 'no-store');
  const body = await r.text();
  assert.ok(body.startsWith('couch-board 1\nnow=1791614218\n'));
  assert.ok(body.endsWith('wx_state=ok\nend\n'));
  assert.equal((await handle(new Request('http://couch.test/'), env, d)).status, 404);
  assert.equal((await handle(new Request('http://couch.test/board', { method: 'POST' }), env, d)).status, 404);
});
