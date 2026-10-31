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
import { FEED_ITEMS, feedItems, parseFeeds, readFeedHead, imageUrl } from './news.js';
import { articleText, capParas, NEWS_LIST, newsText } from './reader.js';
import { extractPage } from './article.js';
import { futbolText } from './futbol.js';
import { economiaText } from './economia.js';

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

// A fetch's options: its time limit as timeout (ms), which paced() turns into a signal when the
// fetch starts, not when it is asked for.
const fetchOpts = () => ({ timeout: TIMEOUT_MS, headers: { 'user-agent': 'couch-board' } });

// The runtime's 6 connections at once (Cloudflare's "Simultaneous open connections",
// https://developers.cloudflare.com/workers/platform/limits/#simultaneous-open-connections): at
// most 6 calls of a request wait for their response headers at once, fetch() and the Cache API's
// match() and put() among them, and a 7th waits in the runtime's queue until one of them has its
// headers. A body that is being read no longer counts. A timer made when a fetch was asked for
// runs out in that queue, so a healthy upstream behind slow ones could fail with "no answer in 4 s"
// (the Board asks up to 10 at once). slots() gives FETCH_SLOTS slots that a request's fetches
// (paced) and Cache API calls (pacedCache) share: at most that many start at once, in the order
// they were asked for, each fetch with its own timer from its start (opts.timeout). A slot frees
// when the answer's headers came (a match: the entry or none; a put: the write done) or the call
// failed, not when its body is read (the runtime does not count a body).
export const FETCH_SLOTS = 6;
export function slots(n = FETCH_SLOTS) {
  let busy = 0;
  const queue = [];
  const next = () => {
    while (busy < n && queue.length) queue.shift()();
  };
  return (start) => new Promise((resolve, reject) => {
    queue.push(() => {
      busy++;
      let p;
      try {
        p = start();
      } catch (e) {
        p = Promise.reject(e);
      }
      Promise.resolve(p).then(resolve, reject).finally(() => {
        busy--;
        next();
      });
    });
    next();
  });
}

// fetchFn in the slots of run (slots' function, or a number: slots of its own).
export function paced(fetchFn, run = FETCH_SLOTS) {
  const take = typeof run === 'function' ? run : slots(run);
  return (url, opts = {}) => take(() => {
    const { timeout, ...rest } = opts;
    return fetchFn(url, timeout ? { ...rest, signal: AbortSignal.timeout(timeout) } : rest);
  });
}

// A Cache API (caches.default) whose match and put take the slots of run, as paced's fetches do.
export function pacedCache(cache, run) {
  return { match: (k) => run(() => cache.match(k)), put: (k, r) => run(() => cache.put(k, r)) };
}

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
// Cached as one entry under the country (no year: UPSTREAMS.hol): {data: [{fecha, nombre}]} sorted.
export const HOL_TTL = 86400;
const country = (env) => (/^[A-Za-z]{2}$/.test(String(env.COUNTRY || '').trim()) ? String(env.COUNTRY).trim().toUpperCase() : '');
// True when the list holds a holiday on or after now's local date (false for a bad TZ).
function holAhead(d, env, now) {
  try {
    const today = localDate(now, env.TZ || 'UTC');
    return Array.isArray(d) && d.some((h) => h && h.fecha >= today);
  } catch {
    return false;
  }
}
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
  // The key holds CURRENCY and the codes, not the date (a new RATES fetches; FX_TTL refreshes the
  // entry each hour, and the URL takes its dates from now): after local midnight a failed fetch
  // still gives the last rates, stale.
  fx: {
    host: 'frankfurter.dev',
    url: (env, now) => `https://api.frankfurter.dev/v1/${addDays(localDate(now, env.TZ || 'UTC'), -FX_DAYS)}..?` +
      new URLSearchParams({ base: home(env), symbols: fxCodes(env).join(',') }),
    key: (env) => `https://couch.internal/fx?b=${home(env)}&s=${fxCodes(env).join(',')}`,
    ttl: (env) => Number(env.FX_TTL) || FX_TTL,
    read: readFx,
    valid: (d) => !!d && Array.isArray(d.dates) && d.dates.length > 0,
  },
  // The key holds the country, not the year: on 1 January a failed fetch still gives the entry of
  // before (it holds the next year's list once this year's are over). An entry with no holiday on
  // or after today is old at once (ttl 0), so the next year's list comes on the next request.
  hol: {
    host: 'date.nager.at',
    key: (env) => `https://couch.internal/hol?c=${country(env)}`,
    ttl: (env, d, now) => (holAhead(d, env, now) ? HOL_TTL : 0),
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
    key: () => `https://couch.internal/news3?u=${encodeURIComponent(feed.url)}`,
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
      const r = await deps.fetch(item.link, { timeout: ARTICLE_TIMEOUT_MS, headers: { 'user-agent': 'couch-board' } });
      if (!r.ok) {
        if (r.body) r.body.cancel().catch(() => {});
        return { data: null, bad: `HTTP ${r.status}` };
      }
      const page = await (deps.extract || extractPage)(r, r.url || item.link);
      return { data: { ...capParas(page.paras), image: imageUrl(page.image) }, bad: 'no paragraphs' };
    },
    valid: (d) => !!d && Array.isArray(d.paras) && d.paras.length > 0,
  };
}

