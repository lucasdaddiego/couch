// Couch's Board Worker: GET /board answers the plain-text reply of board.js with the dollar
// rates (dolarapi.com), the weather now (SMN's station, else Open-Meteo) and the days
// (Open-Meteo), each upstream kept in the Cache API with its time, so that a failed upstream
// gives the old values marked stale with their real age. Plain HTTP on purpose: the PS3 has no
// TLS (the zone's per-host rule, design section 4).

import { boardText, clean, fold } from './board.js';
import { findRow, latin1, unzipFirst } from './smn.js';

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
      daily: 'weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max',
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
};

// One upstream: a fresh cache entry, else the upstream (cached when valid), else the old entry
// marked stale, else none. deps: {fetch, cache, waitUntil}. Never throws.
export async function get(name, env, now, deps) {
  const u = UPSTREAMS[name];
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
  // No SMN_STATION: no SMN fetch, the now line from Open-Meteo.
  const smnOff = { state: 'none', reason: 'no SMN_STATION', age: 0, data: null };
  const [usd, wx, smn] = await Promise.all([get('usd', env, now, deps), get('wx', env, now, deps),
    String(env.SMN_STATION ?? '').trim() ? get('smn', env, now, deps) : smnOff]);
  // Always 200 with the states in the body (design section 4): a reply that cannot be formatted
  // gives the upstreams as none with the error, and a bad setting (TZ) the bare lines.
  let text;
  try {
    text = boardText({ now, usd, wx, smn, env });
  } catch (e) {
    const reason = `worker: ${clean((e && e.message) || e).slice(0, 60)}`;
    const none = { state: 'none', reason, age: 0, data: null };
    try {
      text = boardText({ now, usd: none, wx: none, smn: none, env });
    } catch {
      text = ['couch-board 1', `now=${now}`, `usd_state=none|${reason}`, `wx_state=none|${reason}`, 'end', ''].join('\n');
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
