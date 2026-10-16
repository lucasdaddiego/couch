// Couch's Board Worker (a template: wrangler.jsonc holds the place, the rates, the holidays'
// country and the feeds). GET /board answers the plain-text reply of board.js with the weather
// (Open-Meteo), the rates (Frankfurter: the ECB's reference rates), the next public holiday
// (Nager.Date) and the news (the RSS feeds of NEWS_FEEDS), each upstream kept in the Cache API with
// its time, so that a failed upstream gives the old values marked stale with their real age. The
// news reader: GET /news (the headlines of the same feeds, from the same cache) and GET
// /article?id= (an item's text from the publisher's page, reader.js and article.js). Since Couch
// 0.5.0 the air (Open-Meteo's air quality) and GET /futbol, the reply of the Fútbol page
// (futbol.js), which this template answers with no source. Plain HTTP on purpose: the PS3 has no
// usable TLS (README.md, "The Worker").

import { addDays, boardText, clean, localDate, parseRates } from './board.js';
import { FEED_ITEMS, feedItems, parseFeeds, readFeedHead } from './news.js';
import { articleText, capParas, NEWS_LIST, newsText } from './reader.js';
import { extractPage } from './article.js';
import { futbolText } from './futbol.js';

const TIMEOUT_MS = 4000;
export const ARTICLE_TIMEOUT_MS = 8000;  // a publisher's page: the viewer waits for it, the Board does not
export const ARTICLE_TTL = 3600;          // an article's paragraphs, cached 1 h

// The negative cache: a failed upstream is remembered in its cache entry (fail: {at, reason}), and
// while it is, a request answers at once with the old entry (stale) or none, with that reason.
// At most once per RETRY_S, the upstream is tried again after the reply (waitUntil), so a dead
// feed costs its timeout on its first failure only (and again once a day, when the Cache API drops
// the mark with the entry). 300 s: Couch asks every 5 minutes, so a dead
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

// A feed: {data: its items} (news.js's feedItems: at most FEED_ITEMS, with the id, the link and
// the summary that /news and /article use), [] for text that is no RSS. Only the head of the body
// is read, for every feed (news.js's readFeedHead): up to the end of its 20th item, at most 96 KB,
// when those items are newest first, else up to its 100th item, at most 256 KB, to sort them.
// The rest is cancelled.
async function readFeed(r) {
  return { data: feedItems(await readFeedHead(r.body, FEED_ITEMS), FEED_ITEMS), bad: 'no items' };
}

// Frankfurter's time series (https://frankfurter.dev): {data: {dates, rates: {code: [value]}}},
// the dates oldest first and each value CURRENCY per one unit of the code (Frankfurter gives the
// codes per one CURRENCY, so each is inverted).
async function readFx(r) {
  const { data } = await readJson(r);
  const rt = data && data.rates;
  if (!rt || typeof rt !== 'object') return { data: null };
  const dates = Object.keys(rt).filter((d) => /^\d{4}-\d{2}-\d{2}$/.test(d)).sort();
  const codes = new Set(dates.flatMap((d) => Object.keys(rt[d] || {})));
  const rates = {};
  for (const c of codes) rates[c] = dates.map((d) => (Number.isFinite(rt[d][c]) && rt[d][c] > 0 ? 1 / rt[d][c] : null));
  return { data: { dates, rates }, bad: 'no dates' };
}

// The rates' window: FX_DAYS calendar days, so 30 days of chart and the day before them.
export const FX_DAYS = 32;
export const FX_TTL = 3600;              // the ECB publishes once a working day, about 16:00 CET
const home = (env) => (/^[A-Za-z]{3}$/.test(String(env.CURRENCY || '').trim()) ? String(env.CURRENCY).trim().toUpperCase() : 'EUR');
const fxCodes = (env) => parseRates(env.RATES).map((r) => r.code).filter((c) => c !== home(env));