// One try of an upstream: {data, bad} as its read (or load, for an upstream that makes its own
// fetches) gives them; a fetch or body error throws.
async function attempt(u, env, now, deps) {
  if (u.load) return u.load(env, now, deps);
  const r = await deps.fetch(u.url(env, now), fetchOpts());
  if (!r.ok) {
    if (r.body) r.body.cancel().catch(() => {});      // an error page is not read
    return { data: null, bad: `HTTP ${r.status}` };
  }
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
  if (has && now - entry.at < u.ttl(env, entry.data, now)) return { state: 'ok', age: now - entry.at, data: entry.data };
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

// The items of the feeds that /news listed from (since Couch 0.5.0's review): a headline that left
// its feed after the list was made still opens (/article). One cache entry, {items: [{id, title,
// full, at, link, summary, label, category}]}: the newest LISTED_MAX by date, the feed's label and
// category with each. /news reads it and writes it after the reply (waitUntil) when the feeds
// hold an id it lacks; the Cache API keeps it KEEP_S. Only items of the configured feeds come in,
// so /article still fetches no link that a feed did not give.
const LISTED_KEY = 'https://couch.internal/listed';
export const LISTED_MAX = 150;
const listedOk = (x) => !!x && typeof x === 'object' && /^[0-9a-f]{8}$/.test(x.id);

async function listedRead(deps) {
  try {
    const hit = await deps.cache.match(LISTED_KEY);
    const e = hit ? await hit.json() : null;
    return e && Array.isArray(e.items) ? e.items.filter(listedOk) : [];
  } catch {
    return [];
  }
}

async function listedSave(feeds, rs, deps) {
  const fresh = feeds.flatMap((f, i) => (Array.isArray(rs[i].data) ? rs[i].data : []).filter(listedOk)
    .map((x) => ({ ...x, label: f.label, category: f.category })));
  const old = await listedRead(deps), have = new Set(old.map((x) => x.id));
  if (!fresh.some((x) => !have.has(x.id))) return;
  const by = new Map([...old, ...fresh].map((x) => [x.id, x]));      // a feed's copy over the old one
  const items = [...by.values()].sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity)).slice(0, LISTED_MAX);
  await deps.cache.put(LISTED_KEY, new Response(JSON.stringify({ items }), {
    headers: { 'content-type': 'application/json', 'cache-control': `max-age=${KEEP_S}` },
  }));
}

// GET /news: the list (reader.js's newsText) of every feed of NEWS_FEEDS, from the cache that
// /board fills (or a fetch, as /board's); the listed items' entry after the reply (the list uses
// nothing of it). Always 200, the states in the body.
async function newsReply(env, now, deps) {
  const feeds = parseFeeds(env.NEWS_FEEDS);
  try {
    const rs = await Promise.all(feeds.map((f) => get(newsUpstream(f), env, now, deps)));
    deps.waitUntil(listedSave(feeds, rs, deps).catch(() => {}));
    return newsText({ now, feeds: feeds.map((f, i) => ({ ...f, r: rs[i] })), max: Number(env.NEWS_LIST) || NEWS_LIST });
  } catch (e) {
    return [`couch-news 1`, `now=${now}`, 'news_age=0', `news_state=none|worker: ${clean((e && e.message) || e).slice(0, 60)}`, 'end', ''].join('\n');
  }
}

