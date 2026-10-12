// node --test: the reply's format against the upstream answers of 2026-10-10 (test/fixtures),
// SMN's zip of 2026-10-12 (tiepre.zip), and the Worker's cache and failure paths with a fake
// fetch and a fake Cache API.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { boardText, wmoWords, clean, money, localWhen, tzOffset, parseCasas, MAX_BYTES, fold, skyWords, smnNow } from '../src/board.js';
import { zipEntry, unzipFirst, latin1, obsTime, parseRow, findRow } from '../src/smn.js';
import { get, handle } from '../src/index.js';

const fixture = (f) => JSON.parse(readFileSync(new URL(`./fixtures/${f}`, import.meta.url), 'utf8'));
const usdData = fixture('dolares.json');
const wxData = fixture('meteo.json');
const zipBytes = new Uint8Array(readFileSync(new URL('./fixtures/tiepre.zip', import.meta.url)));
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
    'wx_now=11°|10°|92%|Overcast|9 km/h|night|Open-Meteo',
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
    'wx_age=120', 'wx_state=ok', 'end', '',
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
  assert.equal(d.calls.length, 3);
  const down = deps({ answers: { ...ok, 'ssl.smn.gob.ar': { status: 503, body: 'down' } }, now: SMN_NOW });
  const body = await (await handle(new Request('http://couch.test/board'), smnEnv, down)).text();
  assert.equal(wxNow(body), OM_NOW);
  const off = deps({ answers: smnOk, now: SMN_NOW });
  const plain = await (await handle(new Request('http://couch.test/board'), { ...smnEnv, SMN_STATION: ' ' }, off)).text();
  assert.equal(wxNow(plain), OM_NOW);
  assert.ok(!off.calls.some((u) => u.includes('smn.gob.ar')));
});
