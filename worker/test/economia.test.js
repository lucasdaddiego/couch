// node --test: economia.js's pure functions (the Economía page of Couch 0.6.0) against the shapes
// the sources gave on 2026-10-29, and the template's /economia (no source: state none).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { CATALOGUE, BCRA_IDS, SERIES_IDS, bcraReadings, seriesReadings, econData, econIndicators, histLine, economiaText, numText, ECON_BYTES } from '../src/economia.js';
import { handle, ECON_NONE } from '../src/index.js';

const bcra = (id, ...rows) => ({ results: [{ idVariable: id, detalle: rows.map(([fecha, valor]) => ({ fecha, valor })) }] });
const NOW = 1793297934;                  // 2026-10-29 18:18:54 UTC
const TZ = 'America/Argentina/Buenos_Aires';
const usd = { state: 'ok', age: 0, data: [{ casa: 'oficial', venta: 1560 }, { casa: 'blue', venta: 1565, fechaActualizacion: '2026-10-29T13:00:00.000Z' }] };
const rp = { state: 'ok', age: 0, data: { valor: 763, fecha: '2026-10-28' } };
const BCRA_OK = Object.fromEntries(CATALOGUE.filter((c) => c.bcra).map((c) => [c.bcra, bcra(c.bcra, ['2026-10-28', 10 + c.bcra], ['2026-10-27', 9 + c.bcra])]));
BCRA_OK[31] = bcra(31, ['2026-11-15', 2196.12], ['2026-10-29', 2174.89], ['2026-10-28', 2173.56]);   // published ahead
const SERIES_OK = Object.fromEntries(CATALOGUE.filter((c) => c.series).map((c) => [c.series, { data: [['2026-06-01', 1.5], ['2026-07-01', 2.5]] }]));

test('the catalogue and the readers', () => {
  assert.equal(CATALOGUE.length, 19);
  assert.equal(BCRA_IDS.length, 13);
  assert.equal(SERIES_IDS.length, 3);
  assert.deepEqual(bcraReadings(BCRA_OK[27]), [{ fecha: '2026-10-28', valor: 37 }, { fecha: '2026-10-27', valor: 36 }]);
  assert.deepEqual(bcraReadings({ status: 410 }), []);
  assert.deepEqual(seriesReadings({ data: [['2026-06-01', null], ['2026-07-01', 2.5]] }), [{ fecha: '2026-07-01', valor: 2.5 }]);
  assert.deepEqual([1.9, 1538.4739, -0.00001, 50482].map(numText), ['1.9', '1538.4739', '0', '50482']);
});

test('econIndicators and economiaText: every indicator, the UVA dated today or before, the gap, the tail', () => {
  const d = econData(BCRA_OK, SERIES_OK);
  assert.deepEqual(d.missing, []);
  const out = econIndicators(d, usd, rp, '2026-10-29');
  assert.deepEqual(out.map((i) => i.key), CATALOGUE.map((c) => c.key));
  const by = Object.fromEntries(out.map((i) => [i.key, i]));
  assert.deepEqual([by.uva.value, by.uva.date, by.uva.prev], [2174.89, '2026-10-29', 2173.56]);
  assert.deepEqual([by.desocupacion.value, by.desocupacion.prev], [250, 150]);
  assert.ok(Math.abs(by.brecha.value - (1565 / 1560 - 1) * 100) < 1e-9);
  const text = economiaText({ now: NOW, tz: TZ, r: { state: 'ok', age: 3, data: d }, usd, rp });
  const ls = text.split('\n');
  assert.deepEqual([ls[0], ls[1], ls.at(-4), ls.at(-3), ls.at(-2), ls.at(-1)], ['couch-economia 1', `now=${NOW}`, 'econ_age=3', 'econ_state=ok', 'end', '']);
  assert.equal(ls.filter((l) => l.startsWith('ind=')).length, 19);
  assert.ok(ls.some((l) => l.startsWith('hist=inflacion|')));
  assert.ok(new TextEncoder().encode(text).length < ECON_BYTES);
  assert.equal(histLine(econData({ 27: bcra(27, ['2026-09-30', 1.9]) }, {})), null);
});

test('GET /economia in the template: no source, state none, no indicator', async () => {
  const deps = { now: () => NOW, cache: { async match() { return undefined; }, async put() {} }, waitUntil() {}, async fetch() { throw new Error('no fetch'); } };
  const r = await handle(new Request('http://x/economia'), { TZ }, deps);
  assert.equal(r.status, 200);
  assert.deepEqual((await r.text()).split('\n'), ['couch-economia 1', `now=${NOW}`, 'econ_age=0', `econ_state=none|${ECON_NONE}`, 'end', '']);
});
