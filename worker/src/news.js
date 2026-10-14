// The Board's news (index.js's news upstreams, one per feed of NEWS_FEEDS): an RSS 2.0 feed's
// items as [{title, at}] (feedItems: also the id, the link and the summary, for /news and
// /article since 2026-10-14), with no XML library (Workers have no DOMParser): each <item>'s <title>
// and <pubDate>, the CDATA unwrapped, the tags stripped, the entities decoded, the publisher that
// Google News appends removed, the title cut to TITLE_MAX code points at a word, then the newest
// first. The Worker reads only the head of a feed's body (readFeedHead): a feed can be 1 MB, and
// the free plan gives about 10 ms of CPU per request. Then pure functions: the Worker caches the
// parsed items, not the XML.

import { clean } from './board.js';

export const TITLE_MAX = 110;          // code points: two lines of the TV's News page
export const ITEMS_MAX = 8;            // items kept per feed: a reply has at most 8 news lines
export const HEAD_BYTES = 96 * 1024;   // bytes read of a feed that is newest first, at most
export const SORT_ITEMS = 100;         // items read of a feed that is not newest first, at most
export const SORT_BYTES = 256 * 1024;  // bytes read of such a feed, at most
export const FEEDS_MAX = 6;            // feeds of NEWS_FEEDS, at most
export const WEIGHT_MAX = 8;           // a feed's weight, at most: NEWS_MAX's 8 lines
export const FEED_ITEMS = 20;          // items kept per feed in the cache that /board and /news share
export const FULL_MAX = 300;           // code points of a whole title (/article), at most
export const SUMMARY_MAX = 280;        // code points of a summary (/news, /article's fallback), at most
export const CATEGORY_RE = /^[a-z]{1,16}$/;   // a category of NEWS_FEEDS: economy, politics, world

// The head of a feed's body (a ReadableStream of bytes, as UTF-8): the text up to the end of its
// items-th </item>, else up to bytes bytes (an item cut there is not complete, and parseFeed
// skips it), else the whole body. At the items-th </item>, more(head) can raise both limits once
// ({items, bytes}, or null to stop there). The rest of the stream is cancelled, not read. A
// </item> that two chunks split counts too. A stall or a break of the body throws, as r.text() did.
export async function readHead(body, { items = ITEMS_MAX, bytes = HEAD_BYTES, more = null } = {}) {
  if (!body) return '';
  const reader = body.getReader(), dec = new TextDecoder();
  let text = '', n = 0, seen = 0, from = 0, left = null;
  try {
    for (;;) {
      let value = left;
      left = null;
      if (!value) {
        const r = await reader.read();
        if (r.done) return text + dec.decode();
        value = r.value;
      }
      const chunk = value.subarray(0, bytes - n);
      if (chunk.length < value.length) left = value.subarray(chunk.length);  // kept for a raised cap
      n += chunk.length;
      text += dec.decode(chunk, { stream: true });
      for (let i; (i = text.indexOf('</item>', from)) !== -1;) {
        from = i + 7;
        if (++seen < items) continue;
        const next = more && more(text.slice(0, from));
        more = null;
        if (!next) return text.slice(0, from);
        items = next.items;
        bytes = Math.max(next.bytes, n);
      }
      from = Math.max(from, text.length - 6);     // a "</item" split at the chunk's end
      if (n >= bytes) return text;
    }
  } finally {
    reader.cancel().catch(() => {});             // no-op on a body read to its end
  }
}

// The head of a feed for parseFeed: up to the end of its ITEMS_MAX-th item (at most HEAD_BYTES)
// when those items are newest first, else up to SORT_ITEMS items (at most SORT_BYTES), so that
// parseFeed can sort them. Google News' search sorts by relevance, BBC's feeds are not in order:
// for them the head of a feed in its own order does not hold its newest items.
// keep: the items wanted (index.js reads FEED_ITEMS, the tests the Board's ITEMS_MAX).
export function readFeedHead(body, keep = ITEMS_MAX) {
  return readHead(body, { items: keep, more: (head) => (newestFirst(head) ? null : { items: SORT_ITEMS, bytes: SORT_BYTES }) });
}

