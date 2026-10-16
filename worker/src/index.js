// Couch's Board Worker: GET /board answers the plain-text reply of board.js with the dollar
// rates (dolarapi.com), the weather now (SMN's station, else Open-Meteo), the days, the rain and
// the sun (Open-Meteo), the money extras (argentinadatos.com) and the news (the RSS feeds of
// NEWS_FEEDS), each upstream kept in the Cache API with its time, so that a failed upstream gives
// the old values marked stale with their real age. Since 2026-10-13 also the keys of Couch 0.3.0:
// the place, the weather's codes and hours (the same Open-Meteo fetch), the changes against the
// previous closes and the Blue series (argentinadatos.com, no KV). Since 2026-10-14 the news
// reader: GET /news (30 headlines of the same feeds, from the same cache) and GET /article?id=
// (an item's text from the publisher's page, reader.js and article.js). Plain HTTP on purpose: the
// PS3 has no TLS (the zone's per-host rule, design section 4).

import { addDays, boardText, clean, fold, localDate, parseCasas } from './board.js';
import { findRow, latin1, unzipFirst } from './smn.js';
import { FEED_ITEMS, FEEDS_MAX, feedItems, parseFeeds, readFeedHead } from './news.js';
import { readTail, tailEntries } from './series.js';
import { articleText, capParas, NEWS_LIST, newsText } from './reader.js';
import { extractPage } from './article.js';

const TIMEOUT_MS = 4000;
export const ARTICLE_TIMEOUT_MS = 8000;  // a publisher's page: Lucas waits for it, the Board does not
export const ARTICLE_TTL = 3600;          // an article's paragraphs, cached 1 h

// The negative cache: a failed upstream is remembered in its cache entry (fail: {at, reason}), and
// while it is, a request answers at once with the old entry (stale) or none, with that reason.
// At most once per RETRY_S, the upstream is tried again after the reply (waitUntil), so a dead
// feed costs its timeout on its first failure only (and again once a day, when the Cache API drops
// the mark with the entry). 300 s: Couch asks every 5 minutes and USD_TTL is 300 s, so a dead
// upstream gets one try per Board fetch at most, and an upstream that came back shows on the next
// Board. A success removes the mark.
export const RETRY_S = 300;
const KEEP_S = 86400;                    // the Cache API keeps an entry (data or a failure) 1 day

const fetchOpts = () => ({ signal: AbortSignal.timeout(TIMEOUT_MS), headers: { 'user-agent': 'couch-board' } });

// A thrown fetch or body error in words (after the host).
function errWords(e, ms = TIMEOUT_MS) {
  return e && (e.name === 'TimeoutError' || e.name === 'AbortError') ? `no answer in ${ms / 1000} s`
    : String((e && e.message) || e).slice(0, 60);
}

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

// A feed: {data: its items} (news.js's feedItems: at most FEED_ITEMS, with the id, the link and
// the summary that /news and /article use), [] for text that is no RSS. Only the head of the body
// is read, for every feed (news.js's readFeedHead): up to the end of its 20th item, at most 96 KB,
// when those items are newest first, else up to its 100th item, at most 256 KB, to sort them.
// The rest is cancelled.
async function readFeed(r) {
  return { data: feedItems(await readFeedHead(r.body, FEED_ITEMS), FEED_ITEMS), bad: 'no items' };
}

const dated = (d) => !!d && typeof d === 'object' && Number.isFinite(d.valor) && /^\d{4}-\d{2}-\d{2}$/.test(String(d.fecha));
const AD = 'https://api.argentinadatos.com/v1/finanzas';
const ADC = 'https://api.argentinadatos.com/v1/cotizaciones/dolares';

export const CLOSES_TTL = 7 * 86400;    // a past close does not change (the key holds the date)
export const CLOSES_MISS_TTL = 900;     // a rate with no entry yet: all are fetched again after 15 min
export const SERIES_TTL = 21600;        // the Blue series and the riesgo pais list: 6 h

// A dated list of argentinadatos.com, read by its tail (series.js): {data: its newest `keep`
// entries of `field`}, the reason words for valid() when it is a bad read.
async function readSeries(r, field, keep) {
  const e = tailEntries(await readTail(r.body), field).slice(-keep);
  return { data: e, bad: e.length ? `a tail of ${e.length} entries to ${e.at(-1).fecha}` : 'no entries in the tail' };
}

