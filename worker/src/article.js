// An article page's paragraphs for /article (since 2026-10-14), with the runtime's HTMLRewriter:
// a streaming parser (lol-html), no DOM, so the CPU stays in the free plan's 10 ms per request.
// The publisher's rule picks the body's <p> elements (RULES, by the page's host); a generic rule
// ("article p", paragraphs of 40 code points or more) runs in the same pass and is used when the
// publisher's rule finds less than MIN_TEXT code points (a new layout). The read stops early:
// QUIET_BYTES after the last body paragraph, at ENOUGH_CHARS of body text, or at PAGE_BYTES.
// The selectors were read on the pages of 2026-10-14 (test/fixtures/article_*.html).

import { clean } from './board.js';
import { decodeEntities } from './news.js';

export const PAGE_BYTES = 768 * 1024;  // bytes of a page read, at most (BBC's are about 400 KB)
export const QUIET_BYTES = 64 * 1024;  // bytes read after the last body paragraph, at most
export const ENOUGH_CHARS = 16384;     // body text (as the page writes it) that is enough for TEXT_BYTES
export const MIN_TEXT = 200;           // code points of a usable body, at least
export const MIN_PARA = 3;             // code points of a paragraph, at least ("PB": Clarin's initials)
export const GENERIC_MIN = 40;         // code points of a paragraph of the generic rule, at least
export const GENERIC = 'article p';

// The publishers' body paragraphs (each read on 2026-10-14):
// - Ambito: the <p> children of each <article class="article-body"> (the body comes in several
//   such articles between the related links; a <p> inside an embedded picture's caption is not a
//   child, and the lead in <h2 class="news-headline__article-summary"> is outside).
// - Clarin: the <p> children of the text blocks (<div class="... text-embed">) inside
//   <div id="cuerpo">; an embedded tweet's <p> sits in a <section> and stays out.
// - BBC: the <p> inside the text blocks (<div data-block="text">); captions, video notes and
//   the promo links' <p> are in other blocks.
export const RULES = [
  { name: 'ambito', hosts: ['ambito.com'], select: 'article.article-body > p' },
  { name: 'clarin', hosts: ['clarin.com'], select: '#cuerpo div.text-embed > p' },
  { name: 'bbc', hosts: ['bbc.co.uk', 'bbc.com'], select: 'div[data-block="text"] p' },
];

// The rule of a page's URL: its host or a subdomain of it; null for another host.
export function ruleFor(url) {
  let host;
  try {
    host = new URL(url).hostname.toLowerCase();
  } catch {
    return null;
  }
  return RULES.find((r) => r.hosts.some((h) => host === h || host.endsWith(`.${h}`))) || null;
}

// A page's paragraphs: {paras, how: the rule's name, 'generic' or 'none', bytes: the bytes read,
// stopped: true when the read stopped before the end}. response: the page's (its body a stream of
// bytes, UTF-8), url: the page's URL after redirects. Each paragraph's text is the text of the
// element and its children, entities decoded, spaces collapsed, "|" removed. Rewriter: the
// runtime's HTMLRewriter (a test passes its own).
export async function extractPage(response, url, Rewriter = globalThis.HTMLRewriter) {
  if (typeof Rewriter !== 'function') throw new Error('no HTMLRewriter');
  const rule = ruleFor(url), own = [], generic = [];
  const st = { pushed: 0, last: -1, chars: 0 };
  // A new paragraph at each matched <p>; its text chunks (also those of its children) go to the
  // last paragraph of the bucket, as a <p> cannot hold a <p>. The primary bucket drives the stop.
  const collect = (bucket, primary) => ({
    element() {
      bucket.push('');
      if (primary) st.last = st.pushed;
    },
    text(t) {
      if (!bucket.length) return;
      bucket[bucket.length - 1] += t.text;
      if (primary) st.chars += t.text.length;
    },
  });
  const rw = new Rewriter();
  if (rule) rw.on(rule.select, collect(own, true));
  rw.on(GENERIC, collect(generic, !rule));
  const src = response.body ? response.body.getReader() : null;
  let stopped = false, ended = !src;
  const input = new ReadableStream({
    async pull(c) {
      if (ended) return c.close();
      if (st.pushed >= PAGE_BYTES || st.chars >= ENOUGH_CHARS || (st.last >= 0 && st.pushed - st.last >= QUIET_BYTES)) {
        stopped = ended = true;
        c.close();
        await src.cancel().catch(() => {});
        return;
      }
      const r = await src.read();
      if (r.done) {
        ended = true;
        return c.close();
      }
      st.pushed += r.value.byteLength;
      c.enqueue(r.value);
    },
    cancel() {
      if (src && !ended) src.cancel().catch(() => {});
    },
  });
  // The handlers run while the rewritten stream is read; its bytes are dropped.
  const out = rw.transform(new Response(input, { headers: { 'content-type': 'text/html; charset=utf-8' } }));
  const reader = out.body.getReader();
  while (!(await reader.read()).done) { /* drop */ }
  const tidy = (bucket, min) => bucket.map((t) => clean(decodeEntities(t))).filter((t) => Array.from(t).length >= min);
  const total = (ps) => ps.reduce((n, t) => n + Array.from(t).length, 0);
  let paras = rule ? tidy(own, MIN_PARA) : [], how = rule ? rule.name : 'generic';
  if (total(paras) < MIN_TEXT) {
    paras = tidy(generic, GENERIC_MIN);
    how = 'generic';
  }
  if (total(paras) < MIN_TEXT) {
    paras = [];
    how = 'none';
  }
  return { paras, how, bytes: st.pushed, stopped };
}
