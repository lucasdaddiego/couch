// The Board's dated series (index.js's blue30 and rplist upstreams): argentinadatos.com's whole
// lists, oldest first, read by their tail. The Blue dollar's list is 523 KB of JSON (25 KB with
// gzip, every day since 2011), riesgo pais's 413 KB: the Worker never JSON.parses them (the free
// plan gives about 10 ms of CPU per request). It reads the body as a stream, keeps only its last
// TAIL_BYTES, decodes those, and reads the entries there with regular expressions. Pure functions
// besides the stream read: the Worker caches the entries, not the list.

export const TAIL_BYTES = 8192;         // about 90 entries of the Blue list, 180 of riesgo pais
export const BODY_MAX = 4 << 20;        // bytes read of a list, at most (the Blue list grows 30 KB a year)

// The last `bytes` bytes of a body (a ReadableStream of bytes) as UTF-8 text: the chunks are
// kept only while they hold part of the tail, so the body is never one string. A character that
// the cut splits decodes as U+FFFD (the lists are ASCII). A body over BODY_MAX throws, and so does
// a stall or a break of the body, as r.text() did.
export async function readTail(body, bytes = TAIL_BYTES) {
  if (!body) return '';
  const reader = body.getReader(), keep = [];
  let n = 0, total = 0;
  try {
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      total += value.length;
      if (total > BODY_MAX) throw new Error(`the list is over ${BODY_MAX >> 20} MB`);
      keep.push(value);
      n += value.length;
      while (keep.length > 1 && n - keep[0].length >= bytes) n -= keep.shift().length;
    }
  } finally {
    reader.cancel().catch(() => {});    // no-op on a body read to its end
  }
  const all = new Uint8Array(n);
  let o = 0;
  for (const c of keep) {
    all.set(c, o);
    o += c.length;
  }
  return new TextDecoder().decode(all.subarray(Math.max(0, n - bytes)));
}

// The entries of a list's text, oldest first: [{fecha, [field]}] for each whole {...} object that
// holds "fecha" (yyyy-mm-dd) and the number `field` ("venta", "valor"). An object that the tail
// cut (no "{" or no "}") is not whole and is skipped. A repeated date keeps its last value.
export function tailEntries(text, field) {
  const byDate = new Map(), num = new RegExp(`"${field}"\\s*:\\s*(-?\\d+(?:\\.\\d+)?)`);
  for (const m of String(text ?? '').matchAll(/\{([^{}]*)\}/g)) {
    const f = /"fecha"\s*:\s*"(\d{4}-\d{2}-\d{2})"/.exec(m[1]), v = num.exec(m[1]);
    if (f && v) byDate.set(f[1], Number(v[1]));
  }
  return [...byDate].sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)).map(([fecha, v]) => ({ fecha, [field]: v }));
}