// A series is a good read with at least `min` entries and its newest at most `days` days before
// now's local date (a stale list, or a tail of something else, keeps the old entry).
const series = (min, days) => (d, env, now) => Array.isArray(d) && d.length >= min &&
  d.at(-1).fecha >= addDays(localDate(now, env.TZ || 'UTC'), -days);

// The previous closes (board.js, usdChgLines): the per-day entries of each rate of CASAS dated
// today (local), which hold yesterday's closes, fetched in parallel and cached as one entry under
// the date: {day: yesterday, rates: {casa: sell}, missing: [casa]}. A 404 is a rate with no entry
// yet (the daily job runs late) or none at all; a rate missing is fetched again after
// CLOSES_MISS_TTL. Good with one rate at least; with none, the first rate's failure is the reason.
async function loadCloses(env, now, deps) {
  const stamp = localDate(now, env.TZ || 'UTC'), casas = parseCasas(env.CASAS).slice(0, 8);
  const got = await Promise.all(casas.map(async ({ casa }) => {
    try {
      const r = await deps.fetch(`${ADC}/${encodeURIComponent(casa)}/${stamp.replaceAll('-', '/')}`, fetchOpts());
      if (!r.ok) return { bad: `HTTP ${r.status}` };
      const { data } = await readJson(r);
      const ok = data && typeof data === 'object' && data.fecha === stamp && Number.isFinite(data.venta) && data.venta > 0;
      return ok ? { venta: data.venta } : { bad: 'bad JSON' };
    } catch (e) {
      return { bad: errWords(e) };
    }
  }));
  const rates = {}, missing = [];
  casas.forEach(({ casa }, i) => {
    if (got[i].bad) missing.push(casa);
    else rates[casa] = got[i].venta;
  });
  return { data: { day: addDays(stamp, -1), rates, missing }, bad: (got.find((g) => g.bad) || {}).bad || 'no rate in CASAS' };
}

// The national holidays (since 2026-10-16): argentinadatos.com's list of the local year (2 KB,
// {fecha, tipo, nombre}), and the next year's too when none of this year is left (from the last
// one, 25 December, to the year's end), cached as one entry under the year: {data: [{fecha,
// nombre}]} sorted, the valid entries only.
export const HOL_TTL = 86400;
const ADH = 'https://api.argentinadatos.com/v1/feriados';
async function loadHolidays(env, now, deps) {
  const today = localDate(now, env.TZ || 'UTC'), year = Number(today.slice(0, 4));
  const list = [];
  for (const y of [year, year + 1]) {
    const r = await deps.fetch(`${ADH}/${y}`, fetchOpts());
    if (!r.ok) {
      if (r.body) r.body.cancel().catch(() => {});
      return { data: null, bad: `HTTP ${r.status}` };
    }
    const { data } = await readJson(r);
    if (!Array.isArray(data)) return { data: null, bad: 'bad JSON' };
    for (const x of data) {
      if (x && typeof x === 'object' && /^\d{4}-\d{2}-\d{2}$/.test(String(x.fecha)) && clean(x.nombre)) {
        list.push({ fecha: String(x.fecha), nombre: clean(x.nombre).slice(0, 80) });
      }
    }
    if (list.some((h) => h.fecha >= today)) break;
  }
  list.sort((a, b) => (a.fecha < b.fecha ? -1 : a.fecha > b.fecha ? 1 : 0));
  return { data: list, bad: 'no holiday' };
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
      current: 'temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day,' +
        'cloud_cover,precipitation,wind_direction_10m,wind_gusts_10m',
      daily: 'weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset',
      hourly: 'precipitation_probability,precipitation,temperature_2m,weather_code',
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
  // Since 2026-10-13 (design section 5): the previous closes, the Blue series, the riesgo pais list.
  // The key holds today's local date and CASAS's rates.
  closes: {
    host: 'argentinadatos.com',
    key: (env, now) => `https://couch.internal/closes?d=${localDate(now, env.TZ || 'UTC')}&c=${encodeURIComponent(parseCasas(env.CASAS).slice(0, 8).map((x) => x.casa).join(','))}`,
    ttl: (env, d) => (d && Array.isArray(d.missing) && d.missing.length ? CLOSES_MISS_TTL : CLOSES_TTL),
    load: loadCloses,
    valid: (d) => !!d && !!d.rates && Object.keys(d.rates).length > 0,
  },
  // The 31 newest days of the Blue dollar (523 KB of JSON, read by its tail).
  blue30: {
    host: 'argentinadatos.com',
    url: () => `${ADC}/blue`,
    key: () => 'https://couch.internal/blue30',
    ttl: () => SERIES_TTL,
    read: (r) => readSeries(r, 'venta', 31),
    valid: series(25, 3),
  },
  hol: {
    host: 'argentinadatos.com',
    key: (env, now) => `https://couch.internal/hol?y=${localDate(now, env.TZ || 'UTC').slice(0, 4)}`,
    ttl: () => HOL_TTL,
    load: loadHolidays,
    valid: (d) => Array.isArray(d) && d.length > 0,
  },
  // The 10 newest entries of riesgo pais (413 KB, no weekend or holiday entries: a long weekend
  // leaves 4 days, so a newest entry up to 7 days old is a good read).
  rplist: {
    host: 'argentinadatos.com',
    url: () => `${AD}/indices/riesgo-pais`,
    key: () => 'https://couch.internal/rplist',
    ttl: () => SERIES_TTL,
    read: (r) => readSeries(r, 'valor', 10),
    valid: series(2, 7),
  },
};