// The named entities of the feeds read on 2026-10-13 (&bull;, &nbsp;) and of Spanish text.
const NAMED = {
  amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ', bull: '•', hellip: '...', ndash: '–',
  mdash: '—', laquo: '«', raquo: '»', ldquo: '“', rdquo: '”', lsquo: '‘', rsquo: '’',
  iexcl: '¡', iquest: '¿', ordm: 'º', ordf: 'ª', deg: '°', euro: '€',
  aacute: 'á', eacute: 'é', iacute: 'í', oacute: 'ó', uacute: 'ú', ntilde: 'ñ', uuml: 'ü',
  Aacute: 'Á', Eacute: 'É', Iacute: 'Í', Oacute: 'Ó', Uacute: 'Ú', Ntilde: 'Ñ', Uuml: 'Ü',
};

// Text with its entities decoded: &#8220; &#x201c; and the names above; an unknown name stays.
export function decodeEntities(s) {
  return String(s ?? '').replace(/&(#\d{1,7}|#x[0-9a-fA-F]{1,6}|[a-zA-Z]+);/g, (m, e) => {
    if (e[0] !== '#') return Object.hasOwn(NAMED, e) ? NAMED[e] : m;
    const cp = e[1] === 'x' || e[1] === 'X' ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10);
    return cp > 0 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff) ? String.fromCodePoint(cp) : '';
  });
}

// An element's text: CDATA sections as they are, the rest with its entities decoded; then the
// tags stripped (a title may hold <b> or an escaped <i>) and the spaces collapsed.
export function elementText(raw) {
  let s = '';
  const re = /<!\[CDATA\[([\s\S]*?)\]\]>/g;
  let last = 0, m;
  while ((m = re.exec(raw))) {
    s += decodeEntities(raw.slice(last, m.index)) + m[1];
    last = re.lastIndex;
  }
  s += decodeEntities(raw.slice(last));
  return clean(s.replace(/<[^>]*>/g, ' '));
}

// A title cut to max code points: at the last space before the cut, with "..." (three dots, so a
// Couch whose UTF-8 text failed draws them too).
export function cutTitle(s, max = TITLE_MAX) {
  const cps = Array.from(String(s ?? ''));
  if (cps.length <= max) return cps.join('');
  let cut = cps.slice(0, max - 3).join('');
  const sp = cut.lastIndexOf(' ');
  if (sp > (max * 2) / 3) cut = cut.slice(0, sp);
  return cut.replace(/[\s,.;:–—-]+$/, '') + '...';
}

// A title without the publisher at its end: Google News writes "Title - Infobae" and names the
// publisher in the item's <source>. Only that exact end goes ("-", "–" or "—" between spaces); a
// title that is only the publisher stays.
export function dropPublisher(title, publisher) {
  if (!publisher) return title;
  for (const sep of [' - ', ' – ', ' — ']) {
    const end = sep + publisher;
    if (title.length > end.length && title.endsWith(end)) return title.slice(0, -end.length).trim();
  }
  return title;
}

// An item's id: FNV-1a (32 bits) of the UTF-8 bytes of its link, as 8 lowercase hex digits; of
// "title:" and its whole title when it has no link. Stable while the item is in its feed (the link
// does not change), and /article takes only an id of a cached feed item, so a collision can never
// fetch a page that no feed named.
export function itemId(link, full) {
  const bytes = new TextEncoder().encode(link ? String(link) : `title:${full}`);
  let h = 0x811c9dc5;
  for (const b of bytes) h = Math.imul(h ^ b, 0x01000193);
  return (h >>> 0).toString(16).padStart(8, '0');
}

// A feed item's link: an http or https URL as the URL parser writes it, else ''.
export function feedLink(s) {
  try {
    const u = new URL(String(s ?? '').trim());
    return u.protocol === 'https:' || u.protocol === 'http:' ? u.href : '';
  } catch {
    return '';
  }
}