// The public holidays (Nager.Date, https://date.nager.at): COUNTRY's list of the local year, and
// the next year's too when none of this year is left. Kept: the nationwide ones (global) of type
// Public; the name in the local language (localName), or in English with HOLIDAY_NAMES=english.
// Cached as one entry under the year and the country: {data: [{fecha, nombre}]} sorted.
export const HOL_TTL = 86400;
const country = (env) => (/^[A-Za-z]{2}$/.test(String(env.COUNTRY || '').trim()) ? String(env.COUNTRY).trim().toUpperCase() : '');
async function loadHolidays(env, now, deps) {
  const today = localDate(now, env.TZ || 'UTC'), year = Number(today.slice(0, 4)), english = String(env.HOLIDAY_NAMES || '').trim() === 'english';
  const list = [];
  for (const y of [year, year + 1]) {
    const r = await deps.fetch(`https://date.nager.at/api/v3/PublicHolidays/${y}/${country(env)}`, fetchOpts());
    if (!r.ok) {
      if (r.body) r.body.cancel().catch(() => {});
      return list.length ? { data: list, bad: 'no holiday' } : { data: null, bad: `HTTP ${r.status}` };
    }
    const { data } = await readJson(r);
    if (!Array.isArray(data)) return { data: null, bad: 'bad JSON' };
    for (const x of data) {
      if (!x || typeof x !== 'object' || x.global !== true || !Array.isArray(x.types) || !x.types.includes('Public')) continue;
      const name = clean(english ? x.name : x.localName || x.name);
      if (/^\d{4}-\d{2}-\d{2}$/.test(String(x.date)) && name) list.push({ fecha: String(x.date), nombre: Array.from(name).slice(0, 80).join('') });
    }
    if (list.some((h) => h.fecha >= today)) break;
  }
  list.sort((a, b) => (a.fecha < b.fecha ? -1 : a.fecha > b.fecha ? 1 : 0));
  return { data: list, bad: 'no holiday' };
}

export const UPSTREAMS = {
  wx: {
    host: 'open-meteo',
    url: (env) => 'https://api.open-meteo.com/v1/forecast?' + new URLSearchParams({
      latitude: env.LAT, longitude: env.LON, timezone: env.TZ, forecast_days: '4',
      current: 'temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day,' +
        'cloud_cover,precipitation,wind_direction_10m,wind_gusts_10m',
      daily: 'weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset',
      hourly: 'precipitation_probability,precipitation,temperature_2m,weather_code',
      // since Couch 0.5.0: 24 steps of 15 minutes from now's, so a forecast cached for WX_TTL, or
      // stale for 3 h, still covers the next 2 h (board.js's soonWords)
      minutely_15: 'precipitation', forecast_minutely_15: '24',
    }),
    key: (env) => `https://couch.internal/wx?lat=${env.LAT}&lon=${env.LON}`,
    ttl: (env) => Number(env.WX_TTL) || 900,
    read: readJson,
    valid: (d) => !!(d && d.current && d.daily),
  },
  // Since Couch 0.5.0: the air now (board.js's airLine), Open-Meteo's air quality at the same place.
  air: {
    host: 'air-quality-api.open-meteo.com',
    url: (env) => 'https://air-quality-api.open-meteo.com/v1/air-quality?' + new URLSearchParams({
      latitude: env.LAT, longitude: env.LON, timezone: env.TZ, current: 'us_aqi,uv_index',
    }),
    key: (env) => `https://couch.internal/air?lat=${env.LAT}&lon=${env.LON}`,
    ttl: (env) => Number(env.AIR_TTL) || Number(env.WX_TTL) || 900,
    read: readJson,
    valid: (d) => !!(d && d.current),
  },
  // The key holds today's local date, CURRENCY and the codes: a new day or a new RATES fetches.
  fx: {
    host: 'frankfurter.dev',
    url: (env, now) => `https://api.frankfurter.dev/v1/${addDays(localDate(now, env.TZ || 'UTC'), -FX_DAYS)}..?` +
      new URLSearchParams({ base: home(env), symbols: fxCodes(env).join(',') }),
    key: (env, now) => `https://couch.internal/fx?d=${localDate(now, env.TZ || 'UTC')}&b=${home(env)}&s=${fxCodes(env).join(',')}`,
    ttl: (env) => Number(env.FX_TTL) || FX_TTL,
    read: readFx,
    valid: (d) => !!d && Array.isArray(d.dates) && d.dates.length > 0,
  },
  hol: {
    host: 'date.nager.at',
    key: (env, now) => `https://couch.internal/hol?y=${localDate(now, env.TZ || 'UTC').slice(0, 4)}&c=${country(env)}`,
    ttl: () => HOL_TTL,
    load: loadHolidays,
    valid: (d) => Array.isArray(d) && d.length > 0,
  },
};