// One feed of NEWS_FEEDS as an upstream: its own cache entry under its URL, the parsed items,
// shared by /board, /news and /article. "news2" since 2026-10-14: the items gained the id, the
// link and the summary, so an entry of before (titles and dates only) is never read.
export function newsUpstream(feed) {
  return {
    host: new URL(feed.url).hostname.replace(/^www\./, ''),
    url: () => feed.url,
    key: () => `https://couch.internal/news2?u=${encodeURIComponent(feed.url)}`,
    ttl: (env) => Number(env.NEWS_TTL) || 600,
    read: readFeed,
    valid: (d) => Array.isArray(d) && d.length > 0,
  };
}

// A feed item's article as an upstream (/article): its own cache entry under the item's link
// for ARTICLE_TTL, {paras, cut} (reader.js's capParas of article.js's paragraphs). Only a link
// of a cached feed item comes here (handle's /article), so the Worker is no open proxy. A failure
// is remembered as any upstream's (the negative cache): /article then answers the summary at
// once, and tries the page again after the reply at most once per RETRY_S.
export function articleUpstream(item) {
  return {
    host: new URL(item.link).hostname.replace(/^www\./, ''),
    timeout: ARTICLE_TIMEOUT_MS,
    key: () => `https://couch.internal/article?u=${encodeURIComponent(item.link)}`,
    ttl: (env) => Number(env.ARTICLE_TTL) || ARTICLE_TTL,
    load: async (env, now, deps) => {
      const r = await deps.fetch(item.link, { signal: AbortSignal.timeout(ARTICLE_TIMEOUT_MS), headers: { 'user-agent': 'couch-board' } });
      if (!r.ok) {
        if (r.body) r.body.cancel().catch(() => {});
        return { data: null, bad: `HTTP ${r.status}` };
      }
      const page = await (deps.extract || extractPage)(r, r.url || item.link);
      return { data: capParas(page.paras), bad: 'no paragraphs' };
    },
    valid: (d) => !!d && Array.isArray(d.paras) && d.paras.length > 0,
  };
}

// One try of an upstream: {data, bad} as its read (or load, for an upstream that makes its own
// fetches) gives them; a fetch or body error throws.
async function attempt(u, env, now, deps) {
  if (u.load) return u.load(env, now, deps);
  const r = await deps.fetch(u.url(env), fetchOpts());
  if (!r.ok) return { data: null, bad: `HTTP ${r.status}` };
  return u.read(r, env);
}

// One try and its cache write: {at: now, data} for a good answer, else the old entry's data with
// fail: {at: now, reason}. Gives {ok, data} or {ok: false, reason}. bg: the try runs after the
// reply, so it waits for its own write. Never throws.
async function refresh(u, env, now, deps, key, entry, bg) {
  let reason, body;
  try {
    const { data, bad } = await attempt(u, env, now, deps);
    if (u.valid(data, env, now)) body = { at: now, data };
    else reason = `${u.host}: ${bad || 'bad JSON'}`;
  } catch (e) {
    reason = `${u.host}: ${errWords(e, u.timeout)}`;
  }
  if (!body) body = { at: entry ? entry.at : null, data: entry ? entry.data : null, fail: { at: now, reason } };
  const put = Promise.resolve().then(() => deps.cache.put(key, new Response(JSON.stringify(body), {
    headers: { 'content-type': 'application/json', 'cache-control': `max-age=${KEEP_S}` },
  }))).catch(() => {});
  if (bg) await put;
  else deps.waitUntil(put);
  return body.fail ? { ok: false, reason } : { ok: true, data: body.data };
}

