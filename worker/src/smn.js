// SMN's open data for the Board's now line (index.js's 'smn' upstream): the zip of
// https://ssl.smn.gob.ar/dpd/zipopendata.php?dato=tiepre holds one Latin-1 text file with one line
// per station: station;date;time;sky;visibility;temperature;feels like;humidity;wind;pressure, and
// " /" at the end. Read with no library: the zip's central directory, DecompressionStream
// ('deflate-raw'), the Latin-1 bytes by hand. Pure functions: the Worker caches the parsed row.

import { fold, tzOffset } from './board.js';

// The stations' clock (Argentina's official time), whatever TZ the Board uses.
export const SMN_TZ = 'America/Argentina/Buenos_Aires';
const MAX_TEXT = 1 << 20;            // the text file of 2026-10-12 has 11697 bytes

const MONTHS = ['enero', 'febrero', 'marzo', 'abril', 'mayo', 'junio', 'julio', 'agosto', 'septiembre', 'octubre',
  'noviembre', 'diciembre'];

// The first entry of a zip: {name, method, size, data}, with data its compressed bytes. The sizes
// and the offset come from the central directory (the local header can leave them to a data
// descriptor), the name and extra lengths of the data's start from the local header. Throws an
// Error "bad zip: ..." for bytes that are not such a zip.
export function zipEntry(bytes) {
  const b = bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes);
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength);
  const u16 = (o) => v.getUint16(o, true), u32 = (o) => v.getUint32(o, true);
  let eocd = -1;
  for (let i = b.length - 22; i >= 0 && i >= b.length - 22 - 65535; i--) {
    if (u32(i) === 0x06054b50) {
      eocd = i;
      break;
    }
  }
  if (eocd < 0) throw new Error('bad zip: no end record');
  const cd = u32(eocd + 16);
  if (u16(eocd + 10) < 1) throw new Error('bad zip: no entry');
  if (cd + 46 > eocd || u32(cd) !== 0x02014b50) throw new Error('bad zip: no central directory');
  const flags = u16(cd + 8), method = u16(cd + 10), csize = u32(cd + 20), size = u32(cd + 24);
  const nameLen = u16(cd + 28), lo = u32(cd + 42);
  if (flags & 1) throw new Error('bad zip: encrypted');
  if (cd + 46 + nameLen > eocd) throw new Error('bad zip: no central directory');
  if (lo + 30 > cd || u32(lo) !== 0x04034b50) throw new Error('bad zip: no local header');
  const start = lo + 30 + u16(lo + 26) + u16(lo + 28);
  if (start + csize > cd) throw new Error('bad zip: the entry runs past its end');
  return { name: latin1(b.subarray(cd + 46, cd + 46 + nameLen)), method, size, data: b.subarray(start, start + csize) };
}

// The first entry's name and bytes, inflated (method 8) or stored (method 0). Rejects for a bad
// zip, a bad deflate stream, an entry over 1 MB, or an inflated size that is not the zip's.
export async function unzipFirst(bytes) {
  const e = zipEntry(bytes);
  if (e.size > MAX_TEXT) throw new Error('bad zip: the entry is over 1 MB');
  let out;
  if (e.method === 0) out = e.data;
  else if (e.method === 8) {
    const stream = new Response(e.data.slice()).body.pipeThrough(new DecompressionStream('deflate-raw'));
    out = new Uint8Array(await new Response(stream).arrayBuffer());
  } else throw new Error(`bad zip: method ${e.method}`);
  if (out.length !== e.size) throw new Error('bad zip: the size is not the entry\'s');
  return { name: e.name, bytes: out };
}

// Latin-1 bytes as text, each byte its code point. Not TextDecoder: its 'latin1' is windows-1252,
// which makes 0x80 to 0x9f other letters.
export function latin1(bytes) {
  let s = '';
  for (let i = 0; i < bytes.length; i += 4096) s += String.fromCharCode(...bytes.subarray(i, i + 4096));
  return s;
}

// A number field ("10.3", " 84", "-2"), or null.
function num(s) {
  const t = String(s ?? '').trim();
  return /^[-+]?\d+(\.\d+)?$/.test(t) ? Number(t) : null;
}

// The observation's time, "11-octubre-2026" and "23:00" in SMN_TZ, as Unix seconds; null when
// either is not a date or a time.
export function obsTime(date, time) {
  const d = /^(\d{1,2})-([a-z]+)-(\d{4})$/.exec(fold(String(date ?? '').trim()));
  const t = /^(\d{1,2}):(\d{2})$/.exec(String(time ?? '').trim());
  if (!d || !t) return null;
  const month = d[2] === 'setiembre' ? 8 : MONTHS.indexOf(d[2]);
  const day = Number(d[1]), hour = Number(t[1]), min = Number(t[2]);
  if (month < 0 || hour > 23 || min > 59) return null;
  const wall = Date.UTC(Number(d[3]), month, day, hour, min) / 1000;
  const back = new Date(wall * 1000);
  if (back.getUTCDate() !== day || back.getUTCMonth() !== month) return null;   // 31-septiembre, 0-octubre
  return wall - tzOffset(wall, SMN_TZ);
}

// One line of the file as {station, at, time, sky, temp, feels, humidity, wind}: at is the
// observation's Unix time, wind is km/h ("Calma" is 0), feels is the temperature when SMN writes
// "No se calcula" (no wind chill or heat index applies). A value that is missing or not a number
// is null. null for a line with no station name or fewer than 6 fields (no temperature).
export function parseRow(line) {
  const f = String(line ?? '').replace(/\s*\/\s*$/, '').split(';').map((x) => x.trim());
  if (f.length < 6 || !f[0]) return null;
  const temp = num(f[5]);
  const feels = /^no se calcula$/i.test(f[6] ?? '') ? temp : num(f[6]);
  const w = /(\d+)$/.exec(f[8] ?? '');
  const wind = /^calma$/i.test(f[8] ?? '') ? 0 : w ? Number(w[1]) : null;
  return { station: f[0], at: obsTime(f[1], f[2]), time: f[2], sky: f[3], temp, feels, humidity: num(f[7]), wind };
}

// The row of the station named exactly `station` after a trim, case and accents not counted
// ("Junin" finds "Jun\u00edn"), or null.
export function findRow(text, station) {
  const want = fold(String(station ?? '').trim());
  if (!want) return null;
  for (const line of String(text).split('\n')) {
    const row = parseRow(line);
    if (row && fold(row.station) === want) return row;
  }
  return null;
}
