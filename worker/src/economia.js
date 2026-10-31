// The Economía page of Couch 0.6.0 (since 2026-10-29): GET /economia, the indicators of the
// Argentine economy from the BCRA's statistics API v4 (the variables by id, their newest readings),
// INDEC's series on datos.gob.ar (the last two readings), DolarAPI (the blue and the gap, the
// Board's usd upstream) and ArgentinaDatos (the country risk, the Board's rp upstream). Pure
// functions over the raw readings; index.js fetches them (the econ upstream) and routes the reply.
// The catalogue is the one of `dolar economia` (github.com/lucasdaddiego/dolar), so the TV and the
// terminal show the same numbers.

import { clean, localDate } from './board.js';

export const ECON_VERSION = 1;
export const ECON_BYTES = 8192;          // Couch's ECON_MAX
export const BCRA = 'https://api.bcra.gob.ar/estadisticas/v4.0/monetarias';
export const SERIES = 'https://apis.datos.gob.ar/series/api/series/';
export const HIST_N = 13;                // the inflation chart: 13 months
const ENC = new TextEncoder();

// One line of the catalogue: a BCRA variable (bcra), an INDEC series (series), or one made from
// the dollar quotes or the country risk (neither). scale multiplies the source's value (the
// unemployment rate comes as a fraction); limit is the readings asked of the BCRA (the UVA, the
// CER and the ICL are published days ahead, and the inflation keeps a year for the chart).
export const CATALOGUE = [
  { key: 'inflacion', label: 'Inflación mensual', section: 'Precios', unit: '%', bcra: 27, limit: HIST_N },
  { key: 'inflacion_interanual', label: 'Inflación interanual', section: 'Precios', unit: '%', bcra: 28 },
  { key: 'rem', label: 'Inflación esperada 12 meses (REM)', section: 'Precios', unit: '%', bcra: 29 },
  { key: 'uva', label: 'UVA', section: 'Precios', unit: 'ARS', bcra: 31, limit: 20 },
  { key: 'cer', label: 'CER', section: 'Precios', unit: 'index', bcra: 30, limit: 20 },
  { key: 'icl', label: 'ICL (alquileres)', section: 'Precios', unit: 'index', bcra: 40, limit: 20 },
  { key: 'linea_pobreza', label: 'Línea de pobreza (INDEC)', section: 'Precios', unit: 'ARS', series: '150.1_LA_POBREZA_0_D_13' },
  { key: 'mayorista', label: 'Dólar mayorista (A3500)', section: 'Dólar', unit: 'ARS', bcra: 5 },
  { key: 'minorista', label: 'Dólar minorista (BCRA)', section: 'Dólar', unit: 'ARS', bcra: 4 },
  { key: 'blue', label: 'Dólar blue', section: 'Dólar', unit: 'ARS' },
  { key: 'brecha', label: 'Brecha blue / oficial', section: 'Dólar', unit: '%' },
  { key: 'badlar', label: 'BADLAR bancos privados', section: 'Tasas y BCRA', unit: '%', bcra: 7 },
  { key: 'tamar', label: 'TAMAR bancos privados', section: 'Tasas y BCRA', unit: '%', bcra: 44 },
  { key: 'plazo_fijo', label: 'Plazo fijo 30 días', section: 'Tasas y BCRA', unit: '%', bcra: 12 },
  { key: 'reservas', label: 'Reservas internacionales', section: 'Tasas y BCRA', unit: 'USDM', bcra: 1 },
  { key: 'base_monetaria', label: 'Base monetaria', section: 'Tasas y BCRA', unit: 'ARSM', bcra: 15 },
  { key: 'riesgo_pais', label: 'Riesgo país', section: 'Tasas y BCRA', unit: 'pts' },
  { key: 'emae', label: 'EMAE desestacionalizado', section: 'Actividad', unit: 'index', series: '143.3_NO_PR_2004_A_31' },
  { key: 'desocupacion', label: 'Desocupación (EPH, trimestral)', section: 'Actividad', unit: '%', series: '42.3_EPH_PUNTUATAL_0_M_30', scale: 100 },
];
export const BCRA_IDS = CATALOGUE.filter((c) => c.bcra).map((c) => c.bcra);
export const SERIES_IDS = CATALOGUE.filter((c) => c.series).map((c) => c.series);
export const DEFAULT_LIMIT = 2;

export const bcraUrl = (c) => `${BCRA}/${c.bcra}?limit=${c.limit || DEFAULT_LIMIT}`;
export const seriesUrl = (c) => `${SERIES}?ids=${encodeURIComponent(c.series)}&last=2&format=json`;

const ymd = (s) => /^\d{4}-\d{2}-\d{2}$/.test(String(s));
const num = (v) => Number.isFinite(v);

// The BCRA's v4 answer for one variable as readings, newest first: [{fecha, valor}], [] for a
// body of another shape. A reading with a bad date or value is left out.
export function bcraReadings(body) {
  const r = body && Array.isArray(body.results) && body.results[0];
  const d = r && Array.isArray(r.detalle) ? r.detalle : [];
  return d.filter((x) => x && ymd(x.fecha) && num(x.valor)).map((x) => ({ fecha: x.fecha.slice(0, 10), valor: x.valor }));
}

// datos.gob.ar's answer as readings, oldest first: its rows of [date, value]; a null value or a
// bad row is left out.
export function seriesReadings(body) {
  const rows = body && Array.isArray(body.data) ? body.data : [];
  return rows.filter((x) => Array.isArray(x) && ymd(x[0]) && num(x[1])).map((x) => ({ fecha: x[0].slice(0, 10), valor: x[1] }));
}