// One upstream (a name of UPSTREAMS, or an upstream such as newsUpstream's): a fresh cache entry,
// else the upstream (cached when valid), else the old entry marked stale, else none. An upstream
// that failed before is not waited for (RETRY_S above): the old entry or none at once, with the
// failure's reason, and at most once per RETRY_S a try after the reply. deps: {fetch, cache,
// waitUntil}. Never throws.
export async function get(name, env, now, deps) {
  const u = typeof name === 'string' ? UPSTREAMS[name] : name;
  let key, entry = null;
  try {
    key = u.key(env, now);
  } catch (e) {
    return { state: 'none', reason: `${u.host}: ${errWords(e)}`, age: 0, data: null };   // a bad TZ
  }
  try {
    const hit = await deps.cache.match(key);
    if (hit) entry = await hit.json();
  } catch {
    entry = null;
  }
  if (!entry || typeof entry !== 'object') entry = null;
  const has = !!entry && entry.data !== null && entry.data !== undefined && Number.isFinite(entry.at);
  if (has && now - entry.at < u.ttl(env, entry.data)) return { state: 'ok', age: now - entry.at, data: entry.data };
  const old = (reason) => (has ? { state: 'stale', reason, age: now - entry.at, data: entry.data } : { state: 'none', reason, age: 0, data: null });
  const fail = entry && entry.fail && Number.isFinite(entry.fail.at) ? entry.fail : null;
  if (fail) {
    if (now - fail.at >= RETRY_S || now < fail.at) deps.waitUntil(refresh(u, env, now, deps, key, has ? entry : null, true));
    return old(clean(fail.reason) || `${u.host}: failed`);
  }
  const r = await refresh(u, env, now, deps, key, has ? entry : null, false);
  return r.ok ? { state: 'ok', age: 0, data: r.data } : old(r.reason);
}

const textReply = (text) => new Response(text, { headers: { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'no-store' } });

// GET /news: the list (reader.js's newsText) of every feed of NEWS_FEEDS, from the cache that
// /board fills (or a fetch, as /board's). Always 200, the states in the body.
async function newsReply(env, now, deps) {
  const feeds = parseFeeds(env.NEWS_FEEDS);
  try {
    const rs = await Promise.all(feeds.map((f) => get(newsUpstream(f), env, now, deps)));
    return newsText({ now, feeds: feeds.map((f, i) => ({ ...f, r: rs[i] })), max: Number(env.NEWS_LIST) || NEWS_LIST });
  } catch (e) {
    return [`couch-news 1`, `now=${now}`, 'news_age=0', `news_state=none|worker: ${clean((e && e.message) || e).slice(0, 60)}`, 'end', ''].join('\n');
  }
}

// GET /article?id=: the item of that id in the cached feeds of NEWS_FEEDS (none: state none,
// and no fetch, so only a link that a configured feed gave is ever fetched), then its page's
// paragraphs (articleUpstream), else its summary. Always 200, the state in the body.
async function articleReply(env, now, deps, id) {
  const tz = env.TZ || 'UTC';
  try {
    if (!/^[0-9a-f]{8}$/.test(id)) return articleText({ now, tz, a: { state: 'none', reason: 'bad id' } });
    const feeds = parseFeeds(env.NEWS_FEEDS);
    const rs = await Promise.all(feeds.map((f) => get(newsUpstream(f), env, now, deps)));
    let item = null, feed = null;
    feeds.forEach((f, i) => {
      const x = !item && Array.isArray(rs[i].data) ? rs[i].data.find((d) => d && d.id === id) : null;
      if (x) [item, feed] = [x, f];
    });
    if (!item) return articleText({ now, tz, a: { state: 'none', reason: 'unknown id' } });
    const summary = { state: 'summary', paras: item.summary ? [item.summary] : [] };
    let link = null;
    try {
      const u = new URL(item.link);
      if (u.protocol === 'https:' || u.protocol === 'http:') link = u.href;
    } catch { /* no link */ }
    if (!link) return articleText({ now, tz, item, feed, a: { ...summary, reason: 'no link' } });
    const r = await get(articleUpstream({ ...item, link }), env, now, deps);
    // An old entry (stale) still has the page's text: only no text gives the summary.
    const a = r.data && Array.isArray(r.data.paras) && r.data.paras.length
      ? { state: r.data.cut ? 'cut' : 'ok', paras: r.data.paras } : { ...summary, reason: r.reason };
    return articleText({ now, tz, item, feed, a });
  } catch (e) {
    return articleText({ now, tz, a: { state: 'none', reason: `worker: ${clean((e && e.message) || e).slice(0, 60)}` } });
  }
}

