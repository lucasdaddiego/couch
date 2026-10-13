// Couch's Board Worker: GET /board answers the plain-text reply of board.js with the dollar
// rates (dolarapi.com), the weather now (SMN's station, else Open-Meteo), the days, the rain and
// the sun (Open-Meteo), the money extras (argentinadatos.com) and the news (the RSS feeds of
// NEWS_FEEDS), each upstream kept in the Cache API with its time, so that a failed upstream gives
// the old values marked stale with their real age. Plain HTTP on purpose: the PS3 has no TLS (the
// zone's per-host rule, design section 4).

import { boardText, clean, fold } from './board.js';
import { findRow, latin1, unzipFirst } from './smn.js';
import { parseFeed, parseFeeds, readHead } from './news.js';

const TIMEOUT_MS = 4000;

// A JSON body: {data}, null for a body that is not JSON. A body that stalls or breaks throws, and
// get()'s catch names it.
async function readJson(r) {
  try {
    return { data: await r.json() };
  } catch (e) {
    if (!e || e.name !== 'SyntaxError') throw e;
    return { data: null };
  }
}

// SMN's zip: {data: the station's row} (smn.js), else null with the words for the reason. Only
// the body read can throw (a stall or a break), as in readJson.
async function readSmn(r, env) {
  const bytes = new Uint8Array(await r.arrayBuffer());
  let text;
  try {
    text = latin1((await unzipFirst(bytes)).bytes);
  } catch {
    return { data: null, bad: 'bad zip' };
  }
  return { data: findRow(text, env.SMN_STATION), bad: `no row for ${clean(env.SMN_STATION)}` };
}

// argentinadatos.com's inflation list (53 KB, monthly since 1943): {data: its last entry}, the
// latest fecha, so the cache holds one entry.
async function readLast(r) {
  const { data } = await readJson(r);
  if (!Array.isArray(data)) return { data: null };
  let last = null;
  for (const x of data) if (dated(x) && (!last || x.fecha > last.fecha)) last = x;
  return { data: last && { fecha: last.fecha, valor: last.valor }, bad: 'no entry' };
}

// argentinadatos.com's fixed-term table (20 KB, one row per bank, no date): {data: {entidad, tna}}
// of the bank named PF_BANK after a trim, case and accents not counted, tna its rate for clients.
async function readBank(r, env) {
  const { data } = await readJson(r);
  if (!Array.isArray(data)) return { data: null };
  const want = fold(clean(env.PF_BANK));
  const row = data.find((x) => x && typeof x === 'object' && fold(clean(x.entidad)) === want);
  return { data: row ? { entidad: clean(row.entidad), tna: row.tnaClientes } : null, bad: `no row for ${clean(env.PF_BANK)}` };
}

// A feed: {data: its items} (news.js), [] for text that is no RSS. Only the head of the body is
// read, for every feed: up to the end of its 8th item, at most 96 KB (news.js's readHead), and
// the rest is cancelled.
async function readFeed(r) {
  return { data: parseFeed(await readHead(r.body)), bad: 'no items' };
}

const dated = (d) => !!d && typeof d === 'object' && Number.isFinite(d.valor) && /^\d{4}-\d{2}-\d{2}$/.test(String(d.fecha));
const AD = 'https://api.argentinadatos.com/v1/finanzas';

export const UPSTREAMS = {
  usd: {
    host: 'dolarapi.com',
    url: () => 'https://dolarapi.com/v1/dolares',
    key: () => 'https://couch.internal/usd',
    ttl: (env) => Number(env.USD_TTL) || 300,
    read: readJson,
    valid: (d) => Array.isArray(d) && d.length > 0 && d.every((x) => x !== null && typeof x === 'object'),
  },
  wx: {
    host: 'open-meteo',
    url: (env) => 'https://api.open-meteo.com/v1/forecast?' + new URLSearchParams({
      latitude: env.LAT, longitude: env.LON, timezone: env.TZ, forecast_days: '4',
      current: 'temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day',
      daily: 'weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset',
      hourly: 'precipitation_probability,precipitation',
    }),
    key: (env) => `https://couch.internal/wx?lat=${env.LAT}&lon=${env.LON}`,
    ttl: (env) => Number(env.WX_TTL) || 900,
    read: readJson,
    valid: (d) => !!(d && d.current && d.daily),
  },
  // The parsed row is cached, not the zip; the key holds the folded station, so a new
  // SMN_STATION does not get the old station's row.
  smn: {
    host: 'ssl.smn.gob.ar',
    url: () => 'https://ssl.smn.gob.ar/dpd/zipopendata.php?dato=tiepre',
    key: (env) => `https://couch.internal/smn?station=${encodeURIComponent(fold(String(env.SMN_STATION ?? '').trim()))}`,
    ttl: (env) => Number(env.SMN_TTL) || 600,
    read: readSmn,
    valid: (d) => !!d && typeof d === 'object' && typeof d.station === 'string',
  },
  rp: {
    host: 'argentinadatos.com',
    url: () => `${AD}/indices/riesgo-pais/ultimo`,
    key: () => 'https://couch.internal/rp',
    ttl: (env) => Number(env.RP_TTL) || 900,
    read: readJson,
    valid: dated,
  },
  ipc: {
    host: 'argentinadatos.com',
    url: () => `${AD}/indices/inflacion`,
    key: () => 'https://couch.internal/ipc',
    ttl: (env) => Number(env.IPC_TTL) || 21600,
    read: readLast,
    valid: dated,
  },
  // The key holds the folded bank, as SMN's holds the station.
  pf: {
    host: 'argentinadatos.com',
    url: () => `${AD}/tasas/plazoFijo`,
    key: (env) => `https://couch.internal/pf?bank=${encodeURIComponent(fold(clean(env.PF_BANK)))}`,
    ttl: (env) => Number(env.PF_TTL) || 3600,
    read: readBank,
    valid: (d) => !!d && Number.isFinite(d.tna) && d.tna > 0,
  },
};