// The raw data of the econ upstream: {bcra: {id: readings newest first}, series: {id: readings
// oldest first}, missing: [the ids that gave nothing]}. keep: the readings kept per id.
export function econData(bcra, series) {
  const out = { bcra: {}, series: {}, missing: [] };
  for (const c of CATALOGUE) {
    if (c.bcra) {
      const r = bcra[c.bcra] ? bcraReadings(bcra[c.bcra]).slice(0, c.limit || DEFAULT_LIMIT) : [];
      if (r.length) out.bcra[c.bcra] = r;
      else out.missing.push(String(c.bcra));
    } else if (c.series) {
      const r = series[c.series] ? seriesReadings(series[c.series]).slice(-2) : [];
      if (r.length) out.series[c.series] = r;
      else out.missing.push(c.series);
    }
  }
  return out;
}

// A number for the reply: up to 4 decimals, no trailing zeros, a dot decimal.
export function numText(v) {
  if (!num(v)) return '-';
  const s = (Math.round(v * 10000) / 10000).toFixed(4).replace(/\.?0+$/, '');
  return s === '-0' ? '0' : s;
}

// The indicators from the data of the econ upstream (d), the usd and rp results of get() and
// today's local date: [{key, label, section, unit, value, date, prev, prevDate, source}] in the
// catalogue's order, the ones with a value. A BCRA reading dated after today (the UVA, the CER and
// the ICL are published ahead) is skipped: the newest dated today or before counts.
export function econIndicators(d, usd, rp, today) {
  const out = [];
  const push = (c, source, cur, prev) => out.push({
    key: c.key, label: c.label, section: c.section, unit: c.unit, source,
    value: cur.valor * (c.scale || 1), date: cur.fecha,
    prev: prev ? prev.valor * (c.scale || 1) : null, prevDate: prev ? prev.fecha : '',
  });
  const quotes = usd && usd.data && Array.isArray(usd.data) ? usd.data : [];
  const quote = (casa) => quotes.find((q) => q && q.casa === casa && num(q.venta) && q.venta > 0);
  for (const c of CATALOGUE) {
    if (c.bcra) {
      const rs = (d && d.bcra && d.bcra[c.bcra]) || [];
      const i = rs.findIndex((x) => x.fecha <= today);
      if (i >= 0) push(c, 'BCRA', rs[i], rs[i + 1] || null);
    } else if (c.series) {
      const rs = (d && d.series && d.series[c.series]) || [];
      if (rs.length) push(c, 'INDEC', rs[rs.length - 1], rs.length > 1 ? rs[rs.length - 2] : null);
    } else if (c.key === 'blue' || c.key === 'brecha') {
      const b = quote('blue'), o = quote('oficial');
      const date = b && typeof b.fechaActualizacion === 'string' && ymd(b.fechaActualizacion.slice(0, 10)) ? b.fechaActualizacion.slice(0, 10) : today;
      if (c.key === 'blue' && b) push(c, 'DolarAPI', { fecha: date, valor: b.venta }, null);
      if (c.key === 'brecha' && b && o) push(c, 'DolarAPI', { fecha: date, valor: (b.venta / o.venta - 1) * 100 }, null);
    } else if (c.key === 'riesgo_pais') {
      const v = rp && rp.data;
      if (v && num(v.valor) && ymd(v.fecha)) push(c, 'ArgentinaDatos', { fecha: v.fecha, valor: v.valor }, null);
    }
  }
  return out;
}

// hist=inflacion|v1,v2,...: the monthly inflation of the last HIST_N months, oldest first, from
// the readings kept for the chart; null with fewer than 2.
export function histLine(d) {
  const rs = (d && d.bcra && d.bcra[27]) || [];
  const vals = rs.slice(0, HIST_N).map((x) => x.valor).reverse();
  return vals.length >= 2 ? `hist=inflacion|${vals.map(numText).join(',')}` : null;
}

// The reply of GET /economia: "couch-economia 1", now, an ind line per indicator, the hist line,
// econ_missing (the ids that gave nothing, when any), econ_age, econ_state, end. Lines come in
// the catalogue's order; at most ECON_BYTES, the last ind lines dropped before the tail when it
// does not fit (it does: 19 lines make about 1.6 KB).
export function economiaText({ now, tz, r, usd, rp }) {
  const today = localDate(now, tz);
  const st = r && r.state === 'ok' ? 'ok' : `${(r && r.state) || 'none'}|${clean(r && r.reason)}`;
  const head = [`couch-economia ${ECON_VERSION}`, `now=${now}`];
  const tail = [`econ_age=${Math.max(0, (r && r.age) | 0)}`, `econ_state=${st}`, 'end'];
  const d = r && r.data;
  const lines = econIndicators(d, usd, rp, today).map((i) =>
    `ind=${i.key}|${clean(i.label)}|${i.section}|${i.unit}|${numText(i.value)}|${i.date}|${i.prev === null ? '-' : numText(i.prev)}|${i.prevDate || '-'}|${i.source}`);
  const hist = histLine(d);
  if (hist) lines.push(hist);
  const missing = [...((d && Array.isArray(d.missing)) ? d.missing : []),
    ...(usd && usd.state !== 'ok' && !(usd.data) ? ['dolarapi'] : []), ...(rp && rp.state !== 'ok' && !(rp.data) ? ['riesgo-pais'] : [])];
  if (missing.length) lines.push(`econ_missing=${missing.map(clean).join(',')}`);
  const size = (ls) => ls.reduce((n, l) => n + ENC.encode(l).length + 1, 0);
  while (lines.length && size(head) + size(lines) + size(tail) > ECON_BYTES) lines.splice(lines.findLastIndex((l) => l.startsWith('ind=')), 1);
  return [...head, ...lines, ...tail, ''].join('\n');
}