// The items of a feed's text, in the feed's order, at most SORT_ITEMS: {id, title, full, at,
// link, summary}, at the item's pubDate (or dc:date) as Unix seconds, null when absent or before
// 2000 (a feed that writes "30 Nov -0001"). title is cut to TITLE_MAX code points, full to
// FULL_MAX, the summary (the <description>, tags stripped: Clarin writes a <ul> of <li>) to
// SUMMARY_MAX. The title is '' when the item has none, the link and the summary ''.
function items(xml) {
  const out = [], re = /<item\b[^>]*>([\s\S]*?)<\/item>/g;
  let m;
  while (out.length < SORT_ITEMS && (m = re.exec(xml))) {
    const t = /<title\b[^>]*>([\s\S]*?)<\/title>/.exec(m[1]);
    const s = /<source\b[^>]*>([\s\S]*?)<\/source>/.exec(m[1]);
    const full = t ? cutTitle(dropPublisher(elementText(t[1]), s ? elementText(s[1]) : ''), FULL_MAX) : '';
    const d = /<(pubDate|dc:date)\b[^>]*>([\s\S]*?)<\/\1>/.exec(m[1]);
    const ms = d ? Date.parse(elementText(d[2])) : NaN;
    const l = /<link\b[^>]*>([\s\S]*?)<\/link>/.exec(m[1]);
    const link = l ? feedLink(elementText(l[1])) : '';
    const ds = /<description\b[^>]*>([\s\S]*?)<\/description>/.exec(m[1]);
    out.push({
      id: itemId(link, full), title: cutTitle(full), full, at: Number.isFinite(ms) && ms >= Date.UTC(2000, 0, 1) ? Math.floor(ms / 1000) : null,
      link, summary: ds ? cutTitle(elementText(ds[1]), SUMMARY_MAX) : '',
    });
  }
  return out;
}

// True when the dated items of a feed's text are newest first (an item with no date is not
// counted; equal dates are in order).
export function newestFirst(xml) {
  let last = Infinity;
  for (const { at } of items(String(xml ?? ''))) {
    if (at === null) continue;
    if (at > last) return false;
    last = at;
  }
  return true;
}

// A feed's items, the newest first, at most ITEMS_MAX: {title, at}. Items with equal dates keep
// the feed's order, items with no date come last. An item with an empty title is skipped. [] for
// text that is no RSS.
export function parseFeed(xml) {
  return feedItems(xml, ITEMS_MAX).map(({ title, at }) => ({ title, at }));
}

// A feed's items as the cache keeps them (index.js's readFeed, shared by /board and /news): the
// newest first, at most max, each {id, title, full, at, link, summary} (items above). Sorted as
// parseFeed's, so the first ITEMS_MAX are parseFeed's items.
export function feedItems(xml, max = FEED_ITEMS) {
  return items(String(xml ?? '')).filter((x) => x.title)
    .sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity)).slice(0, max);
}

// NEWS_FEEDS, "Label=https://url,Label*2=https://url,Label*2@economy=https://url" (not CASAS's
// ':', which a URL holds): at most FEEDS_MAX feeds, each {label, url, weight, category} with an
// http or https URL. The weight ("*2" after the label, 1 to WEIGHT_MAX) is the feed's share of
// the news lines; no "*": weight 1. The category ("@economy" after the weight, since 2026-10-14:
// 1 to 16 letters a to z, case not counted, kept in lower case) names the feed's section on
// /news; no "@": ''. The first "=" ends the label part (a URL holds "="). A bad entry is skipped
// (also a bad category). A URL with a comma cannot be written here.
export function parseFeeds(spec) {
  const out = [];
  for (const part of String(spec ?? '').split(',')) {
    const i = part.indexOf('=');
    if (i < 1) continue;
    const m = /^(.*?)(?:\*\s*(\d+))?(?:\s*@\s*([^@*]*?))?\s*$/.exec(part.slice(0, i));
    const label = clean(m[1]), weight = m[2] === undefined ? 1 : Number(m[2]), url = part.slice(i + 1).trim();
    const category = m[3] === undefined ? '' : m[3].trim().toLowerCase();
    if (m[3] !== undefined && !CATEGORY_RE.test(category)) continue;
    let u;
    try {
      u = new URL(url);
    } catch {
      continue;
    }
    if (label && weight >= 1 && weight <= WEIGHT_MAX && (u.protocol === 'https:' || u.protocol === 'http:') && out.length < FEEDS_MAX) {
      out.push({ label, url: u.href, weight, category });
    }
  }
  return out;
}