// GET /article?id=: the item of that id in the cached feeds of NEWS_FEEDS, else in the items /news
// listed (none: state none, and no fetch, so only a link that a configured feed gave is ever
// fetched), then its page's paragraphs (articleUpstream), else its summary. Always 200, the state
// in the body.
// The item of an id: in the cached feeds, else in the listed items (a headline that has left its
// feed since /news listed it). {item, feed}, or null.
async function findItem(env, now, deps, id) {
  const feeds = parseFeeds(env.NEWS_FEEDS);
  const rs = await Promise.all(feeds.map((f) => get(newsUpstream(f), env, now, deps)));
  let item = null, feed = null;
  feeds.forEach((f, i) => {
    const x = !item && Array.isArray(rs[i].data) ? rs[i].data.find((d) => d && d.id === id) : null;
    if (x) [item, feed] = [x, f];
  });
  if (!item) {
    const x = (await listedRead(deps)).find((d) => d.id === id);
    if (x) [item, feed] = [x, { label: x.label, category: x.category }];
  }
  return item ? { item, feed } : null;
}

// GET /image?id=<item id> (Couch 0.6.0): the item's picture, the feed's (news.js's itemImage) or
// the page's og:image from the article's cache entry (read only: no page fetch here), fetched
// once and kept in the Cache API as bytes for IMAGE_TTL. 200 with the image's own type (JPEG or
// PNG only) and its length; a text 404 for anything else (no image, too large, not an image, the
// fetch failed). Only an item of a cached feed is served: the Worker is no open proxy.
export const IMAGE_MAX = 1572864;       // 1.5 MB: Couch's IMG_MAX
export const IMAGE_TTL = 86400;
export const IMAGE_TIMEOUT_MS = 6000;
const IMAGE_TYPES = ['image/jpeg', 'image/png'];
const imageResponse = (bytes, type) => new Response(bytes, {
  headers: { 'content-type': type, 'content-length': String(bytes.byteLength), 'cache-control': 'no-store' },
});
async function readBytes(r, cap) {
  const reader = r.body.getReader(), chunks = [];
  let n = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    n += value.byteLength;
    if (n > cap) {
      await reader.cancel().catch(() => {});
      return null;
    }
    chunks.push(value);
  }
  const out = new Uint8Array(n);
  let o = 0;
  for (const c of chunks) {
    out.set(c, o);
    o += c.byteLength;
  }
  return out;
}
async function imageReply(env, now, deps, id) {
  const bad = (reason) => new Response(`${reason}\n`, { status: 404, headers: { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'no-store' } });
  try {
    if (!/^[0-9a-f]{8}$/.test(id)) return bad('bad id');
    const found = await findItem(env, now, deps, id);
    if (!found) return bad('unknown id');
    let url = imageUrl(found.item.image);
    if (!url && found.item.link) {
      try {
        const r = await get(articleUpstream({ ...found.item, link: new URL(found.item.link).href }), env, now, deps, { cacheOnly: true });
        url = imageUrl(r.data && r.data.image);
      } catch {
        url = '';
      }
    }
    if (!url) return bad('no image');
    const key = `https://couch.internal/image?u=${encodeURIComponent(url)}`;
    try {
      const hit = await deps.cache.match(key);
      if (hit) return imageResponse(new Uint8Array(await hit.arrayBuffer()), hit.headers.get('content-type') || 'image/jpeg');
    } catch { /* no cache: fetched */ }
    const host = new URL(url).hostname.replace(/^www\./, '');
    let r;
    try {
      r = await deps.fetch(url, { timeout: IMAGE_TIMEOUT_MS, headers: { 'user-agent': 'couch-board' } });
    } catch (e) {
      return bad(`${host}: ${errWords(e, IMAGE_TIMEOUT_MS)}`);
    }
    const drop = () => { if (r.body) r.body.cancel().catch(() => {}); };
    if (!r.ok) {
      drop();
      return bad(`${host}: HTTP ${r.status}`);
    }
    const type = String(r.headers.get('content-type') || '').split(';')[0].trim().toLowerCase();
    if (!IMAGE_TYPES.includes(type)) {
      drop();
      return bad(`${host}: not an image (${type || 'no type'})`);
    }
    const len = Number(r.headers.get('content-length'));
    if (Number.isFinite(len) && len > IMAGE_MAX) {
      drop();
      return bad(`${host}: too large (${len} bytes)`);
    }
    const bytes = r.body ? await readBytes(r, IMAGE_MAX) : null;
    if (!bytes || !bytes.byteLength) return bad(`${host}: ${bytes ? 'empty' : `too large (over ${IMAGE_MAX} bytes)`}`);
    deps.waitUntil(Promise.resolve().then(() => deps.cache.put(key, new Response(bytes, {
      headers: { 'content-type': type, 'cache-control': `max-age=${IMAGE_TTL}` },
    }))).catch(() => {}));
    return imageResponse(bytes, type);
  } catch (e) {
    return bad(`worker: ${clean((e && e.message) || e).slice(0, 60)}`);
  }
}

