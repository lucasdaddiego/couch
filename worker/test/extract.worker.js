// The test Worker of article.workerd.test.js (miniflare runs it in workerd, which has the real
// HTMLRewriter). POST {html, url, chunk}: the page in chunks of `chunk` bytes through
// article.js's extractPage, as a fetch body; the answer is extractPage's result with the chunks
// read (pulls), all of them (chunks), whether the rest was cancelled and the page's bytes (size).
import { extractPage } from '../src/article.js';

export default {
  async fetch(request) {
    const { html, url, chunk = 65536 } = await request.json();
    const bytes = new TextEncoder().encode(html);
    const st = { pulls: 0, chunks: Math.ceil(bytes.length / chunk), cancelled: false, size: bytes.length };
    const body = new ReadableStream({
      pull(c) {
        const i = st.pulls * chunk;
        if (i >= bytes.length) return c.close();
        st.pulls++;
        c.enqueue(bytes.slice(i, i + chunk));
      },
      cancel() {
        st.cancelled = true;
      },
    }, { highWaterMark: 0 });
    try {
      return Response.json({ ...(await extractPage({ body }, url)), ...st });
    } catch (e) {
      return new Response(String((e && e.stack) || e), { status: 500 });
    }
  },
};