// One feed of NEWS_FEEDS as an upstream: its own cache entry under its URL, the parsed items.
export function newsUpstream(feed) {
  return {
    host: new URL(feed.url).hostname.replace(/^www\./, ''),
    url: () => feed.url,
    key: () => `https://couch.internal/news?u=${encodeURIComponent(feed.url)}`,
    ttl: (env) => Number(env.NEWS_TTL) || 600,
    read: readFeed,
    valid: (d) => Array.isArray(d) && d.length > 0,
  };
}

// One upstream (a name of UPSTREAMS, or an upstream such as newsUpstream's): a fresh cache entry,
// else the upstream (cached when valid), else the old entry marked stale, else none. deps:
// {fetch, cache, waitUntil}. Never throws.
export async function get(name, env, now, deps) {
  const u = typeof name === 'string' ? UPSTREAMS[name] : name;
  const key = u.key(env);
  let entry = null;
  try {
    const hit = await deps.cache.match(key);
    if (hit) entry = await hit.json();
  } catch {
    entry = null;
  }
  if (entry && now - entry.at < u.ttl(env)) return { state: 'ok', age: now - entry.at, data: entry.data };
  let reason;
  try {
    const r = await deps.fetch(u.url(env), { signal: AbortSignal.timeout(TIMEOUT_MS), headers: { 'user-agent': 'couch-board' } });
    if (!r.ok) reason = `${u.host}: HTTP ${r.status}`;
    else {
      const { data, bad } = await u.read(r, env);
      if (u.valid(data)) {
        const body = JSON.stringify({ at: now, data });
        deps.waitUntil(deps.cache.put(key, new Response(body, {
          headers: { 'content-type': 'application/json', 'cache-control': 'max-age=86400' },
        })));
        return { state: 'ok', age: 0, data };
      }
      reason = `${u.host}: ${bad || 'bad JSON'}`;
    }
  } catch (e) {
    reason = e && (e.name === 'TimeoutError' || e.name === 'AbortError') ? `${u.host}: no answer in ${TIMEOUT_MS / 1000} s`
      : `${u.host}: ${String((e && e.message) || e).slice(0, 60)}`;
  }
  if (entry) return { state: 'stale', reason, age: now - entry.at, data: entry.data };
  return { state: 'none', reason, age: 0, data: null };
}

// The request with its dependencies, for the tests; the default export wires the runtime's.
export async function handle(request, env, deps) {
  const url = new URL(request.url);
  if (request.method !== 'GET' || url.pathname !== '/board') {
    return new Response('not found\n', { status: 404, headers: { 'content-type': 'text/plain; charset=utf-8' } });
  }
  const now = deps.now();
  // No SMN_STATION: no SMN fetch, the now line from Open-Meteo. No PF_BANK: no fixed-term line.
  // No NEWS_FEEDS: no news lines. Every upstream in parallel.
  const smnOff = { state: 'none', reason: 'no SMN_STATION', age: 0, data: null };
  const feeds = parseFeeds(env.NEWS_FEEDS);
  const pfOn = clean(env.PF_BANK) !== '';
  const [usd, wx, smn, rp, ipc, pf, ...rs] = await Promise.all([get('usd', env, now, deps), get('wx', env, now, deps),
    String(env.SMN_STATION ?? '').trim() ? get('smn', env, now, deps) : smnOff, get('rp', env, now, deps),
    get('ipc', env, now, deps), pfOn ? get('pf', env, now, deps) : null, ...feeds.map((f) => get(newsUpstream(f), env, now, deps))]);
  const money = { rp, ipc, pf: pf || undefined };
  const news = feeds.map((f, i) => ({ label: f.label, r: rs[i] }));
  // Always 200 with the states in the body (design section 4): a reply that cannot be formatted
  // gives the upstreams as none with the error, and a bad setting (TZ) the bare lines.
  let text;
  try {
    text = boardText({ now, usd, wx, smn, money, news, env });
  } catch (e) {
    const reason = `worker: ${clean((e && e.message) || e).slice(0, 60)}`;
    const none = { state: 'none', reason, age: 0, data: null };
    try {
      text = boardText({ now, usd: none, wx: none, smn: none, news: feeds.map((f) => ({ label: f.label, r: none })), env });
    } catch {
      text = ['couch-board 1', `now=${now}`, `usd_state=none|${reason}`, `wx_state=none|${reason}`,
        ...(feeds.length ? [`news_state=none|${reason}`] : []), 'end', ''].join('\n');
    }
  }
  return new Response(text, { headers: { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'no-store' } });
}

export default {
  async fetch(request, env, ctx) {
    return handle(request, env, {
      fetch: (u, o) => fetch(u, o),
      cache: caches.default,
      waitUntil: (p) => ctx.waitUntil(p),
      now: () => Math.floor(Date.now() / 1000),
    });
  },
};
