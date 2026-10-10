// The Board's reply for Couch, version 1 (the design: ~/.claude/plans/2026-10-10-couch-design.md,
// section 4): UTF-8 text that the PS3 parses with no JSON library. Line 1 is "couch-board 1",
// then key=value lines with "|" between the fields of a value, then "end". Pure functions: the
// Worker (index.js) gets the upstream data, this file only formats it.

export const VERSION = 1;
export const MAX_BYTES = 4096;

const WMO = [
  [0, 0, 'Clear'], [1, 1, 'Mainly clear'], [2, 2, 'Partly cloudy'], [3, 3, 'Overcast'],
  [45, 48, 'Fog'], [51, 57, 'Drizzle'], [61, 67, 'Rain'], [71, 77, 'Snow'],
  [80, 82, 'Rain showers'], [85, 86, 'Snow showers'], [95, 99, 'Thunderstorm'],
];

// A WMO weather code in words (Open-Meteo's weather_code).
export function wmoWords(code) {
  for (const [a, b, w] of WMO) if (code >= a && code <= b) return w;
  return 'Unknown';
}

// Upstream text for a field: no "|", no control character, no space at the ends.
export function clean(s) {
  return String(s ?? '').replace(/[|\u0000-\u001f\u007f]/g, ' ').replace(/ +/g, ' ').trim();
}

// A price with two decimals in the locale's format ("1.535,00" in es-AR), or "-".
export function money(v, locale) {
  if (typeof v !== 'number' || !Number.isFinite(v)) return '-';
  return new Intl.NumberFormat(locale, { minimumFractionDigits: 2, maximumFractionDigits: 2 }).format(v);
}

function parts(t, tz) {
  const f = new Intl.DateTimeFormat('en-US', {
    timeZone: tz, year: 'numeric', month: '2-digit', day: '2-digit', weekday: 'short',
    hour: '2-digit', minute: '2-digit', hourCycle: 'h23',
  });
  const p = {};
  for (const x of f.formatToParts(new Date(t * 1000))) p[x.type] = x.value;
  return p;
}

// The local time of t (Unix seconds) in tz: "HH:MM" on now's local day, else "Ddd HH:MM".
export function localWhen(t, now, tz) {
  const a = parts(t, tz), b = parts(now, tz);
  const hm = `${a.hour}:${a.minute}`;
  return a.year === b.year && a.month === b.month && a.day === b.day ? hm : `${a.weekday} ${hm}`;
}

// tz's offset from UTC at now, in seconds (-10800 for Buenos Aires).
export function tzOffset(now, tz) {
  const p = parts(now, tz);
  const local = Date.UTC(+p.year, +p.month - 1, +p.day, +p.hour, +p.minute) / 1000;
  return Math.round((local - (now - (now % 60))) / 60) * 60;
}

// "oficial:Oficial,blue:Blue" as [{casa, label}], in that order.
export function parseCasas(spec) {
  return String(spec || '').split(',').map((x) => x.split(':')).filter((x) => x.length === 2 && x[0])
    .map(([casa, label]) => ({ casa: casa.trim(), label: clean(label) }));
}

function stateLine(u) {
  return u.state === 'ok' ? 'ok' : `${u.state}|${clean(u.reason)}`;
}

// The reply. usd and wx are {state: 'ok'|'stale'|'none', reason, age, data}, as index.js's get()
// gives them: data is dolarapi's /v1/dolares array and Open-Meteo's forecast object.
export function boardText({ now, usd, wx, env }) {
  const tz = env.TZ || 'UTC', locale = env.LOCALE || 'es-AR';
  // The offset at now, not Open-Meteo's utc_offset_seconds: a cached forecast keeps the offset of
  // its fetch, which is wrong after a DST change. The city is cut to 100 code points, the only
  // line that the 4 KB guard below cannot drop.
  const offset = tzOffset(now, tz);
  const city = Array.from(clean(env.CITY || '')).slice(0, 100).join('').trim();
  const out = [`couch-board ${VERSION}`, `now=${now}`, `offset=${offset}`, `city=${city}`];
  if (Array.isArray(usd.data)) {
    const byCasa = new Map(usd.data.filter((d) => d !== null && typeof d === 'object').map((d) => [d.casa, d]));
    for (const { casa, label } of parseCasas(env.CASAS).slice(0, 8)) {
      const d = byCasa.get(casa);
      if (!d) continue;
      const t = Date.parse(d.fechaActualizacion);
      const when = Number.isFinite(t) ? localWhen(Math.floor(t / 1000), now, tz) : '-';
      out.push(`usd=${label}|${money(d.compra, locale)}|${money(d.venta, locale)}|${when}`);
    }
  }
  out.push(`usd_age=${Math.max(0, usd.age | 0)}`, `usd_state=${stateLine(usd)}`);
  const c = wx.data && wx.data.current, dy = wx.data && wx.data.daily;
  if (c) {
    const deg = (v) => (Number.isFinite(v) ? `${Math.round(v)}°` : '-');
    out.push(`wx_now=${deg(c.temperature_2m)}|${deg(c.apparent_temperature)}|` +
      `${Number.isFinite(c.relative_humidity_2m) ? `${Math.round(c.relative_humidity_2m)}%` : '-'}|` +
      `${wmoWords(c.weather_code)}|${Number.isFinite(c.wind_speed_10m) ? `${Math.round(c.wind_speed_10m)} km/h` : '-'}|` +
      `${c.is_day ? 'day' : 'night'}`);
  }
  if (dy && Array.isArray(dy.time)) {
    // Open-Meteo starts `daily` on the local date of its fetch, so a cached forecast can start on
    // a past day: skip the days before now's local date and any value that is not a date. i stays
    // each day's index into the other daily arrays.
    const p = parts(now, tz), today = `${p.year}-${p.month}-${p.day}`;
    const days = dy.time.map((d, i) => [String(d), i])
      .filter(([d]) => /^\d{4}-\d{2}-\d{2}$/.test(d) && Number.isFinite(Date.parse(`${d}T12:00:00Z`)) && d >= today)
      .slice(0, 3);
    for (const [d, i] of days) {
      const label = d === today ? 'Today'
        : new Intl.DateTimeFormat('en-US', { timeZone: 'UTC', weekday: 'short' }).format(new Date(`${d}T12:00:00Z`));
      const num = (a, unit) => (a && Number.isFinite(a[i]) ? `${Math.round(a[i])}${unit}` : '-');
      out.push(`day=${label}|${wmoWords(dy.weather_code && dy.weather_code[i])}|${num(dy.temperature_2m_max, '°')}|` +
        `${num(dy.temperature_2m_min, '°')}|${num(dy.precipitation_probability_max, '%')}`);
    }
  }
  out.push(`wx_age=${Math.max(0, wx.age | 0)}`, `wx_state=${stateLine(wx)}`, 'end');
  let text = out.join('\n') + '\n';
  // A body over MAX_BYTES would be cut by Couch's 4 KB buffer: drop rate and day lines from the end
  // until it fits.
  while (new TextEncoder().encode(text).length > MAX_BYTES) {
    const i = out.findLastIndex((l) => l.startsWith('usd=') || l.startsWith('day='));
    if (i < 0) break;
    out.splice(i, 1);
    text = out.join('\n') + '\n';
  }
  return text;
}