// One feed of NEWS_FEEDS as an upstream: its own cache entry under its URL, the parsed items,
// shared by /board, /news and /article. "news2": the items hold the id, the
// link and the summary, so an entry of an older format is never read.
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
  const r = await deps.fetch(u.url(env, now), fetchOpts());
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
// waitUntil}. opts.cacheOnly (since Couch 0.5.0): the cache entry only, never a fetch or a write
// (an old entry is stale with BUDGET_WORDS). Never throws.
export const BUDGET_WORDS = 'not refreshed: the subrequest budget';
export async function get(name, env, now, deps, opts = {}) {
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
  if (has && now - entry.at < u.ttl(env, entry.data, now)) return { state: 'ok', age: now - entry.at, data: entry.data };
  const old = (reason) => (has ? { state: 'stale', reason, age: now - entry.at, data: entry.data } : { state: 'none', reason, age: 0, data: null });
  if (opts.cacheOnly) return old(BUDGET_WORDS);
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

// GET /futbol?d=0|1: the Fútbol page's reply (futbol.js) for today or tomorrow. The template has no
// source of fixtures, so its state is none: a Worker with one gives futbolText that day's data (the
// normalized form of futbol.js) as get() gives it.
export const FUTBOL_NONE = 'no f\u00fatbol source in this Worker';
function futbolReply(env, now, d) {
  const tz = env.TZ || 'UTC';
  try {
    return futbolText({ now, tz, d, r: { state: 'none', reason: FUTBOL_NONE, age: 0, data: null } });
  } catch (e) {
    return ['couch-futbol 1', `now=${now}`, 'futbol_age=0', `futbol_state=none|worker: ${clean((e && e.message) || e).slice(0, 60)}`, 'end', ''].join('\n');
  }
}

// The request with its dependencies, for the tests; the default export wires the runtime's.
export async function handle(request, env, deps) {
  const url = new URL(request.url);
  const route = request.method === 'GET' ? url.pathname : '';
  if (route === '/news') return textReply(await newsReply(env, deps.now(), deps));
  if (route === '/article') return textReply(await articleReply(env, deps.now(), deps, url.searchParams.get('id') || ''));
  if (route === '/futbol') return textReply(futbolReply(env, deps.now(), url.searchParams.get('d') === '1' ? 1 : 0));
  if (route !== '/board') {
    return new Response('not found\n', { status: 404, headers: { 'content-type': 'text/plain; charset=utf-8' } });
  }
  const now = deps.now();
  // No RATES: no rate lines. No COUNTRY: no holiday. No NEWS_FEEDS: no news lines. Every
  // upstream in parallel.
  const feeds = parseFeeds(env.NEWS_FEEDS);
  const [wx, air, fx, hol, ...rs] = await Promise.all([get('wx', env, now, deps), get('air', env, now, deps),
    fxCodes(env).length ? get('fx', env, now, deps) : null, country(env) ? get('hol', env, now, deps) : null,
    ...feeds.map((f) => get(newsUpstream(f), env, now, deps))]);
  const news = feeds.map((f, i) => ({ label: f.label, weight: f.weight, r: rs[i] }));
  // Always 200 with the states in the body: a reply that cannot be formatted gives the upstreams
  // as none with the error, and a bad setting (TZ) the bare lines.
  let text;
  try {
    text = boardText({ now, fx: fx || undefined, wx, air, news, hol: hol || undefined, env });
  } catch (e) {
    const reason = `worker: ${clean((e && e.message) || e).slice(0, 60)}`;
    const none = { state: 'none', reason, age: 0, data: null };
    try {
      text = boardText({ now, fx: none, wx: none, news: feeds.map((f) => ({ label: f.label, weight: f.weight, r: none })), env });
    } catch {
      text = ['couch-board 1', `now=${now}`, `rate_state=none|${reason}`, `wx_state=none|${reason}`,
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