// The request with its dependencies, for the tests; the default export wires the runtime's.
export async function handle(request, env, deps) {
  const url = new URL(request.url);
  const route = request.method === 'GET' ? url.pathname : '';
  if (route === '/news') return textReply(await newsReply(env, deps.now(), deps));
  if (route === '/article') return textReply(await articleReply(env, deps.now(), deps, url.searchParams.get('id') || ''));
  if (route !== '/board') {
    return new Response('not found\n', { status: 404, headers: { 'content-type': 'text/plain; charset=utf-8' } });
  }
  const now = deps.now();
  // No SMN_STATION: no SMN fetch, the now line from Open-Meteo. No PF_BANK: no fixed-term line.
  // No NEWS_FEEDS: no news lines. FEEDS_MAX feeds: no holidays (their fetch and two cache calls
  // would pass the free plan's 50 subrequests). Every upstream in parallel.
  const smnOff = { state: 'none', reason: 'no SMN_STATION', age: 0, data: null };
  const feeds = parseFeeds(env.NEWS_FEEDS);
  const pfOn = clean(env.PF_BANK) !== '';
  // The changes: the closes when CASAS has a rate, the Blue series when it has blue.
  const casas = parseCasas(env.CASAS).slice(0, 8);
  const [usd, wx, smn, rp, ipc, pf, closes, blue, rpList, hol, ...rs] = await Promise.all([get('usd', env, now, deps), get('wx', env, now, deps),
    String(env.SMN_STATION ?? '').trim() ? get('smn', env, now, deps) : smnOff, get('rp', env, now, deps),
    get('ipc', env, now, deps), pfOn ? get('pf', env, now, deps) : null,
    casas.length ? get('closes', env, now, deps) : null, casas.some((x) => x.casa === 'blue') ? get('blue30', env, now, deps) : null,
    get('rplist', env, now, deps), feeds.length < FEEDS_MAX ? get('hol', env, now, deps) : null,
    ...feeds.map((f) => get(newsUpstream(f), env, now, deps))]);
  const money = { rp, ipc, pf: pf || undefined };
  const chg = { closes: closes || undefined, blue: blue || undefined, rpList };
  const news = feeds.map((f, i) => ({ label: f.label, weight: f.weight, r: rs[i] }));
  // Always 200 with the states in the body (design section 4): a reply that cannot be formatted
  // gives the upstreams as none with the error, and a bad setting (TZ) the bare lines.
  let text;
  try {
    text = boardText({ now, usd, wx, smn, money, news, chg, hol: hol || undefined, env });
  } catch (e) {
    const reason = `worker: ${clean((e && e.message) || e).slice(0, 60)}`;
    const none = { state: 'none', reason, age: 0, data: null };
    try {
      text = boardText({ now, usd: none, wx: none, smn: none, news: feeds.map((f) => ({ label: f.label, weight: f.weight, r: none })), env });
    } catch {
      text = ['couch-board 1', `now=${now}`, `usd_state=none|${reason}`, `wx_state=none|${reason}`,
        ...(feeds.length ? [`news_state=none|${reason}`] : []), 'end', ''].join('\n');
    }
  }
  return textReply(text);
}

export default {
  async fetch(request, env, ctx) {
    return handle(request, env, {
      fetch: (u, o) => fetch(u, o),
      cache: caches.default,
      waitUntil: (p) => ctx.waitUntil(p),
      now: () => Math.floor(Date.now() / 1000),
      extract: extractPage,
    });
  },
};