async function articleReply(env, now, deps, id) {
  const tz = env.TZ || 'UTC';
  try {
    if (!/^[0-9a-f]{8}$/.test(id)) return articleText({ now, tz, a: { state: 'none', reason: 'bad id' } });
    const found = await findItem(env, now, deps, id);
    if (!found) return articleText({ now, tz, a: { state: 'none', reason: 'unknown id' } });
    const { item, feed } = found;
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
    return articleText({ now, tz, item, feed, a, photo: !!(imageUrl(item.image) || imageUrl(r.data && r.data.image)) });
  } catch (e) {
    return articleText({ now, tz, a: { state: 'none', reason: `worker: ${clean((e && e.message) || e).slice(0, 60)}` } });
  }
}

// GET /futbol?d=0|1: the Fútbol page's reply (futbol.js) for today or tomorrow. The template has no
// source of fixtures, so its state is none: a Worker with one gives futbolText that day's data (the
// normalized form of futbol.js) as get() gives it.
export const FUTBOL_NONE = 'no f\u00fatbol source in this Worker';
export const ECON_NONE = 'no economy source in this Worker';
function futbolReply(env, now, d) {
  const tz = env.TZ || 'UTC';
  try {
    return futbolText({ now, tz, d, r: { state: 'none', reason: FUTBOL_NONE, age: 0, data: null } });
  } catch (e) {
    return ['couch-futbol 1', `now=${now}`, 'futbol_age=0', `futbol_state=none|worker: ${clean((e && e.message) || e).slice(0, 60)}`, 'end', ''].join('\n');
  }
}

// GET /economia: the Economía page's reply (economia.js, Couch 0.6.0): the indicators of the
// Argentine economy. The template has no source (the author's private Worker reads the BCRA,
// INDEC on datos.gob.ar, DolarAPI and ArgentinaDatos: economia.js's catalogue names the ids), so
// its state is none: a Worker with the sources gives economiaText the econ entry's data, the
// dollar quotes and the country risk as get() gives them.
function economiaReply(env, now) {
  const tz = env.TZ || 'UTC';
  return economiaText({ now, tz, r: { state: 'none', reason: ECON_NONE, age: 0, data: null }, usd: null, rp: null });
}

// A request's dependencies in the runtime (the default export's; the tests give a fake fetch and
// cache): the fetches and the Cache API calls in the same slots (slots above).
export function runtimeDeps(ctx, fetchFn = (u, o) => fetch(u, o), cache = caches.default) {
  const run = slots();
  return {
    fetch: paced(fetchFn, run),
    cache: pacedCache(cache, run),
    waitUntil: (p) => ctx.waitUntil(p),
    now: () => Math.floor(Date.now() / 1000),
    extract: extractPage,
  };
}

// The request with its dependencies, for the tests; the default export wires the runtime's.
export async function handle(request, env, deps) {
  const url = new URL(request.url);
  const route = request.method === 'GET' ? url.pathname : '';
  if (route === '/news') return textReply(await newsReply(env, deps.now(), deps));
  if (route === '/article') return textReply(await articleReply(env, deps.now(), deps, url.searchParams.get('id') || ''));
  if (route === '/futbol') return textReply(futbolReply(env, deps.now(), url.searchParams.get('d') === '1' ? 1 : 0));
  if (route === '/economia') return textReply(economiaReply(env, deps.now()));
  if (route === '/image') return imageReply(env, deps.now(), deps, url.searchParams.get('id') || '');
  if (route !== '/board') {
    return new Response('not found\n', { status: 404, headers: { 'content-type': 'text/plain; charset=utf-8' } });
  }
  const now = deps.now();
  // No RATES: no rate lines. No COUNTRY: no holiday. No NEWS_FEEDS: no news lines. Every
  // upstream in parallel, asked in this order (paced() starts 6 at a time): the small answers
  // first, the feeds (up to 96 KB of each, more for a feed that is not newest first) last.
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
    return handle(request, env, runtimeDeps(ctx));
  },
};
