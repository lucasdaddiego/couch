// Couch's Board Worker: GET /board answers the plain-text reply of board.js with the dollar
// rates (dolarapi.com) and the weather (Open-Meteo), each kept in the Cache API with its time,
// so that a failed upstream gives the old values marked stale with their real age. Plain HTTP
// on purpose: the PS3 has no TLS (the zone's per-host rule, design section 4).

import { boardText } from './board.js';

const TIMEOUT_MS = 4000;

export const UPSTREAMS = {
  usd: {
    host: 'dolarapi.com',
    url: () => 'https://dolarapi.com/v1/dolares',
    key: () => 'https://couch.internal/usd',
    ttl: (env) => Number(env.USD_TTL) || 300,
    valid: (d) => Array.isArray(d) && d.length > 0,
  },
  wx: {
    host: 'open-meteo',
    url: (env) => 'https://api.open-meteo.com/v1/forecast?' + new URLSearchParams({
      latitude: env.LAT, longitude: env.LON, timezone: env.TZ, forecast_days: '3',
      current: 'temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day',
      daily: 'weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max',
    }),
    key: (env) => `https://couch.internal/wx?lat=${env.LAT}&lon=${env.LON}`,
    ttl: (env) => Number(env.WX_TTL) || 900,
    valid: (d) => !!(d && d.current && d.daily),
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
      let data = null;
      try {
        data = await r.json();
      } catch {
        data = null;
      }
      if (u.valid(data)) {
        const body = JSON.stringify({ at: now, data });
        deps.waitUntil(deps.cache.put(key, new Response(body, {
          headers: { 'content-type': 'application/json', 'cache-control': 'max-age=86400' },
        })));
        return { state: 'ok', age: 0, data };
      }
      reason = `${u.host}: bad JSON`;
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
  const [usd, wx] = await Promise.all([get('usd', env, now, deps), get('wx', env, now, deps)]);
  return new Response(boardText({ now, usd, wx, env }), {
    headers: { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'no-store' },
  });
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
