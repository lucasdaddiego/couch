// The news reader's replies for Couch (since 2026-10-14): GET /news (the list) and GET
// /article?id= (one item's text), each its own request because a Couch 0.2.0 refuses a Board over
// 4 KB. Plain UTF-8 text as the Board's (board.js): line 1 the format and its version, then
// key=value lines with "|" between the fields of a value, then "end". No value holds "|", CR or LF
// (clean() makes them spaces). Pure functions: the Worker (index.js) gets the feeds and the page.

import { clean, dateFmt } from './board.js';
import { cutTitle } from './news.js';

export const NEWS_VERSION = 1;
export const ARTICLE_VERSION = 1;
export const NEWS_LIST = 30;           // items of /news, at most
export const NEWS_BYTES = 32768;       // a /news reply, at most: the oldest items drop first
export const PARA_MAX = 1500;          // code points of a paragraph, at most (cut at a word, "...")
export const TEXT_BYTES = 12288;       // bytes of an article's p= lines (with "p=" and LF), at most
export const LINK_BYTES = 1024;        // a link line, at most (else no link line)
export const ARTICLE_BYTES = 16384;    // an /article reply, at most

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const utf8 = (s) => new TextEncoder().encode(s).length;
const age = (at, now) => (Number.isFinite(at) ? Math.max(0, now - at) : '-');

// Each feed's share of max items, by weight (largest remainder: 3, 3, 1, 1 of 30 give 11, 11, 4,
// 4; on a tie the feed earlier in NEWS_FEEDS gets the item). A weight that is not a whole number
// above 0 counts 1.
export function quotas(weights, max) {
  const w = weights.map((x) => (Number.isInteger(x) && x > 0 ? x : 1));
  const sum = w.reduce((a, b) => a + b, 0);
  if (!sum || max <= 0) return w.map(() => 0);
  const exact = w.map((x) => (x * max) / sum), q = exact.map(Math.floor);
  let left = max - q.reduce((a, b) => a + b, 0);
  const order = exact.map((x, i) => [x - q[i], i]).sort((a, b) => b[0] - a[0] || a[1] - b[1]);
  for (const [, i] of order) {
    if (left-- <= 0) break;
    q[i]++;
  }
  return q;
}

// The /news reply. feeds: [{label, category, weight, r}], one per feed of NEWS_FEEDS, r as
// index.js's get() gives it (r.data: news.js's feedItems). Each feed first gives its quota of its
// newest items (quotas above), then each feed gives one item in turn until max, so a feed with
// fewer items leaves its share to the others; an id already taken (the same link in two feeds) is
// skipped. Then the items are sorted newest first (no date last) and the oldest drop until the
// reply fits NEWS_BYTES. No summary since Couch 0.5.0's review: Couch never drew it (Couch 0.4.1
// reads a missing one as none); the cache keeps it for /article's fallback.
//   couch-news 1
//   now=<unix>
//   item=<id>|<source>|<category>|<age>|<title>      (0 to max lines)
//   news_age=<s>
//   news_state=ok | stale|<reason> | none|<reason>
//   end
export function newsText({ now, feeds = [], max = NEWS_LIST }) {
  const head = [`couch-news ${NEWS_VERSION}`, `now=${now}`];
  if (!feeds.length) return [...head, 'news_age=0', 'news_state=none|no NEWS_FEEDS', 'end', ''].join('\n');
  const lists = feeds.map(({ label, category, r }) => (r && Array.isArray(r.data) ? r.data : [])
    .filter((x) => x && /^[0-9a-f]{8}$/.test(x.id) && typeof x.title === 'string' && clean(x.title))
    .map((x) => ({
      id: x.id, label: clean(label) || '-', cat: clean(category) || '-', title: clean(x.title),
      at: Number.isFinite(x.at) ? x.at : null,
    }))
    .sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity)));
  const picked = [], seen = new Set(), taken = lists.map(() => 0);
  const take = (j, k) => {
    while (k > 0 && taken[j] < lists[j].length && picked.length < max) {
      const x = lists[j][taken[j]++];
      if (seen.has(x.id)) continue;
      seen.add(x.id);
      picked.push(x);
      k--;
    }
  };
  quotas(feeds.map((f) => f.weight), max).forEach((q, j) => take(j, q));
  while (picked.length < max && lists.some((l, j) => taken[j] < l.length)) lists.forEach((_, j) => take(j, 1));
  picked.sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity));
  const rs = feeds.map((f) => f.r || { state: 'none', reason: '', age: 0, data: null });
  const bad = rs.find((r) => r.state !== 'ok');
  const tail = [`news_age=${Math.max(0, ...rs.filter((r) => r.data).map((r) => r.age | 0))}`,
    `news_state=${!bad ? 'ok' : `${picked.length ? 'stale' : 'none'}|${clean(bad.reason)}`}`, 'end', ''];
  const lines = picked.map((x) => `item=${x.id}|${x.label}|${x.cat}|${age(x.at, now)}|${x.title}`);
  let size = utf8([...head, ...tail].join('\n')) + lines.reduce((a, l) => a + utf8(l) + 1, 0);
  while (lines.length && size > NEWS_BYTES) size -= utf8(lines.pop()) + 1;
  return [...head, ...lines, ...tail].join('\n');
}

