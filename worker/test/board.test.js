// node --test: the reply's format against the upstream answers of 2026-10-10 (test/fixtures),
// SMN's zip of 2026-10-12 (tiepre.zip), the answers of 2026-10-13 17:47 UTC for Junin
// (meteo_junin.json with the hourly rain and the sun, riesgo.json, inflacion.json: the last 24
// entries of the 53 KB list, plazofijo.json, clarin.xml), Infobae's feed of 2026-10-13 21:32 UTC
// (infobae.xml: the head of the 687 KB feed, its first 61440 bytes, 10 items and part of the 11th),
// the feeds of NEWS_FEEDS of 2026-10-13 22:04:30 UTC (MIX_NOW: Google News' RSS search for
// Infobae's economy and politics sections, Ambito's economy, Clarin's politics and world, BBC's
// world; every item kept, in the feed's order, but only the first item of a Google News or BBC
// feed whole, the others only their title, link, pubDate and source; Ambito's first 12 items of
// 99; Clarin's feeds whole), and the Worker's cache and failure paths with a fake fetch and a
// fake Cache API.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import {
  boardText, wmoWords, clean, money, localWhen, tzOffset, parseCasas, MAX_BYTES, fold, skyWords, smnNow,
  rainWords, sunTimes, moonPhase, dayWords, moneyLines, newsLines, NEW_MOON_2000, SYNODIC_DAYS,
} from '../src/board.js';
import { zipEntry, unzipFirst, latin1, obsTime, parseRow, findRow } from '../src/smn.js';
import {
  decodeEntities, elementText, cutTitle, parseFeed, parseFeeds, readHead, readFeedHead, dropPublisher, newestFirst,
  TITLE_MAX, ITEMS_MAX, HEAD_BYTES, SORT_ITEMS, SORT_BYTES, FEEDS_MAX, WEIGHT_MAX,
} from '../src/news.js';
import { get, handle, newsUpstream } from '../src/index.js';

const text = (f) => readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8');
const fixture = (f) => JSON.parse(text(f));
const usdData = fixture('dolares.json');
const wxData = fixture('meteo.json');
const zipBytes = new Uint8Array(readFileSync(new URL('./fixtures/tiepre.zip', import.meta.url)));
const junData = fixture('meteo_junin.json');
const rpData = fixture('riesgo.json');
const ipcData = fixture('inflacion.json');
const pfData = fixture('plazofijo.json');
const clarinXml = text('clarin.xml');
const ibXml = text('infobae.xml');
const mix = {
  gnEco: text('gn_infobae_economia.xml'), gnPol: text('gn_infobae_politica.xml'), ambito: text('ambito_economia.xml'),
  clarinPol: text('clarin_politica.xml'), clarinMundo: text('clarin_mundo.xml'), bbc: text('bbc_world.xml'),
};
const MIX_NOW = 1791929070; // 2026-10-13 22:04:30 UTC: the time of the NEWS_FEEDS fixtures
// wrangler.jsonc's vars: its comment lines (each starts with //) removed, then JSON.
const wranglerVars = JSON.parse(readFileSync(new URL('../wrangler.jsonc', import.meta.url), 'utf8')
  .split('\n').filter((l) => !l.trimStart().startsWith('//')).join('\n')).vars;
const env = {
  CITY: 'Buenos Aires', LAT: '-34.6037', LON: '-58.3816', TZ: 'America/Argentina/Buenos_Aires',
  CASAS: 'oficial:Oficial,blue:Blue,bolsa:MEP,contadoconliqui:CCL,cripto:Cripto,tarjeta:Tarjeta',
  LOCALE: 'es-AR', USD_TTL: '300', WX_TTL: '900',
};
const NOW = 1791614218; // 2026-10-10 06:36:58 UTC, 03:36 in Buenos Aires (a Saturday)
const JUN_NOW = 1791913620; // 2026-10-13 17:47 UTC, 14:47 in Junin (a Tuesday): the time of the Junin fixtures
const junEnv = { ...env, CITY: 'Jun\u00edn', PF_BANK: 'Banco de la Naci\u00f3n Argentina', PF_LABEL: 'Plazo fijo BNA (TNA)' };
const okR = (data, age = 0) => ({ state: 'ok', age, data });
const junMoney = () => ({
  rp: okR(rpData), ipc: okR(ipcData.at(-1)), pf: okR({ entidad: 'BANCO DE LA NACION ARGENTINA', tna: pfData[0].tnaClientes }),
});
// Infobae's items are from 21:32 UTC, after JUN_NOW: their age is 0, so they come first.
const junNews = () => [{ label: 'Clar\u00edn', r: okR(parseFeed(clarinXml), 120) }, { label: 'Infobae', r: okR(parseFeed(ibXml), 300) }];

// The first test's reply is the host check's (couch/tools/hostcheck/run.zsh takes this array, line
// for line) and the preview's body_ok: it keeps its text as UTF-8, with no escapes.
test('the reply of 2026-10-13 in Junin, every upstream, line for line', () => {
  const t = boardText({ now: JUN_NOW, env: junEnv, usd: okR(usdData, 42), wx: okR(junData, 310), money: junMoney(), news: junNews() });
  assert.equal(t, [
    'couch-board 1',
    'now=1791913620',
    'offset=-10800',
    'city=Junín',
    'usd=Oficial|1.485,00|1.535,00|Fri 15:00',
    'usd=Blue|1.520,00|1.540,00|Fri 18:00',
    'usd=MEP|1.535,90|1.542,90|Fri 18:00',
    'usd=CCL|1.605,50|1.606,60|Fri 18:00',
    'usd=Cripto|1.601,81|1.606,88|Fri 18:00',
    'usd=Tarjeta|1.930,50|1.995,50|Fri 15:00',
    'usd_age=42',
    'usd_state=ok',
    'money=Riesgo país|589|12 Oct|ok',
    'money=Inflación mensual|1,7%|Aug 2026|ok',
    'money=Plazo fijo BNA (TNA)|19,25%|14:47|ok',
    'wx_now=19°|17°|61%|Overcast|10 km/h|day|Open-Meteo',
    'day=Today|Drizzle|19°|9°|75%',
    'day=Wed|Overcast|20°|10°|2%',
    'day=Thu|Drizzle|19°|12°|48%',
    'wx_rain=rain|Rain now (75%)',
    'wx_sun=06:24|19:16',
    'wx_moon=Waxing crescent moon',
    'wx_age=310',
    'wx_state=ok',
    'news=Infobae|0|EN VIVO Colombia vs. Venezuela - Hoy online: GOL de Colombia, siga aquí el último partido de la Tricolor...',
    'news=Infobae|0|El Gobierno Vasco homenajea en Argentina a la selección de pelota en la previa del Mundial',
    'news=Infobae|0|Turistas pagarán más por ingresar a México a partir de 2027: Diputados aprueban Ley Federal de Derechos',
    'news=Infobae|0|Galbiati: "Hemos perdido lucidez, pero hemos aprendido una lección"',
    'news=Clarín|1335|Una granja solar de Nueva York redujo en un 24% la luz solar otoñal que recibían sus hortalizas, y los...',
    'news=Clarín|1335|Una tortuga marina en peligro de extinción de 5 kilos llamada Pancake fue encontrada aturdida por el frío...',
    'news=Clarín|1498|El ruido por Ramón Díaz y la respuesta de River: el plantel respaldó a Ponzio, ¿qué dicen los dirigentes?',
    'news=Clarín|1611|La embajadora de EEUU en Grecia dimite ante escándalo de presunta corrupción',
    'news_age=300',
    'news_state=ok',
    'end',
    '',
  ].join('\n'));
  assert.ok(new TextEncoder().encode(t).length < MAX_BYTES / 2);  // 1618 bytes: the 4 KB guard drops nothing
});

