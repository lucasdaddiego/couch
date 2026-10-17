// The fútbol of Couch 0.5.0 (since 2026-10-16): the Board's futbol line and the reply of GET
// /futbol, pure functions over one normalized day. The day comes from a source of the Worker
// (none in the template): { day: 'YYYY-MM-DD', leagues: [{ name, country, games: [{ start (Unix
// seconds), kind ('pre', 'live', 'end' or 'off'), status (the words of a live, ended or called-off
// game), home, away, homeShort, awayShort, sh, sa (whole goals or null), tv ('' for none) }] }] }.
// The leagues and the games keep the source's order.

import { addDays, clean, dateFmt, localDate } from './board.js';

export const FUTBOL_VERSION = 1;
// The /futbol reply, at most (Couch's FUTBOL_MAX). A busy Saturday of a real source (87 games in 17
// leagues) made 5.8 KB: 16 KB holds about 240 games, Couch's FUT_GAMES.
export const FUTBOL_BYTES = 16384;
export const FUT_MAX_AGE = 1800;         // the Board's line: data at most 30 min old
export const KINDS = ['pre', 'live', 'end', 'off'];
const NAME_CP = 28, TV_CP = 40, LEAGUE_CP = 48, COUNTRY_CP = 32;
const MORE_ROOM = 24;                    // "more=<leagues>|<games>" and its line end, at most
const ENC = new TextEncoder();

// Upstream text cut to n code points, "-" when nothing is left.
export function cut(s, n) {
  return Array.from(clean(s)).slice(0, n).join('').trim() || '-';
}

// The channels, whole names only, as many as fit in n code points; a
// name's "(...)" note goes ("Kanal 9 (Fußball-Paket)" is "Kanal 9"), before
// the split, so a note with commas goes whole. One name longer than n is cut.
export function tvCut(s, n) {
  const names = clean(s).replace(/\s*\([^)]*\)?/g, '').split(',').map((x) => x.trim()).filter(Boolean);
  let out = '';
  for (const x of names) {
    const next = out ? `${out}, ${x}` : x;
    if (Array.from(next).length > n) break;
    out = next;
  }
  return out || cut(names[0] || '', n);
}

// t (Unix seconds) as the local "HH:MM" in tz, on any day.
export function hm(t, tz) {
  const p = {};
  for (const x of dateFmt('en-US', { timeZone: tz, hour: '2-digit', minute: '2-digit', hourCycle: 'h23' })
    .formatToParts(new Date(t * 1000))) p[x.type] = x.value;
  return `${p.hour}:${p.minute}`;
}

// "2026-10-16" as "Fri 16 Oct".
export function dayName(ymd) {
  const d = new Date(`${ymd}T12:00:00Z`);
  const f = (o) => dateFmt('en-GB', { timeZone: 'UTC', ...o }).format(d);
  return `${f({ weekday: 'short' })} ${f({ day: 'numeric' })} ${f({ month: 'short' })}`;
}

const games = (data) => (data && Array.isArray(data.leagues) ? data.leagues : [])
  .flatMap((l) => (l && Array.isArray(l.games) ? l.games : [])).filter((g) => g && KINDS.includes(g.kind));

// futbol=live|words for the Board from today's day (r as get() gives it): the games live now and
// "2 live", else the next game that has not started ("Next: Home–Away 21:00", the short names, the
// earliest start, the first in the list for a tie), else "No more games today". null with no
// game, no data, data of another day or data older than FUT_MAX_AGE.
export function futbolLine(r, now, tz) {
  const d = r && r.data;
  if (!d || d.day !== localDate(now, tz) || !(Math.max(0, r.age | 0) <= FUT_MAX_AGE)) return null;
  const all = games(d);
  if (!all.length) return null;
  const live = all.filter((g) => g.kind === 'live').length;
  let next = null;
  for (const g of all) if (g.kind === 'pre' && Number.isFinite(g.start) && (!next || g.start < next.start)) next = g;
  const words = live ? `${live} live`
    : next ? `Next: ${cut(next.homeShort || next.home, NAME_CP)}–${cut(next.awayShort || next.away, NAME_CP)} ${hm(next.start, tz)}`
      : 'No more games today';
  return `futbol=${live}|${words}`;
}

// A goal count as text, "-" for none.
const goals = (v) => (Number.isInteger(v) && v >= 0 && v <= 999 ? String(v) : '-');

// game=when|home|away|sh|sa|tv|kind: when is the local start of a game to come, else its status.
export function gameLine(g, tz) {
  const when = g.kind === 'pre' ? (Number.isFinite(g.start) ? hm(g.start, tz) : '-') : cut(g.status, 16);
  const score = g.kind === 'live' || g.kind === 'end';
  return `game=${when}|${cut(g.home, NAME_CP)}|${cut(g.away, NAME_CP)}|${score ? goals(g.sh) : '-'}|${score ? goals(g.sa) : '-'}|` +
    `${tvCut(g.tv, TV_CP)}|${g.kind}`;
}

// The reply of GET /futbol for day d (0 today, 1 tomorrow, by tz) from r, get()'s result whose data
// is the normalized day: "couch-futbol 1", now, day (words), per league a league line and its game
// lines, then futbol_age, futbol_state and end. At most FUTBOL_BYTES: whole leagues from the start,
// and a more=leagues|games line for the ones left out. A league with no game has no line.
export function futbolText({ now, tz, d, r }) {
  const ymd = addDays(localDate(now, tz), d === 1 ? 1 : 0);
  const head = [`couch-futbol ${FUTBOL_VERSION}`, `now=${now}`, `day=${d === 1 ? 'Tomorrow' : 'Today'}, ${dayName(ymd)}`];
  const st = r && r.state === 'ok' ? 'ok' : `${(r && r.state) || 'none'}|${clean(r && r.reason)}`;
  const tail = [`futbol_age=${Math.max(0, (r && r.age) | 0)}`, `futbol_state=${st}`, 'end'];
  const size = (lines) => lines.reduce((n, l) => n + ENC.encode(l).length + 1, 0);
  const data = r && r.data && r.data.day === ymd ? r.data : null;
  const blocks = (data && Array.isArray(data.leagues) ? data.leagues : []).map((l) => {
    const gs = (l && Array.isArray(l.games) ? l.games : []).filter((g) => g && KINDS.includes(g.kind));
    if (!gs.length) return null;
    const lines = [`league=${cut(l.name, LEAGUE_CP)}|${cut(l.country, COUNTRY_CP)}`, ...gs.map((g) => gameLine(g, tz))];
    return { n: gs.length, lines, bytes: size(lines) };              // each line measured once
  }).filter(Boolean);
  const room = FUTBOL_BYTES - size(head) - size(tail);
  const out = [...head];
  let used = 0, k = 0;
  const all = blocks.reduce((n, b) => n + b.bytes, 0);
  for (; k < blocks.length; k++) {
    if (used + blocks[k].bytes > (all <= room ? room : room - MORE_ROOM)) break;
    out.push(...blocks[k].lines);
    used += blocks[k].bytes;
  }
  if (k < blocks.length) out.push(`more=${blocks.length - k}|${blocks.slice(k).reduce((n, b) => n + b.n, 0)}`);
  return [...out, ...tail, ''].join('\n');
}
