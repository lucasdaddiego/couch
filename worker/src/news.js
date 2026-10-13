// The Board's news (index.js's news upstreams, one per feed of NEWS_FEEDS): an RSS 2.0 feed's
// items as [{title, at}], with no XML library (Workers have no DOMParser): each <item>'s <title>
// and <pubDate>, the CDATA unwrapped, the tags stripped, the entities decoded, the title cut to
// TITLE_MAX code points at a word. Pure functions: the Worker caches the parsed items, not the
// XML (Clarin's feed is 12 KB, La Verdad's 188 KB).

import { clean } from './board.js';

export const TITLE_MAX = 110;        // code points: two lines of the TV's News page
export const ITEMS_MAX = 20;         // items kept per feed

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

// A feed's items, at most ITEMS_MAX, in the feed's order: {title, at}, at the item's pubDate (or
// dc:date) as Unix seconds, null when absent or before 2000 (a feed that writes "30 Nov -0001").
// An item with an empty title is skipped. [] for text that is no RSS.
export function parseFeed(xml) {
  const out = [], src = String(xml ?? '');
  const items = /<item\b[^>]*>([\s\S]*?)<\/item>/g;
  let m;
  while (out.length < ITEMS_MAX && (m = items.exec(src))) {
    const t = /<title\b[^>]*>([\s\S]*?)<\/title>/.exec(m[1]);
    const title = t ? cutTitle(elementText(t[1])) : '';
    if (!title) continue;
    const d = /<(pubDate|dc:date)\b[^>]*>([\s\S]*?)<\/\1>/.exec(m[1]);
    const ms = d ? Date.parse(elementText(d[2])) : NaN;
    out.push({ title, at: Number.isFinite(ms) && ms >= Date.UTC(2000, 0, 1) ? Math.floor(ms / 1000) : null });
  }
  return out;
}

// NEWS_FEEDS, "Label=https://url,Label=https://url" (not CASAS's ':', which a URL holds): at most 4
// feeds, each {label, url} with an http or https URL; a bad entry is skipped.
export function parseFeeds(spec) {
  const out = [];
  for (const part of String(spec ?? '').split(',')) {
    const i = part.indexOf('=');
    if (i < 1) continue;
    const label = clean(part.slice(0, i)), url = part.slice(i + 1).trim();
    let u;
    try {
      u = new URL(url);
    } catch {
      continue;
    }
    if (label && (u.protocol === 'https:' || u.protocol === 'http:') && out.length < 4) out.push({ label, url: u.href });
  }
  return out;
}