test('the reply of the design example, line for line', () => {
  const t = boardText({ now: NOW, env, usd: { state: 'ok', age: 42, data: usdData }, wx: { state: 'ok', age: 310, data: wxData } });
  assert.equal(t, [
    'couch-board 1', 'now=1791614218', 'offset=-10800', 'city=Buenos Aires',
    'usd=Oficial|1.485,00|1.535,00|Fri 15:00', 'usd=Blue|1.520,00|1.540,00|Fri 18:00',
    'usd=MEP|1.535,90|1.542,90|Fri 18:00', 'usd=CCL|1.605,50|1.606,60|Fri 18:00',
    'usd=Cripto|1.601,81|1.606,88|Fri 18:00', 'usd=Tarjeta|1.930,50|1.995,50|Fri 15:00',
    'usd_age=42', 'usd_state=ok',
    'wx_now=11°|10°|92%|Overcast|9 km/h|night|Open-Meteo',
    'day=Today|Rain showers|16°|11°|96%', 'day=Sun|Overcast|16°|9°|2%', 'day=Mon|Drizzle|16°|10°|22%',
    'wx_moon=New moon', 'wx_age=310', 'wx_state=ok', 'end', '',
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
      const u = new URL(url);
      // a path (or a path and a query) when one host serves several upstreams
      const a = answers[u.hostname + u.pathname + u.search] ?? answers[u.hostname + u.pathname] ?? answers[u.hostname];
      if (a instanceof Error) throw a;
      if (!a) throw new TypeError('no route');
      if (a.response) return a.response;
      const body = typeof a.body === 'string' || a.body instanceof Uint8Array ? a.body : JSON.stringify(a.body);
      return new Response(body, { status: a.status || 200 });
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

// ---- SMN: the now line ---------------------------------------------------------------

// Two lines of the zip of 2026-10-12, as SMN writes them (Latin-1 decoded; the file ends each line with " / \r\n").
const JUNIN = ' Jun\u00edn;11-octubre-2026;23:00;Nublado;15 km;10.3;No se calcula; 84;Este  3;1011.2 / ';
const BSAS = ' Buenos Aires;11-octubre-2026;23:00;Cubierto;10 km;13.1;No se calcula; 76;Sudeste  9;1017.6 / ';
const OBS = 1791770400;        // 2026-10-11 23:00 in Argentina (UTC-3): 2026-10-12 02:00 UTC
const SMN_NOW = OBS + 1800;    // 23:30 in Buenos Aires, a Sunday: Open-Meteo's fixture gives Today and Mon
const NONE = { state: 'none', reason: 'x', age: 0, data: null };
const JUNIN_ROW = { station: 'Jun\u00edn', at: OBS, time: '23:00', sky: 'Nublado', temp: 10.3, feels: 10.3, humidity: 84, wind: 3 };
const wxNow = (t) => t.split('\n').find((l) => l.startsWith('wx_now='));
const OM_NOW = 'wx_now=11°|10°|92%|Overcast|9 km/h|night|Open-Meteo';

test('smn: the zip reader on the real file of 2026-10-12', async () => {
  const e = zipEntry(zipBytes);
  assert.deepEqual([e.name, e.method, e.size, e.data.length], ['estado_tiempo20261012.txt', 8, 11697, 2899]);
  const z = await unzipFirst(zipBytes);
  assert.equal(z.name, 'estado_tiempo20261012.txt');
  assert.equal(z.bytes.length, 11697);
  const text = latin1(z.bytes);
  assert.ok(text.startsWith('Azul;11-octubre-2026;23:00;Algo nublado;'));
  assert.ok(text.includes(`\r\n${JUNIN}\r\n`));
  assert.ok(text.includes(`\r\n${BSAS}\r\n`));
  assert.equal(text.split('\n').filter((l) => l.trim()).length, 120);
  assert.ok(text.endsWith(' / \r\n'));
});

test('smn: a zip that cannot be read throws or rejects, never a wrong text', async () => {
  assert.throws(() => zipEntry(new TextEncoder().encode('<html>busy</html>')), /bad zip: no end record/);
  assert.throws(() => zipEntry(new Uint8Array(0)), /bad zip/);
  assert.throws(() => zipEntry(zipBytes.subarray(0, zipBytes.length - 22)), /bad zip: no end record/);
  const cd = zipBytes.length - 22 - 71;                        // the central directory: 46 bytes and the 25-byte name
  assert.equal(new DataView(zipBytes.buffer).getUint32(cd, true), 0x02014b50);
  const method = zipBytes.slice();
  new DataView(method.buffer).setUint16(cd + 10, 99, true);
  await assert.rejects(unzipFirst(method), /bad zip: method 99/);
  const size = zipBytes.slice();
  new DataView(size.buffer).setUint32(cd + 24, 11696, true);
  await assert.rejects(unzipFirst(size), /bad zip: the size/);
  const big = zipBytes.slice();
  new DataView(big.buffer).setUint32(cd + 24, (1 << 20) + 1, true);
  await assert.rejects(unzipFirst(big), /over 1 MB/);
  const off = zipBytes.slice();
  new DataView(off.buffer).setUint32(cd + 42, 5, true);       // the local header's offset points at no header
  assert.throws(() => zipEntry(off), /bad zip: no local header/);
});

test('smn: Latin-1 by hand, each byte its code point', () => {
  assert.equal(latin1(new Uint8Array([0x4a, 0x75, 0x6e, 0xed, 0x6e])), 'Jun\u00edn');
  assert.equal(latin1(new Uint8Array([0x80, 0x9f, 0xa0, 0xff])), '\u0080\u009f\u00a0\u00ff'); // windows-1252 would give the euro sign for 0x80
  assert.equal(latin1(new Uint8Array(10000).fill(0x61)), 'a'.repeat(10000));               // more than one chunk of 4096
  assert.equal(latin1(new Uint8Array(0)), '');
});

test('smn: the observation time in Argentina, Spanish month names', () => {
  assert.equal(obsTime('11-octubre-2026', '23:00'), OBS);
  assert.equal(obsTime('1-enero-2027', '00:05'), Date.UTC(2027, 0, 1, 3, 5) / 1000);
  assert.equal(obsTime('5-setiembre-2026', '12:00'), obsTime('5-septiembre-2026', '12:00'));
  assert.equal(obsTime(' 11-OCTUBRE-2026 ', ' 9:30 '), Date.UTC(2026, 9, 11, 12, 30) / 1000);
  for (const [d, t] of [['31-septiembre-2026', '12:00'], ['0-octubre-2026', '12:00'], ['11-october-2026', '12:00'],
    ['11-octubre-2026', '24:00'], ['11-octubre-2026', '12:60'], ['11-octubre-2026', ''], ['', '23:00'], [undefined, undefined]]) {
    assert.equal(obsTime(d, t), null, `${d} ${t}`);
  }
});

test('smn: the rows, Calma, No se calcula, a missing field', () => {
  assert.deepEqual(parseRow(JUNIN), JUNIN_ROW);
  assert.deepEqual(parseRow(BSAS + '\r'),
    { station: 'Buenos Aires', at: OBS, time: '23:00', sky: 'Cubierto', temp: 13.1, feels: 13.1, humidity: 76, wind: 9 });
  const cba = parseRow(' C\u00f3rdoba;11-octubre-2026;23:00;Parcialmente nublado;15 km;11.9;No se calcula; 94;Calma;960.6 / \r');
  assert.deepEqual([cba.station, cba.wind, cba.feels], ['C\u00f3rdoba', 0, 11.9]);
  const mdp = parseRow(' Mar del Plata;11-octubre-2026;23:00;Cubierto;10 km;9.3;8.8; 80;Sudeste  5;1020.8 / ');
  assert.deepEqual([mdp.temp, mdp.feels, mdp.humidity, mdp.wind], [9.3, 8.8, 80, 5]);
  const vrb = parseRow(' X;11-octubre-2026;18:00;Cubierto con lluvia y nevada;8 km;0.5;No se calcula; 93;Direcciones Variables  1;997 / ');
  assert.deepEqual([vrb.temp, vrb.wind], [0.5, 1]);
  assert.equal(parseRow(' X;11-octubre-2026;23:00;Despejado;10 km;-2.4;-5; 60;Sur  20;1020 / ').feels, -5);
  const gap = parseRow(' Jun\u00edn;11-octubre-2026;23:00;Nublado;15 km;10.3;No se calcula;;Este  3;1011.2 / ');
  assert.deepEqual([gap.temp, gap.humidity, gap.wind], [10.3, null, 3]);
  const short = parseRow(' Jun\u00edn;11-octubre-2026;23:00;Nublado;15 km;10.3');
  assert.deepEqual([short.temp, short.feels, short.humidity, short.wind], [10.3, null, null, null]);
  const noTemp = parseRow(' Jun\u00edn;11-octubre-2026;23:00;Nublado;15 km;;No se calcula; 84;Este  3;1011.2 / ');
  assert.deepEqual([noTemp.temp, noTemp.feels], [null, null]);
  assert.equal(parseRow(' Jun\u00edn;11-octubre-2026;23:00;Nublado;15 km'), null);   // no temperature field
  assert.equal(parseRow(';11-octubre-2026;23:00;Nublado;15 km;10.3'), null);          // no name
  assert.equal(parseRow(''), null);
  assert.equal(parseRow(' X;32-octubre-2026;23:00;Nublado;15 km;10.3').at, null);
});

test('smn: the station by its exact name after a trim, case and accents not counted', async () => {
  const text = latin1((await unzipFirst(zipBytes)).bytes);
  for (const name of ['Jun\u00edn', 'Junin', ' JUNIN ', 'jun\u00cdn']) assert.deepEqual(findRow(text, name), JUNIN_ROW, name);
  const ba = findRow(text, 'buenos aires');                    // not "Aeroparque Buenos Aires", which comes first
  assert.deepEqual([ba.station, ba.temp, ba.sky], ['Buenos Aires', 13.1, 'Cubierto']);
  assert.equal(findRow(text, 'Juni'), null);
  assert.equal(findRow(text, 'Jun\u00edn de los Andes'), null);
  assert.equal(findRow(text, ''), null);
  assert.equal(findRow(text, '  '), null);
  assert.equal(findRow(text, undefined), null);
  assert.equal(fold('Jun\u00edn'), 'junin');
});

test('smn: the sky in English words, by keyword in order', () => {
  const cases = [
    ['Nublado con tormenta', 'Thunderstorm'], ['Cubierto con lluvia y nevada', 'Snow'], ['Cubierto con nevada', 'Snow'], ['Nieve', 'Snow'],
    ['Chaparr\u00f3n', 'Rain showers'], ['Nublado con lluvia en la hora anterior', 'Rain'], ['Cubierto con llovizna', 'Drizzle'],
    ['Cubierto con niebla en bancos', 'Fog'], ['Cubierto con neblina', 'Mist'], ['Bruma', 'Haze'], ['Humo', 'Smoke'],
    ['Cubierto', 'Overcast'], ['Cubierto con precipitaci\u00f3n a la vista', 'Overcast'], ['Nublado', 'Cloudy'],
    ['Nublado con rel\u00e1mpagos sin truenos', 'Cloudy'], ['Parcialmente nublado', 'Partly cloudy'], ['Algo nublado', 'Partly cloudy'],
    ['Parcialmente nublado con neblina', 'Mist'], ['Despejado', 'Clear'], ['  DESPEJADO ', 'Clear'],
    ['Polvo|en\tsuspensi\u00f3n', 'Polvo en suspensi\u00f3n'], ['Remolinos de polvo y arena a la vista', 'Remolinos de polvo y are'],
    ['', '-'], [undefined, '-'],
  ];
  for (const [state, words] of cases) assert.equal(skyWords(state), words, state);
});

test('the now line: SMN within 3 h of its observation (and 1 h ahead), else Open-Meteo', () => {
  const at = (t, extra = {}) => ({ state: 'ok', age: 0, data: { ...JUNIN_ROW, at: t, ...extra } });
  const wx = { state: 'ok', age: 0, data: wxData };
  const line = (smn, now = SMN_NOW) => wxNow(boardText({ now, env, usd: NONE, wx, smn }));
  assert.equal(line(at(OBS)), 'wx_now=10°|10°|84%|Cloudy|3 km/h|night|SMN 23:00');
  assert.equal(line(at(SMN_NOW - 3 * 3600)), 'wx_now=10°|10°|84%|Cloudy|3 km/h|night|SMN 20:30');
  assert.equal(line(at(SMN_NOW - 3 * 3600 - 1)), OM_NOW);
  assert.equal(line(at(SMN_NOW + 3600)), 'wx_now=10°|10°|84%|Cloudy|3 km/h|night|SMN 00:30');
  assert.equal(line(at(SMN_NOW + 3601)), OM_NOW);
  assert.equal(line(at(null)), OM_NOW);                        // a date SMN wrote that is not one
  assert.equal(line(at(OBS, { temp: null })), OM_NOW);         // no temperature
  assert.equal(line({ state: 'stale', reason: 'x', age: 900, data: JUNIN_ROW }), 'wx_now=10°|10°|84%|Cloudy|3 km/h|night|SMN 23:00');
  assert.equal(line(NONE), OM_NOW);
  assert.equal(line(undefined), OM_NOW);
  assert.deepEqual(smnNow(at(OBS), SMN_NOW), JUNIN_ROW);
  assert.equal(smnNow({ state: 'ok', age: 0, data: 'Junin' }, SMN_NOW), null);
  // the missing values of a row read "-"; Calma is 0 km/h; the feels-like temperature rounds as the temperature
  assert.equal(line(at(OBS, { feels: -0.4, humidity: null, wind: 0, sky: 'Remolinos de polvo y arena a la vista' })),
    'wx_now=10°|0°|-|Remolinos de polvo y are|0 km/h|night|SMN 23:00');
});

test('the now line: day or night from Open-Meteo when it answered, else the local hour (06:00 to 19:59)', () => {
  const smn = (now) => ({ state: 'ok', age: 0, data: { ...JUNIN_ROW, at: now - 600 } });
  const dayOf = (now, wx) => wxNow(boardText({ now, env, usd: NONE, wx, smn: smn(now) })).split('|')[5];
  const isDay = structuredClone(wxData);
  isDay.current.is_day = 1;
  assert.equal(dayOf(SMN_NOW, { state: 'ok', age: 0, data: wxData }), 'night');
  assert.equal(dayOf(SMN_NOW, { state: 'ok', age: 0, data: isDay }), 'day');            // Open-Meteo wins over the hour (23:30)
  assert.equal(dayOf(SMN_NOW, { state: 'stale', reason: 'x', age: 5000, data: isDay }), 'night'); // stale: the hour
  const local = (h, m) => Date.UTC(2026, 9, 12, h + 3, m) / 1000;   // a local time in Buenos Aires
  assert.equal(dayOf(local(5, 59), NONE), 'night');
  assert.equal(dayOf(local(6, 0), NONE), 'day');
  assert.equal(dayOf(local(12, 0), NONE), 'day');
  assert.equal(dayOf(local(19, 59), NONE), 'day');
  assert.equal(dayOf(local(20, 0), NONE), 'night');
});

test('the reply with SMN and Open-Meteo, line for line; SMN alone gives the now line with no days', () => {
  const e = { ...env, CITY: 'Jun\u00edn' };
  const t = boardText({ now: SMN_NOW, env: e, usd: NONE, wx: { state: 'ok', age: 120, data: wxData }, smn: { state: 'ok', age: 60, data: JUNIN_ROW } });
  assert.equal(t, [
    'couch-board 1', `now=${SMN_NOW}`, 'offset=-10800', 'city=Jun\u00edn',
    'usd_age=0', 'usd_state=none|x',
    'wx_now=10°|10°|84%|Cloudy|3 km/h|night|SMN 23:00',
    'day=Today|Overcast|16°|9°|2%', 'day=Mon|Drizzle|16°|10°|22%',
    'wx_moon=New moon', 'wx_age=120', 'wx_state=ok', 'end', '',
  ].join('\n'));
  const alone = boardText({ now: SMN_NOW, env: e, usd: NONE, wx: { state: 'none', reason: 'open-meteo: HTTP 503', age: 0, data: null },
    smn: { state: 'ok', age: 0, data: JUNIN_ROW } }).trimEnd().split('\n');
  assert.ok(alone.includes('wx_now=10°|10°|84%|Cloudy|3 km/h|night|SMN 23:00'));
  assert.equal(alone.filter((l) => l.startsWith('day=')).length, 0);
  assert.ok(alone.includes('wx_state=none|open-meteo: HTTP 503'));
  assert.equal(alone.filter((l) => l.startsWith('smn')).length, 0);  // SMN has no state line: its row's time decides
});

const smnEnv = { ...env, CITY: 'Jun\u00edn', SMN_STATION: 'Junin' };
const smnOk = { ...ok, 'ssl.smn.gob.ar': { body: zipBytes } };

test('get smn: the zip to the station\'s row, cached as the row under the folded station', async () => {
  const d = deps({ answers: smnOk, now: SMN_NOW });
  const a = await get('smn', smnEnv, SMN_NOW, d);
  assert.deepEqual([a.state, a.age, a.data], ['ok', 0, JUNIN_ROW]);
  assert.deepEqual(d.calls, ['https://ssl.smn.gob.ar/dpd/zipopendata.php?dato=tiepre']);
  await Promise.all(d.waits);
  assert.deepEqual([...d.cache.m.keys()], ['https://couch.internal/smn?station=junin']);
  assert.deepEqual(JSON.parse(d.cache.m.get('https://couch.internal/smn?station=junin')), { at: SMN_NOW, data: JUNIN_ROW });
  const b = await get('smn', { ...smnEnv, SMN_STATION: ' Jun\u00edn ' }, SMN_NOW + 599, d);  // the same key; SMN_TTL 600 s by default
  assert.deepEqual([b.state, b.age, d.calls.length], ['ok', 599, 1]);
  const c = await get('smn', { ...smnEnv, SMN_TTL: '60' }, SMN_NOW + 61, deps({ cache: d.cache, answers: { 'ssl.smn.gob.ar': { status: 500, body: 'x' } } }));
  assert.deepEqual([c.state, c.reason, c.age, c.data], ['stale', 'ssl.smn.gob.ar: HTTP 500', 61, JUNIN_ROW]);
});

test('get smn: no row, a bad zip, no answer: none with the host', async () => {
  const n = await get('smn', { ...smnEnv, SMN_STATION: 'Atl|antis' }, SMN_NOW, deps({ answers: smnOk }));
  assert.deepEqual([n.state, n.reason, n.data], ['none', 'ssl.smn.gob.ar: no row for Atl antis', null]);
  const d = deps({ answers: { 'ssl.smn.gob.ar': { body: '<html>busy</html>' } } });
  const z = await get('smn', smnEnv, SMN_NOW, d);
  await Promise.all(d.waits);
  assert.deepEqual([z.state, z.reason, d.cache.m.size], ['none', 'ssl.smn.gob.ar: bad zip', 0]);
  const timeout = Object.assign(new Error('timed out'), { name: 'TimeoutError' });
  const t = await get('smn', smnEnv, SMN_NOW, deps({ answers: { 'ssl.smn.gob.ar': timeout } }));
  assert.deepEqual([t.state, t.reason], ['none', 'ssl.smn.gob.ar: no answer in 4 s']);
  const stall = { ok: true, status: 200, arrayBuffer: async () => { throw timeout; } };
  const s = await get('smn', smnEnv, SMN_NOW, deps({ answers: { 'ssl.smn.gob.ar': { response: stall } } }));
  assert.deepEqual([s.state, s.reason], ['none', 'ssl.smn.gob.ar: no answer in 4 s']);
});

test('handle: SMN for the now line, Open-Meteo for the days; SMN down or no SMN_STATION: Open-Meteo', async () => {
  const d = deps({ answers: smnOk, now: SMN_NOW });
  const lines = (await (await handle(new Request('http://couch.test/board'), smnEnv, d)).text()).trimEnd().split('\n');
  assert.ok(lines.includes('city=Jun\u00edn'));
  assert.ok(lines.includes('wx_now=10°|10°|84%|Cloudy|3 km/h|night|SMN 23:00'));
  assert.deepEqual(lines.filter((l) => l.startsWith('day=')), ['day=Today|Overcast|16°|9°|2%', 'day=Mon|Drizzle|16°|10°|22%']);
  assert.ok(lines.includes('wx_state=ok') && lines.includes('usd_state=ok'));
  assert.equal(d.calls.length, 5);                              // usd, wx, smn, riesgo pais, inflation: no PF_BANK, no NEWS_FEEDS
  const down = deps({ answers: { ...ok, 'ssl.smn.gob.ar': { status: 503, body: 'down' } }, now: SMN_NOW });
  const body = await (await handle(new Request('http://couch.test/board'), smnEnv, down)).text();
  assert.equal(wxNow(body), OM_NOW);
  const off = deps({ answers: smnOk, now: SMN_NOW });
  const plain = await (await handle(new Request('http://couch.test/board'), { ...smnEnv, SMN_STATION: ' ' }, off)).text();
  assert.equal(wxNow(plain), OM_NOW);
  assert.ok(!off.calls.some((u) => u.includes('smn.gob.ar')));
});

// ---- 2026-10-13: the rain, the sun, the moon ----------------------------------------------------

test('the rain of the next 12 h: the first wet hour (40 % or 0.2 mm) by the times, the highest chance', () => {
  const h = junData.hourly;
  const at = (now) => rainWords(h, now, env.TZ);
  assert.deepEqual(at(JUN_NOW), { wet: true, words: 'Rain now (75%)' });          // 14:47: the hour 14:00 to 15:00 has 55 %
  assert.deepEqual(at(1791894600), { wet: true, words: 'Rain from 10:00 (75%)' }); // 09:30
  assert.deepEqual(at(1792047600), { wet: true, words: 'Rain from 11:00 (48%)' }); // Thu 04:00
  assert.deepEqual(at(1791937800), { wet: false, words: 'No rain in the next 12 h' }); // 21:30: 24 % at most
  // Wed 06:10: dry, though the hours at the same index of the day before were wet (the times count, not the index)
  assert.deepEqual(at(1791969000), { wet: false, words: 'No rain in the next 12 h' });
  assert.deepEqual(at(1792166400), { wet: false, words: 'No rain in the next 10 h' }); // Fri 13:00: the forecast ends at 23:00
  assert.deepEqual(at(1792191600), { wet: false, words: 'No rain in the next 3 h' });
  assert.equal(at(1792191600 + 86400), null);                                      // past the forecast
  assert.equal(rainWords(undefined, JUN_NOW, env.TZ), null);
  assert.equal(rainWords({ time: 'x' }, JUN_NOW, env.TZ), null);
  // the amount alone makes an hour wet; with no chance at all, the amount is the number
  const mmOnly = { time: ['2026-10-13T15:00', '2026-10-13T16:00'], precipitation_probability: [null, 10], precipitation: [0, 0.2] };
  assert.deepEqual(rainWords(mmOnly, JUN_NOW, env.TZ), { wet: true, words: 'Rain from 15:00 (10%)' });
  const noPct = { time: ['2026-10-13T15:00', '2026-10-13T16:00'], precipitation: [0, 0.44] };
  assert.deepEqual(rainWords(noPct, JUN_NOW, env.TZ), { wet: true, words: 'Rain from 15:00 (0.4 mm)' });
  const late = { time: ['2026-10-13T23:00', '2026-10-14T00:00'], precipitation_probability: [10, 80], precipitation: [0, 1] };
  assert.deepEqual(rainWords(late, Date.UTC(2026, 9, 14, 2, 30) / 1000, env.TZ), { wet: true, words: 'Rain now (80%)' }); // 23:30
  assert.deepEqual(rainWords(late, Date.UTC(2026, 9, 14, 0, 30) / 1000, env.TZ), { wet: true, words: 'Rain from 23:00 (80%)' }); // 21:30
});

test('the sun of today, the moon from the date', () => {
  assert.deepEqual(sunTimes(junData.daily, '2026-10-13'), { rise: '06:24', set: '19:16' });
  assert.deepEqual(sunTimes(junData.daily, '2026-10-14'), { rise: '06:22', set: '19:17' });
  assert.equal(sunTimes(junData.daily, '2026-10-20'), null);
  assert.equal(sunTimes(wxData.daily, '2026-10-10'), null);                 // the forecast of 2026-10-10 has no sunrise
  assert.equal(sunTimes(undefined, '2026-10-13'), null);
  const day = 86400;
  assert.equal(moonPhase(NEW_MOON_2000), 'New moon');
  assert.equal(moonPhase(NEW_MOON_2000 + (SYNODIC_DAYS / 4) * day), 'First quarter moon');
  assert.equal(moonPhase(NEW_MOON_2000 + (SYNODIC_DAYS / 2) * day), 'Full moon');
  assert.equal(moonPhase(NEW_MOON_2000 + (SYNODIC_DAYS * 3 / 4) * day), 'Last quarter moon');
  assert.equal(moonPhase(NEW_MOON_2000 - (SYNODIC_DAYS / 2) * day), 'Full moon');   // before the anchor
  assert.equal(moonPhase(Date.UTC(2024, 0, 11, 11, 57) / 1000), 'New moon');      // the new moon of 2024-01-11
  assert.equal(moonPhase(Date.UTC(2024, 0, 25, 17, 54) / 1000), 'Full moon');     // the full moon of 2024-01-25
  assert.equal(moonPhase(JUN_NOW), 'Waxing crescent moon');                       // the sample of 2026-10-13
});

test('the sky lines: no hourly or sun data (a forecast cached before 2026-10-13) gives only the moon', () => {
  const lines = (wx) => boardText({ now: JUN_NOW, env: junEnv, usd: NONE, wx }).split('\n').filter((l) => /^wx_(rain|sun|moon)=/.test(l));
  assert.deepEqual(lines(okR(junData)), ['wx_rain=rain|Rain now (75%)', 'wx_sun=06:24|19:16', 'wx_moon=Waxing crescent moon']);
  assert.deepEqual(lines(okR(wxData)), ['wx_moon=Waxing crescent moon']);
  assert.deepEqual(lines(NONE), ['wx_moon=Waxing crescent moon']);
});

// ---- 2026-10-13: the money extras ----------------------------------------------------------------

test('the money lines: each with its date, stale with the reason, none with no value', () => {
  const m = { now: JUN_NOW, tz: env.TZ, locale: 'es-AR', pfLabel: 'Plazo fijo' };
  assert.deepEqual(moneyLines({ ...m, ...junMoney() }), [
    'money=Riesgo país|589|12 Oct|ok', 'money=Inflación mensual|1,7%|Aug 2026|ok', 'money=Plazo fijo|19,25%|14:47|ok',
  ]);
  assert.deepEqual(moneyLines({
    ...m,
    rp: { state: 'stale', reason: 'argentinadatos.com: HTTP 503', age: 4000, data: rpData },
    ipc: { state: 'none', reason: 'argentinadatos.com: no answer in 4 s', age: 0, data: null },
    pf: { state: 'stale', reason: 'x|y', age: 7200, data: { tna: 0.2 } },
  }), [
    'money=Riesgo país|589|12 Oct|stale|argentinadatos.com: HTTP 503',
    'money=Inflación mensual|-|-|none|argentinadatos.com: no answer in 4 s',
    'money=Plazo fijo|20%|12:47|stale|x y',                     // the fixed-term date is the Worker's read: 2 h ago
  ]);
  assert.deepEqual(moneyLines(m), []);                          // no upstream: no line
  assert.deepEqual(moneyLines({ ...m, rp: okR({ fecha: '2026-10-13', valor: 612.4 }), pfLabel: '' }), ['money=Riesgo país|612|Today|ok']);
  assert.equal(dayWords('2026-01-05', JUN_NOW, env.TZ), '5 Jan');
  assert.equal(dayWords('2026-13-01', JUN_NOW, env.TZ), '-');
  assert.equal(dayWords('soon', JUN_NOW, env.TZ), '-');
});

const AD = 'api.argentinadatos.com/v1/finanzas';
const moneyOk = {
  [`${AD}/indices/riesgo-pais/ultimo`]: { body: rpData }, [`${AD}/indices/inflacion`]: { body: ipcData },
  [`${AD}/tasas/plazoFijo`]: { body: pfData },
};

test('get rp, ipc, pf: the live answers; the inflation and the bank cached as one entry', async () => {
  const d = deps({ answers: moneyOk, now: JUN_NOW });
  const rp = await get('rp', junEnv, JUN_NOW, d), ipc = await get('ipc', junEnv, JUN_NOW, d), pf = await get('pf', junEnv, JUN_NOW, d);
  assert.deepEqual([rp.state, rp.data], ['ok', { valor: 589, fecha: '2026-10-12' }]);
  assert.deepEqual([ipc.state, ipc.data], ['ok', { fecha: '2026-08-31', valor: 1.7 }]);
  assert.deepEqual([pf.state, pf.data], ['ok', { entidad: 'BANCO DE LA NACION ARGENTINA', tna: 0.1925 }]);
  await Promise.all(d.waits);
  assert.deepEqual(JSON.parse(d.cache.m.get('https://couch.internal/ipc')), { at: JUN_NOW, data: { fecha: '2026-08-31', valor: 1.7 } });
  assert.ok(d.cache.m.has('https://couch.internal/pf?bank=banco%20de%20la%20nacion%20argentina'));
  const shuffled = [ipcData.at(-1), ...ipcData.slice(0, -1), { fecha: 'x', valor: 9 }, null];   // the latest date wins, not the last element
  const s = await get('ipc', junEnv, JUN_NOW, deps({ answers: { [`${AD}/indices/inflacion`]: { body: shuffled } } }));
  assert.deepEqual(s.data, { fecha: '2026-08-31', valor: 1.7 });
  const nb = await get('pf', { ...junEnv, PF_BANK: 'Banco Inexistente' }, JUN_NOW, deps({ answers: moneyOk }));
  assert.deepEqual([nb.state, nb.reason], ['none', 'argentinadatos.com: no row for Banco Inexistente']);
  const empty = await get('ipc', junEnv, JUN_NOW, deps({ answers: { [`${AD}/indices/inflacion`]: { body: '[]' } } }));
  assert.deepEqual([empty.state, empty.reason], ['none', 'argentinadatos.com: no entry']);
  const err = await get('rp', junEnv, JUN_NOW, deps({ answers: { [`${AD}/indices/riesgo-pais/ultimo`]: { status: 404, body: '{"error":"Not found"}' } } }));
  assert.deepEqual([err.state, err.reason], ['none', 'argentinadatos.com: HTTP 404']);
  const bad = await get('rp', junEnv, JUN_NOW, deps({ answers: { [`${AD}/indices/riesgo-pais/ultimo`]: { body: '{"error":"x"}' } } }));
  assert.deepEqual([bad.state, bad.reason], ['none', 'argentinadatos.com: bad JSON']);
});

// ---- 2026-10-13: the news -------------------------------------------------------------------------

test('news: entities, CDATA, tags, the cut at a word', () => {
  assert.equal(decodeEntities('A &amp; B &#8220;x&#8221; &#x41;&bull;&nbsp;&foo; &#0;.'), 'A & B “x” A• &foo; .');
  assert.equal(elementText('<![CDATA[Ramón & <b>River</b>]]> &amp; m&aacute;s'), 'Ramón & River & más');
  assert.equal(elementText('Oficial | USCIS\n  ok'), 'Oficial USCIS ok');
  assert.equal(elementText('&lt;i&gt;Hola&lt;/i&gt;'), 'Hola');      // an escaped tag is stripped too
  assert.equal(cutTitle('short'), 'short');
  assert.equal(cutTitle('palabra '.repeat(30)), `${'palabra '.repeat(12)}palabra...`);
  assert.equal(cutTitle('a'.repeat(200)), `${'a'.repeat(TITLE_MAX - 3)}...`);
  assert.equal(cutTitle('ñ'.repeat(20), 10), `${'ñ'.repeat(7)}...`);  // code points, not bytes
});

test('news: the live feeds of 2026-10-13', () => {
  const c = parseFeed(clarinXml), ib = parseFeed(ibXml);
  assert.equal(c.length, ITEMS_MAX);                                  // 8 of its 10 items
  assert.deepEqual(c[3], { title: 'La embajadora de EEUU en Grecia dimite ante escándalo de presunta corrupción', at: 1791912009 });
  assert.equal(c[2].title, 'El ruido por Ramón Díaz y la respuesta de River: el plantel respaldó a Ponzio, ¿qué dicen los dirigentes?');
  assert.equal(c[4].title, 'Oficial y confirmado USCIS ratifica que todos los extranjeros que no llenen el formulario AR-11 en un...');
  assert.ok(c.every((x) => Array.from(x.title).length <= TITLE_MAX && Number.isFinite(x.at)));
  assert.equal(ib.length, ITEMS_MAX);
  assert.deepEqual(ib[1], { title: 'El Gobierno Vasco homenajea en Argentina a la selecci\u00f3n de pelota en la previa del Mundial', at: 1791927139 });
  assert.deepEqual([ib[0].at, ib[7].at], [1791927152, 1791927105]);   // 21:32:32 and 21:31:45 UTC
  assert.ok(ib.every((x, i) => i === 0 || ib[i - 1].at >= x.at));     // newest first: the head holds the newest
  const odd = '<rss><channel><title>Feed</title><item><title></title></item>' +
    '<item><title>Uno &amp; dos</title><pubDate>Tue, 30 Nov -0001 00:00:00 -0353</pubDate></item>' +
    '<item><title>Tres</title><dc:date>2026-10-13T12:00:00Z</dc:date></item></channel></rss>';
  // the newest first, an item with no date last
  assert.deepEqual(parseFeed(odd), [{ title: 'Tres', at: Date.UTC(2026, 9, 13, 12) / 1000 }, { title: 'Uno & dos', at: null }]);
  assert.deepEqual(parseFeed('<html>busy</html>'), []);
});

test('news: NEWS_FEEDS, the fair share per feed, the newest first, the states', () => {
  assert.deepEqual(parseFeeds('Clarín=https://www.clarin.com/rss/lo-ultimo/,Infobae=https://www.infobae.com/arc/outboundfeeds/rss/'), [
    { label: 'Clarín', url: 'https://www.clarin.com/rss/lo-ultimo/', weight: 1 },
    { label: 'Infobae', url: 'https://www.infobae.com/arc/outboundfeeds/rss/', weight: 1 },
  ]);
  assert.deepEqual(parseFeeds(' X = https://a.example/f?x=1 ,=https://b.example/,Nolabel,Y=ftp://c.example/,Z=not a url'),
    [{ label: 'X', url: 'https://a.example/f?x=1', weight: 1 }]);
  assert.equal(parseFeeds(Array.from({ length: 8 }, (_, i) => `F${i}=https://f${i}.example/`).join(',')).length, FEEDS_MAX);
  assert.deepEqual(parseFeeds(''), []);
  const a = Array.from({ length: 10 }, (_, i) => ({ title: `A${i}`, at: 109 - i }));
  const b = [{ title: 'B0', at: null }, { title: 'B1', at: 150 }, { title: ' ', at: 160 }];
  assert.deepEqual(newsLines([{ label: 'A', r: okR(a, 30) }, { label: 'B', r: { state: 'stale', reason: 'b.example: HTTP 503', age: 900, data: b } }], 200, 8), [
    'news=B|50|B1', 'news=A|91|A0', 'news=A|92|A1', 'news=A|93|A2', 'news=A|94|A3', 'news=A|95|A4', 'news=A|96|A5', 'news=B|-|B0',
    'news_age=900', 'news_state=stale|b.example: HTTP 503',
  ]);
  assert.deepEqual(newsLines([{ label: 'A', r: { state: 'none', reason: 'a.example: no items', age: 0, data: null } }], 200, 8),
    ['news_age=0', 'news_state=none|a.example: no items']);
  assert.deepEqual(newsLines([], 200, 8), []);
});

// A body in chunks of size bytes, as a fetch's: pulls counts the chunks read, cancelled marks a
// body whose rest was dropped. highWaterMark 0: a chunk is made only when the reader asks for it.
function chunked(s, size) {
  const b = typeof s === 'string' ? new TextEncoder().encode(s) : s;
  const st = { pulls: 0, cancelled: false, chunks: Math.ceil(b.length / size) };
  st.body = new ReadableStream({
    pull(c) {
      const i = st.pulls * size;
      if (i >= b.length) return c.close();
      st.pulls++;
      c.enqueue(b.slice(i, i + size));
    },
    cancel() { st.cancelled = true; },
  }, { highWaterMark: 0 });
  return st;
}
const bytesOf = (s) => new TextEncoder().encode(s).length;

test('news: only the head of a feed is read: the end of the 8th item, the byte cap, or the end', async () => {
  // The cut: Infobae's head (61440 bytes, 10 items) stops at the end of its 8th item, byte 50320,
  // and the rest of the body is cancelled.
  assert.equal(bytesOf(ibXml), 61440);
  const s = chunked(ibXml, 4096);
  const head = await readHead(s.body);
  assert.equal(bytesOf(head), 50320);
  assert.ok(ibXml.startsWith(head) && head.endsWith('</item>'));
  assert.deepEqual([s.cancelled, s.pulls < s.chunks], [true, true]);
  assert.deepEqual(parseFeed(head), parseFeed(ibXml));
  // The byte cap inside an item: the 5th item ends at byte 30933, so a cut at 30000 keeps 4 items.
  // A cut inside a character (30003: the 2nd byte of an "\u00f3") drops that character.
  const c = chunked(ibXml, 1000);
  const cut = await readHead(c.body, { bytes: 30000 });
  assert.deepEqual([bytesOf(cut), c.cancelled], [30000, true]);
  assert.deepEqual(parseFeed(cut), parseFeed(ibXml).slice(0, 4));
  const mid = await readHead(chunked(ibXml, 1000).body, { bytes: 30003 });
  assert.ok(bytesOf(mid) === 30002 && !mid.includes('\ufffd') && mid.endsWith('porci'));
  // The default cap, HEAD_BYTES (96 KB), on a body with no items.
  const x = chunked('x'.repeat(200000), 16384);
  assert.deepEqual([(await readHead(x.body)).length, x.cancelled], [HEAD_BYTES, true]);
  // A feed shorter than the cap: Clarin (11845 bytes, 10 items), with room for 20 items, is read
  // whole, in 7-byte chunks that split tags and accents, every chunk read, nothing cancelled.
  const w = chunked(clarinXml, 7);
  assert.equal(await readHead(w.body, { items: 20 }), clarinXml);
  assert.deepEqual([w.pulls, w.cancelled], [w.chunks, false]);
  // A </item> split by the chunks still counts.
  const feed = `<rss><channel>${['Uno', 'Dos', 'Tres'].map((t) => `<item><title>${t}</title></item>`).join('')}</channel></rss>`;
  const two = feed.slice(0, feed.indexOf('Tres') - '<item><title>'.length);
  for (const size of [1, 3, 5, 7]) {
    const f = chunked(feed, size);
    assert.deepEqual([await readHead(f.body, { items: 2 }), f.cancelled], [two, true]);
    assert.equal(await readHead(chunked(feed, size).body), feed);     // 3 items: the whole feed
  }
  assert.equal(await readHead(null), '');
});

test('get news: one cache entry per feed URL, the parsed items; a failed feed names its host', async () => {
  const feed = { label: 'Clarín', url: 'https://www.clarin.com/rss/lo-ultimo/' };
  const d = deps({ answers: { 'www.clarin.com': { body: clarinXml } }, now: JUN_NOW });
  const r = await get(newsUpstream(feed), junEnv, JUN_NOW, d);
  assert.deepEqual([r.state, r.data.length, r.data[3].at], ['ok', 8, 1791912009]);
  await Promise.all(d.waits);
  assert.deepEqual([...d.cache.m.keys()], ['https://couch.internal/news?u=https%3A%2F%2Fwww.clarin.com%2Frss%2Flo-ultimo%2F']);
  const again = await get(newsUpstream(feed), junEnv, JUN_NOW + 599, d);           // NEWS_TTL 600 s by default
  assert.deepEqual([again.state, again.age, d.calls.length], ['ok', 599, 1]);
  const f = await get(newsUpstream(feed), junEnv, JUN_NOW, deps({ answers: { 'www.clarin.com': { status: 403, body: 'no' } } }));
  assert.deepEqual([f.state, f.reason], ['none', 'clarin.com: HTTP 403']);
  const h = await get(newsUpstream(feed), junEnv, JUN_NOW, deps({ answers: { 'www.clarin.com': { body: '<html>busy</html>' } } }));
  assert.deepEqual([h.state, h.reason], ['none', 'clarin.com: no items']);
  // Infobae through the Worker's fetch: 8 items from the head, the rest of the body cancelled.
  const ib = { label: 'Infobae', url: 'https://www.infobae.com/arc/outboundfeeds/rss/' };
  const st = chunked(ibXml, 4096);
  const g = await get(newsUpstream(ib), junEnv, JUN_NOW, deps({ answers: { 'www.infobae.com': { response: new Response(st.body) } } }));
  assert.deepEqual([g.state, g.data.length, g.data[0].at, g.data[7].at, st.cancelled], ['ok', 8, 1791927152, 1791927105, true]);
});

test('the 4 KB guard drops the news first, then the money and the sky, then the rate and day lines', () => {
  const long = Array.from({ length: 8 }, (_, i) => ({ title: `${'T'.repeat(600)}${i}`, at: JUN_NOW - i }));
  const t = boardText({ now: JUN_NOW, env: junEnv, usd: okR(usdData), wx: okR(junData), money: junMoney(), news: [{ label: 'L', r: okR(long) }] });
  const lines = t.trimEnd().split('\n'), count = (k) => lines.filter((l) => l.startsWith(k)).length;
  assert.ok(new TextEncoder().encode(t).length <= MAX_BYTES);
  assert.ok(count('news=') >= 1 && count('news=') < 8);
  assert.deepEqual([count('money='), count('usd='), count('day='), count('wx_rain='), count('wx_moon=')], [3, 6, 3, 1, 1]);
  assert.deepEqual(lines.filter((l) => l.startsWith('news=')).map((l) => l.at(-1)), ['0', '1', '2', '3', '4', '5', '6', '7'].slice(0, count('news=')));
  assert.ok(lines.includes('news_state=ok') && lines.at(-1) === 'end');
  const many = Array.from({ length: 8 }, (_, i) => ({ casa: `c${i}`, compra: 1, venta: 2, fechaActualizacion: '2026-10-13T06:00:00Z' }));
  const e = { ...junEnv, CASAS: many.map((d) => `${d.casa}:${'L'.repeat(500)}`).join(',') };
  const u = boardText({ now: JUN_NOW, env: e, usd: okR(many), wx: okR(junData), money: junMoney(), news: [{ label: 'L', r: okR(long) }] });
  const ul = u.trimEnd().split('\n');
  assert.ok(new TextEncoder().encode(u).length <= MAX_BYTES);
  assert.ok(ul.filter((l) => l.startsWith('usd=')).length < 8);
  assert.equal(ul.filter((l) => /^(news|money|wx_rain|wx_sun|wx_moon)=/.test(l)).length, 0);
  assert.ok(ul.includes('usd_state=ok') && ul.includes('news_state=ok') && ul.at(-1) === 'end');
});

test('handle: every upstream of 2026-10-13, two feeds, PF_BANK; a dead feed leaves the other', async () => {
  const feeds = 'Clarín=https://www.clarin.com/rss/lo-ultimo/,Infobae=https://www.infobae.com/arc/outboundfeeds/rss/';
  const e = { ...junEnv, NEWS_FEEDS: feeds };
  const all = { ...moneyOk, 'dolarapi.com': { body: usdData }, 'api.open-meteo.com': { body: junData },
    'www.clarin.com': { body: clarinXml }, 'www.infobae.com': { body: ibXml } };
  const d = deps({ answers: all, now: JUN_NOW });
  const body = await (await handle(new Request('http://couch.test/board'), e, d)).text();
  const lines = body.trimEnd().split('\n');
  assert.equal(d.calls.length, 7);                              // usd, wx, riesgo pais, inflation, plazo fijo, two feeds: no SMN_STATION
  assert.ok(d.calls.some((u) => u.includes('hourly=precipitation_probability%2Cprecipitation') && u.includes('sunrise%2Csunset')));
  assert.deepEqual(lines.filter((l) => l.startsWith('money=')),
    ['money=Riesgo país|589|12 Oct|ok', 'money=Inflación mensual|1,7%|Aug 2026|ok', 'money=Plazo fijo BNA (TNA)|19,25%|14:47|ok']);
  assert.ok(lines.includes('wx_rain=rain|Rain now (75%)') && lines.includes('wx_sun=06:24|19:16'));
  assert.equal(lines.filter((l) => l.startsWith('news=')).length, 8);
  assert.ok(lines.includes('news_state=ok') && body.endsWith('news_state=ok\nend\n'));
  const dead = deps({ answers: { ...all, 'www.infobae.com': { status: 500, body: 'x' } }, now: JUN_NOW });
  const half = (await (await handle(new Request('http://couch.test/board'), e, dead)).text()).trimEnd().split('\n');
  assert.equal(half.filter((l) => l.startsWith('news=Clarín|')).length, 8);  // the fair share goes to the feed that answered
  assert.ok(half.includes('news_state=stale|infobae.com: HTTP 500'));
  const off = deps({ answers: all, now: JUN_NOW });
  const none = await (await handle(new Request('http://couch.test/board'), { ...junEnv, PF_BANK: ' ' }, off)).text();
  assert.ok(!/\nnews/.test(none) && !none.includes('Plazo fijo') && off.calls.length === 4);
});

// ---- 2026-10-13: the mix of NEWS_FEEDS (Infobae through Google News, Ambito, Clarin, BBC) ------------

test('news: the publisher that Google News appends goes; the weights of NEWS_FEEDS', () => {
  assert.equal(dropPublisher('Dólar hoy - Infobae', 'Infobae'), 'Dólar hoy');
  assert.equal(dropPublisher('Dólar hoy – Infobae', 'Infobae'), 'Dólar hoy');
  assert.equal(dropPublisher('Sobre Infobae - Infobae', 'Infobae'), 'Sobre Infobae');
  assert.equal(dropPublisher('Dólar hoy - Infobae', ''), 'Dólar hoy - Infobae');           // no <source>: no change
  assert.equal(dropPublisher('Dólar hoy - Infobae', 'Clarín'), 'Dólar hoy - Infobae');
  assert.equal(dropPublisher('Infobae', 'Infobae'), 'Infobae');                              // only the publisher: it stays
  const gn = '<rss><channel><item><title>Uno - Infobae</title><source url="https://www.infobae.com">Infobae</source></item>' +
    `<item><title>${'palabra '.repeat(13)}fin - Infobae</title><source url="https://www.infobae.com">Infobae</source></item>` +
    '<item><title>Dos - Infobae</title></item></channel></rss>';
  // the publisher goes before the cut: 13 words and "fin" are 107 code points (whole), 117 with the publisher
  assert.deepEqual(parseFeed(gn).map((x) => x.title), ['Uno', `${'palabra '.repeat(13)}fin`, 'Dos - Infobae']);
  assert.equal(Array.from(`${'palabra '.repeat(13)}fin`).length, 107);
  assert.deepEqual(parseFeeds(`Infobae*2=https://a.example/rss?q=site:x.com/y+when:1d&hl=es-419, Ámbito * 3 =https://b.example/,C*0=https://c.example/,D*${WEIGHT_MAX + 1}=https://d.example/,E*x=https://e.example/`), [
    { label: 'Infobae', url: 'https://a.example/rss?q=site:x.com/y+when:1d&hl=es-419', weight: 2 },
    { label: 'Ámbito', url: 'https://b.example/', weight: 3 },
    { label: 'E*x', url: 'https://e.example/', weight: 1 },                 // no number after "*": part of the label
  ]);
  // wrangler.jsonc: 3 economy, 3 politics, 2 world, 8 lines in all
  const live = parseFeeds(wranglerVars.NEWS_FEEDS);
  assert.deepEqual(live.map((f) => `${f.label}*${f.weight}`), ['Infobae*2', 'Ámbito*1', 'Infobae*2', 'Clarín*1', 'Clarín*1', 'BBC*1']);
  assert.equal(live.reduce((a, f) => a + f.weight, 0), Number(wranglerVars.NEWS_MAX));
  assert.equal(live[0].url, 'https://news.google.com/rss/search?q=site:infobae.com/economia+when:1d&hl=es-419&gl=AR&ceid=AR:es-419');
  // the weights: first each feed's share, then one per feed in turn; with no weight, 1
  const f = (p, n, t0) => Array.from({ length: n }, (_, i) => ({ title: `${p}${i}`, at: t0 - i }));
  assert.deepEqual(newsLines([{ label: 'A', weight: 2, r: okR(f('A', 5, 100)) }, { label: 'B', r: okR(f('B', 5, 50)) },
    { label: 'C', weight: 1, r: okR(f('C', 1, 10)) }], 200, 8), [
    'news=A|100|A0', 'news=A|101|A1', 'news=A|102|A2', 'news=A|103|A3', 'news=B|150|B0', 'news=B|151|B1', 'news=B|152|B2', 'news=C|190|C0',
    'news_age=0', 'news_state=ok',
  ]);
  // weights over NEWS_MAX: the feeds earlier in NEWS_FEEDS first
  assert.deepEqual(newsLines([{ label: 'A', weight: 6, r: okR(f('A', 8, 100)) }, { label: 'B', weight: 6, r: okR(f('B', 8, 50)) }], 200, 8)
    .filter((l) => l.startsWith('news=')).map((l) => l.split('|')[2]), ['A0', 'A1', 'A2', 'A3', 'A4', 'A5', 'B0', 'B1']);
});

// The end of the n-th </item> of a text.
function itemEnd(s, n) {
  let i = -1;
  for (let k = 0; k < n; k++) i = s.indexOf('</item>', i + 1);
  return i + 7;
}

test('news: a feed that is not newest first is read further and sorted; one that is stops at its 8th item', async () => {
  // Ambito is newest first: the read stops at the end of its 8th item, byte 6702, and the rest is cancelled.
  const a = chunked(mix.ambito, 1024);
  const ah = await readFeedHead(a.body);
  assert.deepEqual([bytesOf(ah), ah.endsWith('</item>'), a.cancelled, newestFirst(ah)], [6702, true, true, true]);
  // Google News sorts by relevance: its first 8 items are not in order, so all 45 are read
  // (13876 bytes here, 100245 live), and sorted. Its head in its own order has 2 of its 8 newest.
  const g = chunked(mix.gnEco, 1024);
  const gh = await readFeedHead(g.body);
  assert.deepEqual([gh === mix.gnEco, newestFirst(mix.gnEco.slice(0, itemEnd(mix.gnEco, 8))), g.pulls, g.chunks], [true, false, g.chunks, g.chunks]);
  const eco = parseFeed(gh);
  assert.deepEqual(eco[0], { title: "Carrefour lanzó descuentos de hasta 50% y cuotas sin interés por el Día de la Madre", at: 1791927993 });
  assert.ok(eco.every((x, i) => (i === 0 || eco[i - 1].at >= x.at) && MIX_NOW - x.at < 86400 && !x.title.endsWith('Infobae')));
  assert.ok(gh.includes('<pubDate>Mon, 09 Dec 2019 03:01:53 GMT</pubDate>'));         // an old item that "when:1d" gives: last
  const own = new Set(parseFeed(gh.slice(0, itemEnd(gh, 8))).map((x) => x.title));
  assert.equal(eco.filter((x) => own.has(x.title)).length, 2);
  // BBC's world feed: its newest item is its 20th of 23; read whole.
  const bh = await readFeedHead(chunked(mix.bbc, 1024).body);
  assert.deepEqual([bh === mix.bbc, parseFeed(bh)[0]], [true, { title: "Why the lonely in China are hiring 'temporary children'", at: 1791928820 }]);
  // The caps of a feed that is not in order: SORT_ITEMS items (here 150, the oldest first) ...
  const item = (i, pad = '') => `<item><title>T${i}</title><pubDate>${new Date(Date.UTC(2026, 9, 13, 0, i)).toUTCString()}</pubDate>${pad}</item>`;
  const many = (pad) => `<rss><channel>${Array.from({ length: 150 }, (_, i) => item(i, pad)).join('')}</channel></rss>`;
  const o = chunked(many(''), 4096);
  const oh = await readFeedHead(o.body);
  assert.deepEqual([oh.split('</item>').length - 1, oh.endsWith('</item>'), o.cancelled], [SORT_ITEMS, true, true]);
  assert.deepEqual(parseFeed(oh).map((x) => x.title), Array.from({ length: ITEMS_MAX }, (_, k) => `T${SORT_ITEMS - 1 - k}`));
  // ... else SORT_BYTES bytes (items of about 4 KB): the newest complete items of the head.
  const big = chunked(many(`<description>${'x'.repeat(4000)}</description>`), 4096);
  const bigHead = await readFeedHead(big.body);
  const done = bigHead.split('</item>').length - 1;
  assert.deepEqual([bytesOf(bigHead), big.cancelled, done < SORT_ITEMS], [SORT_BYTES, true, true]);
  assert.deepEqual(parseFeed(bigHead).map((x) => x.title), Array.from({ length: ITEMS_MAX }, (_, k) => `T${done - 1 - k}`));
  // A raised cap reads the rest of the chunk that the first cap cut (one chunk of 130 bytes, cut at 80).
  const feed = `<rss><channel>${['Uno', 'Dos', 'Tres'].map((t) => `<item><title>${t}</title></item>`).join('')}</channel></rss>`;
  const raise = () => ({ items: 3, bytes: 1000 });
  assert.equal(await readHead(chunked(feed, 4096).body, { items: 2, bytes: 80, more: raise }), feed.slice(0, feed.indexOf('</channel>')));
  assert.equal(await readHead(chunked(feed, 4096).body, { items: 2, bytes: 80 }), feed.slice(0, itemEnd(feed, 2)));
  assert.equal(await readHead(chunked(feed, 4096).body, { items: 2, bytes: 80, more: () => null }), feed.slice(0, itemEnd(feed, 2)));
});

test('handle: the NEWS_FEEDS of wrangler.jsonc: 3 economy, 3 politics, 2 world; a dead feed leaves its share to the others', async () => {
  const e = { ...junEnv, NEWS_FEEDS: wranglerVars.NEWS_FEEDS, NEWS_MAX: wranglerVars.NEWS_MAX };
  const gn = (s) => `news.google.com/rss/search?q=site:infobae.com/${s}+when:1d&hl=es-419&gl=AR&ceid=AR:es-419`;
  const all = { ...moneyOk, 'dolarapi.com': { body: usdData }, 'api.open-meteo.com': { body: junData },
    [gn('economia')]: { body: mix.gnEco }, [gn('politica')]: { body: mix.gnPol }, 'www.ambito.com': { body: mix.ambito },
    'www.clarin.com/rss/politica/': { body: mix.clarinPol }, 'www.clarin.com/rss/mundo/': { body: mix.clarinMundo }, 'feeds.bbci.co.uk': { body: mix.bbc } };
  const news = async (answers) => {
    const d = deps({ answers, now: MIX_NOW });
    const body = await (await handle(new Request('http://couch.test/board'), e, d)).text();
    assert.ok(bytesOf(body) <= MAX_BYTES && body.endsWith('\nend\n'));
    return { calls: d.calls, lines: body.trimEnd().split('\n').filter((l) => l.startsWith('news')) };
  };
  const fine = await news(all);
  assert.equal(fine.calls.length, 11);                            // usd, wx, riesgo pais, inflation, plazo fijo, six feeds: no SMN_STATION
  assert.deepEqual(fine.lines, [
    "news=BBC|250|Why the lonely in China are hiring 'temporary children'",
    "news=Infobae|964|Tomaron la Facultad de Filosofía y Letras de la UBA y permanecerá así hasta la marcha del 15 de octubre",
    "news=Infobae|1077|Carrefour lanzó descuentos de hasta 50% y cuotas sin interés por el Día de la Madre",
    "news=Infobae|1078|“Elegimos Estar”: Arcor presentó su nueva campaña para celebrar el 75° aniversario del grupo",
    "news=Infobae|2069|La mesa política retomó las reuniones: persisten las diferencias y no hubo avances con la reforma electoral",
    "news=Clarín|3932|Vialidad: la Cámara de Casación rechazó darle la prisión domiciliaria a otro ex funcionario K",
    "news=Ámbito|3990|Aumentó la venta de viviendas usadas en EEUU, pero la tasa hipotecaria a 30 años se disparó",
    "news=Clarín|5898|Qué dice el comunicado completo que confirma el diagnóstico de cáncer de pulmón del papa León XIV",
    'news_age=0',
    'news_state=ok',
  ]);
  // Infobae's economy feed dead: its 2 lines go to the others in turn (Ambito, then Infobae's politics).
  const dead = await news({ ...all, [gn('economia')]: { status: 503, body: 'x' } });
  assert.deepEqual(dead.lines, [
    "news=BBC|250|Why the lonely in China are hiring 'temporary children'",
    "news=Infobae|964|Tomaron la Facultad de Filosofía y Letras de la UBA y permanecerá así hasta la marcha del 15 de octubre",
    "news=Infobae|2069|La mesa política retomó las reuniones: persisten las diferencias y no hubo avances con la reforma electoral",
    "news=Clarín|3932|Vialidad: la Cámara de Casación rechazó darle la prisión domiciliaria a otro ex funcionario K",
    "news=Ámbito|3990|Aumentó la venta de viviendas usadas en EEUU, pero la tasa hipotecaria a 30 años se disparó",
    "news=Ámbito|5010|Inflación de septiembre: cuáles fueron los productos básicos que más subieron en el mes",
    "news=Infobae|5660|El Gobierno celebró el dato de inflación de septiembre: “Es un buen número comparando de dónde venimos”",
    "news=Clarín|5898|Qué dice el comunicado completo que confirma el diagnóstico de cáncer de pulmón del papa León XIV",
    'news_age=0',
    'news_state=stale|news.google.com: HTTP 503',
  ]);
});
