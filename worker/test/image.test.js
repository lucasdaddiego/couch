// node --test: the reader's photo (Couch 0.6.0): the item's picture from a feed (news.js's
// itemImage), the photo line of /article, and GET /image?id= (the bytes of the picture, its type
// and length; the cache; a picture too large, not an image, missing, or of an unknown id), with
// a fake fetch and a fake Cache API that keeps bytes.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { itemImage, imageUrl, feedItems, IMAGE_URL_MAX } from '../src/news.js';
import { handle, IMAGE_MAX } from '../src/index.js';

const NOW = 1793297934;
const feedXml = (items) => `<?xml version="1.0"?><rss version="2.0" xmlns:media="http://search.yahoo.com/mrss/"><channel><title>Feed</title>${items}</channel></rss>`;
const items = `
<item><title>With a media picture</title><link>https://news.example/a</link><pubDate>Thu, 29 Oct 2026 12:00:00 GMT</pubDate>
  <description><![CDATA[<p>Words</p>]]></description>
  <media:content medium="image" url="https://img.example/a.jpg" width="1200"/></item>
<item><title>With a thumbnail and a typed enclosure</title><link>https://news.example/b</link><pubDate>Thu, 29 Oct 2026 11:00:00 GMT</pubDate>
  <media:thumbnail url="https://img.example/b-thumb.jpg"/><enclosure url="https://img.example/b.png" type="image/png" length="100"/></item>
<item><title>With an img in the description as entities</title><link>https://news.example/c</link><pubDate>Thu, 29 Oct 2026 10:00:00 GMT</pubDate>
  <description>&lt;img src=&quot;https://img.example/c.jpg&quot; /&gt;&lt;p&gt;Words&lt;/p&gt;</description></item>
<item><title>With no picture</title><link>https://news.example/d</link><pubDate>Thu, 29 Oct 2026 09:00:00 GMT</pubDate><description>Words</description></item>
<item><title>With a video only</title><link>https://news.example/e</link><pubDate>Thu, 29 Oct 2026 08:00:00 GMT</pubDate>
  <media:content medium="video" url="https://img.example/e.mp4" type="video/mp4"/></item>`;
const FEED = 'https://feeds.example/rss.xml';
const env = { TZ: 'America/Argentina/Buenos_Aires', NEWS_FEEDS: `Example=${FEED}`, NEWS_TTL: '600' };
const JPEG = new Uint8Array([0xff, 0xd8, 0xff, 0xe0, 0, 16, 74, 70, 73, 70, 0, 1, 1, 0, 0, 1, 0, 1, 0, 0, 0xff, 0xd9]);

function fakeCache() {
  const m = new Map();
  return {
    m, ops: 0,
    async match(k) { this.ops++; const e = m.get(k); return e ? new Response(e.bytes, { headers: e.headers }) : undefined; },
    async put(k, r) { this.ops++; m.set(k, { bytes: new Uint8Array(await r.arrayBuffer()), headers: Object.fromEntries(r.headers) }); },
  };
}
function deps({ answers = {}, cache = fakeCache(), now = NOW } = {}) {
  const calls = [];
  return {
    calls, cache, waits: [],
    now: () => now,
    waitUntil(p) { this.waits.push(p); },
    async fetch(url) {
      calls.push(url);
      const a = answers[url];
      if (!a) throw new TypeError('no route');
      if (a instanceof Error) throw a;
      return new Response(a.body, { status: a.status || 200, headers: a.headers || {} });
    },
  };
}
const answers = () => ({
  [FEED]: { body: feedXml(items), headers: { 'content-type': 'application/rss+xml' } },
  'https://img.example/a.jpg': { body: JPEG, headers: { 'content-type': 'image/jpeg', 'content-length': String(JPEG.byteLength) } },
  'https://img.example/b-thumb.jpg': { body: JPEG, headers: { 'content-type': 'image/jpeg' } },
  'https://img.example/c.jpg': { body: 'not really', headers: { 'content-type': 'text/html' } },
});
const ids = () => Object.fromEntries(feedItems(feedXml(items)).map((it) => [it.link.slice(-1), it.id]));