// An article's paragraphs for the reply: each cleaned and cut to PARA_MAX code points at a word,
// empty ones dropped, in order until the p= lines would pass TEXT_BYTES (whole paragraphs: the
// one that crosses and the rest drop, cut: true).
export function capParas(paras) {
  const out = [];
  let bytes = 0, cut = false;
  for (const raw of Array.isArray(paras) ? paras : []) {
    const t = cutTitle(clean(raw), PARA_MAX);
    if (!t) continue;
    const n = utf8(`p=${t}\n`);
    if (bytes + n > TEXT_BYTES) {
      cut = true;
      break;
    }
    out.push(t);
    bytes += n;
  }
  return { paras: out, cut };
}

// t (Unix seconds) in tz as "Wed 14 Oct 16:42", "-" for no time.
export function dateWords(t, tz) {
  if (!Number.isFinite(t)) return '-';
  const p = {};
  for (const x of dateFmt('en-US', {
    timeZone: tz, month: '2-digit', day: '2-digit', weekday: 'short', hour: '2-digit', minute: '2-digit', hourCycle: 'h23',
  }).formatToParts(new Date(t * 1000))) p[x.type] = x.value;
  return `${p.weekday} ${+p.day} ${MONTHS[+p.month - 1]} ${p.hour}:${p.minute}`;
}

// The /article reply. a: {state, reason, paras}: state 'ok' (the page's paragraphs), 'cut' (the
// same, the end dropped at TEXT_BYTES), 'summary' (the page failed: the feed's summary as the
// only paragraph, reason why) or 'none' (no item: only the state line, reason why). item is
// news.js's feed item, feed its entry of NEWS_FEEDS.
//   couch-article 1
//   now=<unix>
//   id=<id>
//   source=<label>
//   category=<category or ->
//   title=<whole title>
//   date=<local words or ->|<age or ->
//   link=<url>                     (none when the item has no link, or it is over LINK_BYTES)
//   state=ok | cut | summary|<reason> | none|<reason>
//   p=<paragraph>                  (0 or more)
//   end
export function articleText({ now, tz = 'UTC', item = null, feed = null, a, photo = false }) {
  const out = [`couch-article ${ARTICLE_VERSION}`, `now=${now}`];
  if (!item || a.state === 'none') return [...out, `state=none|${clean(a.reason)}`, 'end', ''].join('\n');
  out.push(`id=${item.id}`, `source=${clean(feed && feed.label) || '-'}`, `category=${clean(feed && feed.category) || '-'}`,
    `title=${clean(item.full || item.title)}`, `date=${dateWords(item.at, tz)}|${age(item.at, now)}`);
  const link = String(item.link || '').replace(/[\u0000- \u007f|]/g, (c) => `%${c.charCodeAt(0).toString(16).padStart(2, '0').toUpperCase()}`);
  if (link && utf8(`link=${link}`) <= LINK_BYTES) out.push(`link=${link}`);
  if (photo) out.push('photo=1');          // Couch 0.6.0 asks /image?id= for it; an older Couch skips the key
  out.push(a.state === 'ok' || a.state === 'cut' ? `state=${a.state}` : `state=summary|${clean(a.reason)}`);
  const ps = (a.paras || []).map((p) => `p=${p}`);
  // A guard only: the caps above keep the reply within ARTICLE_BYTES (title 300 code points, link
  // 1 KB, p= lines 12 KB).
  let size = utf8([...out, 'end', ''].join('\n')) + ps.reduce((n, l) => n + utf8(l) + 1, 0);
  while (ps.length && size > ARTICLE_BYTES) size -= utf8(ps.pop()) + 1;
  return [...out, ...ps, 'end', ''].join('\n');
}