test('itemImage: media:content, the thumbnail, the enclosure, an img as entities, none, a video only; the URL rules', () => {
  const [a, b, c, d, e] = feedItems(feedXml(items));
  assert.deepEqual([a.image, b.image, c.image, d.image, e.image],
    ['https://img.example/a.jpg', 'https://img.example/b-thumb.jpg', 'https://img.example/c.jpg', '', '']);
  assert.equal(itemImage('<media:content url="https://x.example/p.jpg" type="image/jpeg"/>'), 'https://x.example/p.jpg');
  assert.equal(itemImage('<media:content url="https://x.example/p"/>'), 'https://x.example/p');          // no type, no medium: taken
  assert.equal(itemImage('<media:content type="video/mp4" url="https://x.example/v.mp4"/>'), '');
  assert.equal(itemImage('<enclosure url="https://x.example/a.mp3" type="audio/mpeg"/>'), '');
  assert.equal(itemImage('<description><![CDATA[<img src="https://x.example/i.jpg">]]></description>'), 'https://x.example/i.jpg');
  assert.equal(imageUrl('ftp://x.example/a.jpg'), '');
  assert.equal(imageUrl(' https://x.example/a.jpg '), 'https://x.example/a.jpg');
  assert.equal(imageUrl(`https://x.example/${'a'.repeat(IMAGE_URL_MAX)}`), '');
  assert.equal(imageUrl(null), '');
});

test('/article: photo=1 for an item with a picture, no line without one', async () => {
  const d = deps({ answers: answers() });
  const id = ids();
  const a = await (await handle(new Request(`http://x/article?id=${id.a}`), env, d)).text();
  assert.ok(a.split('\n').includes('photo=1'), a);
  const none = await (await handle(new Request(`http://x/article?id=${id.d}`), env, d)).text();
  assert.ok(!none.split('\n').includes('photo=1'), none);
});

test('/image: the bytes with their type and length; the cache serves the second call', async () => {
  const d = deps({ answers: answers() });
  const id = ids();
  const r = await handle(new Request(`http://x/image?id=${id.a}`), env, d);
  assert.equal(r.status, 200);
  assert.deepEqual([r.headers.get('content-type'), r.headers.get('content-length')], ['image/jpeg', String(JPEG.byteLength)]);
  assert.deepEqual(new Uint8Array(await r.arrayBuffer()), JPEG);
  await Promise.all(d.waits);
  const fetches = d.calls.filter((u) => u.startsWith('https://img.example/')).length;
  const r2 = await handle(new Request(`http://x/image?id=${id.a}`), env, d);
  assert.equal(r2.status, 200);
  assert.deepEqual(new Uint8Array(await r2.arrayBuffer()), JPEG);
  assert.equal(d.calls.filter((u) => u.startsWith('https://img.example/')).length, fetches);   // from the cache
  // the thumbnail of the second item, with no content-length
  const r3 = await handle(new Request(`http://x/image?id=${id.b}`), env, d);
  assert.equal(r3.status, 200);
  assert.equal(r3.headers.get('content-length'), String(JPEG.byteLength));
});

test('/image: not an image, too large, no picture, an unknown id, a bad id, the fetch failed', async () => {
  const a = answers();
  a['https://img.example/a.jpg'] = { body: JPEG, headers: { 'content-type': 'image/jpeg', 'content-length': String(IMAGE_MAX + 1) } };
  const d = deps({ answers: a });
  const id = ids();
  const text = async (path) => { const r = await handle(new Request(`http://x${path}`), env, d); return [r.status, (await r.text()).trim()]; };
  assert.deepEqual(await text(`/image?id=${id.c}`), [404, 'img.example: not an image (text/html)']);
  assert.deepEqual(await text(`/image?id=${id.a}`), [404, `img.example: too large (${IMAGE_MAX + 1} bytes)`]);
  assert.deepEqual(await text(`/image?id=${id.d}`), [404, 'no image']);
  assert.deepEqual(await text('/image?id=00000000'), [404, 'unknown id']);
  assert.deepEqual(await text('/image?id=zz'), [404, 'bad id']);
  a['https://img.example/b-thumb.jpg'] = new TypeError('connection reset');
  assert.deepEqual(await text(`/image?id=${id.b}`), [404, 'img.example: connection reset']);
  // a body over the cap with no content-length: read up to the cap, then refused
  a['https://img.example/b-thumb.jpg'] = { body: new Uint8Array(IMAGE_MAX + 10), headers: { 'content-type': 'image/png' } };
  assert.deepEqual(await text(`/image?id=${id.b}`), [404, `img.example: too large (over ${IMAGE_MAX} bytes)`]);
});
