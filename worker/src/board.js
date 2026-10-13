// The Board's reply for Couch, version 1 (the design: ~/.claude/plans/2026-10-10-couch-design.md,
// section 4): UTF-8 text that the PS3 parses with no JSON library. Line 1 is "couch-board 1",
// then key=value lines with "|" between the fields of a value, then "end". Pure functions: the
// Worker (index.js) gets the upstream data, this file only formats it. The now line comes from
// SMN's station (smn.js) when its observation is at most 3 h old, else from Open-Meteo; the days,
// the rain of the next 12 h and the sun always come from Open-Meteo, the moon from the date. Since
// 2026-10-13 also the money extras (argentinadatos.com) and the news (RSS, news.js): new keys, which
// a Couch 0.1.0 skips, so the version stays 1.

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

// Text for a comparison: lower case, the accents removed ("Jun\u00edn" and "JUNIN" give "junin").
export function fold(s) {
  return String(s ?? '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
}

// SMN's sky states by keyword, first match wins (on the folded text, so "chaparr\u00f3n" matches);
// "nevad" because SMN writes "Cubierto con nevada". Then "nublado" and "despejado" below.
const SKY = [
  ['tormenta', 'Thunderstorm'], ['niev', 'Snow'], ['nevad', 'Snow'], ['chaparron', 'Rain showers'], ['lluvia', 'Rain'],
  ['llovizna', 'Drizzle'], ['niebla', 'Fog'], ['neblina', 'Mist'], ['bruma', 'Haze'], ['humo', 'Smoke'], ['cubierto', 'Overcast'],
];

// An SMN sky state ("Cubierto con llovizna") in English words; an unknown state keeps its own
// text, cleaned and cut to 24 code points.
export function skyWords(state) {
  const s = fold(clean(state));
  for (const [k, w] of SKY) if (s.includes(k)) return w;
  if (s.includes('nublado')) return /(parcialmente|algo) nublado/.test(s) ? 'Partly cloudy' : 'Cloudy';
  if (s.includes('despejado')) return 'Clear';
  return Array.from(clean(state)).slice(0, 24).join('').trim() || '-';
}

export const SMN_MAX_AGE = 3 * 3600;
export const SMN_MAX_AHEAD = 3600;

// The SMN row for the now line, or null: a row with a temperature and an observation at most 3 h
// before now (and at most 1 h after: a date or a clock that is off).
export function smnNow(smn, now) {
  const r = smn && smn.data;
  if (!r || typeof r !== 'object' || !Number.isFinite(r.at) || !Number.isFinite(r.temp)) return null;
  const age = now - r.at;
  return age <= SMN_MAX_AGE && age >= -SMN_MAX_AHEAD ? r : null;
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

const NONE = { state: 'none', reason: '', age: 0, data: null };

// ---- the sky: the rain of the next 12 h, the sun, the moon --------------------------------------

// An hour is wet when its chance of precipitation is at least 40 % (where most weather apps start
// to show rain) or its precipitation is at least 0.2 mm (the smallest amount a rain gauge counts).
export const RAIN_PCT = 40;
export const RAIN_MM = 0.2;
export const RAIN_HOURS = 12;

// "2026-10-13T14:47": now's local date and time in tz, the form of Open-Meteo's hourly times.
function localStamp(t, tz) {
  const p = parts(t, tz);
  return `${p.year}-${p.month}-${p.day}T${p.hour}:${p.minute}`;
}

// The rain of the next 12 h from Open-Meteo's hourly arrays: {wet, words}, or null with no hourly
// data for that window. Open-Meteo stamps each hour at its end (the value at 15:00 is the chance
// and the sum of 14:00 to 15:00), so an hour counts when its end is after now and its start is
// before now + 12 h, by the times, not by the array's index (a cached forecast starts on an earlier
// day). The first wet hour gives the words: "Rain now" when it started before now, else "Rain from
// 14:00"; the percentage is the highest chance in the window ("0.4 mm" when no hour has a chance).
// With no wet hour: "No rain in the next 12 h" (fewer hours when the forecast ends sooner).
export function rainWords(hourly, now, tz) {
  const h = hourly;
  if (!h || !Array.isArray(h.time)) return null;
  const from = localStamp(now, tz), to = localStamp(now + (RAIN_HOURS + 1) * 3600, tz);
  const pp = Array.isArray(h.precipitation_probability) ? h.precipitation_probability : [];
  const mm = Array.isArray(h.precipitation) ? h.precipitation : [];
  let n = 0, first = -1, maxPct = -1, maxMm = 0;
  h.time.forEach((t, i) => {
    t = String(t);
    if (!/^\d{4}-\d{2}-\d{2}T\d{2}:00$/.test(t) || t <= from || t >= to) return;
    n++;
    const p = Number.isFinite(pp[i]) ? pp[i] : null, m = Number.isFinite(mm[i]) ? mm[i] : 0;
    if (p !== null) maxPct = Math.max(maxPct, p);
    maxMm = Math.max(maxMm, m);
    if (first < 0 && ((p !== null && p >= RAIN_PCT) || m >= RAIN_MM)) first = i;
  });
  if (!n) return null;
  if (first < 0) return { wet: false, words: `No rain in the next ${Math.min(n, RAIN_HOURS)} h` };
  // The wet hour's start: the hour before its stamp. It started before now exactly when it is
  // now's hour (its end is the first stamp after now).
  const hour = (Number(String(h.time[first]).slice(11, 13)) + 23) % 24;
  const pct = maxPct >= 0 ? `${Math.round(maxPct)}%` : `${Math.round(maxMm * 10) / 10} mm`;
  return {
    wet: true,
    words: hour === Number(from.slice(11, 13)) ? `Rain now (${pct})` : `Rain from ${String(hour).padStart(2, '0')}:00 (${pct})`,
  };
}

// Today's sunrise and sunset ("06:24", "19:16") from Open-Meteo's daily arrays, or null.
export function sunTimes(daily, today) {
  const d = daily;
  if (!d || !Array.isArray(d.time)) return null;
  const i = d.time.findIndex((x) => String(x) === today);
  const hm = (a) => {
    const m = Array.isArray(a) ? /T(\d{2}:\d{2})/.exec(String(a[i] ?? '')) : null;
    return m ? m[1] : null;
  };
  if (i < 0 || !hm(d.sunrise) || !hm(d.sunset)) return null;
  return { rise: hm(d.sunrise), set: hm(d.sunset) };
}

// The moon's phase from the date alone: the age since the new moon of 2000-01-06 18:14 UTC in
// synodic months (29.530588853 days), in eight phases of 3.7 days each, each centred on its
// moment (a new moon is within 1.85 days of one).
export const SYNODIC_DAYS = 29.530588853;
export const NEW_MOON_2000 = Date.UTC(2000, 0, 6, 18, 14) / 1000;
const MOON = ['New moon', 'Waxing crescent moon', 'First quarter moon', 'Waxing gibbous moon', 'Full moon',
  'Waning gibbous moon', 'Last quarter moon', 'Waning crescent moon'];

export function moonPhase(now) {
  const age = ((((now - NEW_MOON_2000) / 86400) % SYNODIC_DAYS) + SYNODIC_DAYS) % SYNODIC_DAYS;
  return MOON[Math.floor((age / SYNODIC_DAYS) * 8 + 0.5) % 8];
}

// ---- the money extras ---------------------------------------------------------------------------

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

// A number in the locale's format with at most `max` decimals (at least `min`), or "-".
function num(v, locale, min, max) {
  if (typeof v !== 'number' || !Number.isFinite(v)) return '-';
  return new Intl.NumberFormat(locale, { minimumFractionDigits: min, maximumFractionDigits: max }).format(v);
}

// "2026-10-12" as "Today" on now's local date, else "12 Oct"; "-" for a value that is not a date.
export function dayWords(ymd, now, tz) {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(ymd ?? ''));
  if (!m || +m[2] < 1 || +m[2] > 12) return '-';
  const p = parts(now, tz);
  return `${p.year}-${p.month}-${p.day}` === m[0] ? 'Today' : `${+m[3]} ${MONTHS[+m[2] - 1]}`;
}

// The money lines: riesgo pais (its date), the last monthly inflation (its month) and the
// fixed-term deposit rate of PF_BANK (TNA for clients; the list has no date, so the time of the
// Worker's read). money=label|value|date|state, and the reason after a stale or none state.
export function moneyLines({ now, tz, locale, rp, ipc, pf, pfLabel }) {
  const out = [];
  const line = (label, u, value, date) => {
    if (!u) return;
    if (u.state === 'none' || !u.data) out.push(`money=${label}|-|-|none|${clean(u.reason)}`);
    else out.push(`money=${label}|${value(u.data)}|${date(u)}|${u.state === 'ok' ? 'ok' : `stale|${clean(u.reason)}`}`);
  };
  line('Riesgo pa\u00eds', rp, (d) => num(d.valor, locale, 0, 0), (u) => dayWords(u.data.fecha, now, tz));
  line('Inflaci\u00f3n mensual', ipc, (d) => (Number.isFinite(d.valor) ? `${num(d.valor, locale, 1, 1)}%` : '-'), (u) => {
    const m = /^(\d{4})-(\d{2})/.exec(String(u.data.fecha ?? ''));
    return m && +m[2] >= 1 && +m[2] <= 12 ? `${MONTHS[+m[2] - 1]} ${m[1]}` : '-';
  });
  line(clean(pfLabel) || 'Plazo fijo', pf, (d) => (Number.isFinite(d.tna) ? `${num(d.tna * 100, locale, 0, 2)}%` : '-'),
    (u) => localWhen(now - Math.max(0, u.age | 0), now, tz));
  return out;
}

// ---- the news -----------------------------------------------------------------------------------

// The news lines from the feeds' results ([{label, weight, r}], r as get() gives it, r.data the
// items of news.js's parseFeed): first each feed's share, its weight (1 when absent) of its newest
// items, in the feeds' order; then one item per feed in turn until NEWS_MAX, so a feed with no
// items leaves its share to the others. Then the newest first. With weight 1 for every feed this
// is a fair share in turn. news=source|age|title, the age in seconds at now ("-" for an item with
// no date), then news_age (the oldest feed's) and news_state: ok when every feed is ok, else stale
// with the first failed feed's reason while any feed has items, else none.
export function newsLines(feeds, now, max) {
  if (!Array.isArray(feeds) || !feeds.length) return [];
  const lists = feeds.map(({ label, r }) => ((r && Array.isArray(r.data) ? r.data : [])
    .filter((x) => x && typeof x.title === 'string' && clean(x.title))
    .map((x) => ({ label: clean(label), title: clean(x.title), at: Number.isFinite(x.at) ? x.at : null }))
    .sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity))));
  const picked = [], taken = lists.map(() => 0);
  const take = (j, k) => {
    for (; k > 0 && taken[j] < lists[j].length && picked.length < max; k--) picked.push(lists[j][taken[j]++]);
  };
  feeds.forEach((f, j) => take(j, Number.isInteger(f.weight) && f.weight > 0 ? f.weight : 1));
  while (picked.length < max && lists.some((l, j) => taken[j] < l.length)) lists.forEach((_, j) => take(j, 1));
  picked.sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity));
  const out = picked.map((x) => `news=${x.label}|${x.at === null ? '-' : Math.max(0, now - x.at)}|${x.title}`);
  const rs = feeds.map((f) => f.r || NONE);
  const bad = rs.find((r) => r.state !== 'ok');
  const age = Math.max(0, ...rs.filter((r) => r.data).map((r) => r.age | 0));
  out.push(`news_age=${age}`, `news_state=${!bad ? 'ok' : `${picked.length ? 'stale' : 'none'}|${clean(bad.reason)}`}`);
  return out;
}

// The reply. usd, wx and smn are {state: 'ok'|'stale'|'none', reason, age, data}, as index.js's
// get() gives them: data is dolarapi's /v1/dolares array, Open-Meteo's forecast object and the
// SMN station's row (smn.js's parseRow). smn has no state line: its row's own time decides.
// money is {rp, ipc, pf}, each such a result or absent (no line); news is [{label, weight, r}], one
// per feed of NEWS_FEEDS (none: no news lines).
export function boardText({ now, usd, wx, smn = NONE, money: extras = {}, news = [], env }) {
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
  out.push(...moneyLines({ now, tz, locale, ...extras, pfLabel: env.PF_LABEL }));
  const c = wx.data && wx.data.current, dy = wx.data && wx.data.daily;
  const deg = (v) => (Number.isFinite(v) ? `${Math.round(v)}°` : '-');
  const pct = (v) => (Number.isFinite(v) ? `${Math.round(v)}%` : '-');
  const kmh = (v) => (Number.isFinite(v) ? `${Math.round(v)} km/h` : '-');
  // The 7th field names the now line's source (Couch's footer). A Couch of before 2026-10-12 splits
  // wx_now into 6 fields, and the 6th ends at its "|": it ignores the 7th.
  const row = smnNow(smn, now);
  if (row) {
    // Day or night: Open-Meteo's is_day when it answered (ok: within WX_TTL), else now's local hour.
    const om = wx.state === 'ok' && c && (c.is_day === 0 || c.is_day === 1);
    const h = Number(parts(now, tz).hour), at = parts(row.at, tz);
    const day = om ? c.is_day === 1 : h >= 6 && h < 20;
    out.push(`wx_now=${deg(row.temp)}|${deg(row.feels)}|${pct(row.humidity)}|${skyWords(row.sky)}|${kmh(row.wind)}|` +
      `${day ? 'day' : 'night'}|SMN ${at.hour}:${at.minute}`);
  } else if (c) {
    out.push(`wx_now=${deg(c.temperature_2m)}|${deg(c.apparent_temperature)}|${pct(c.relative_humidity_2m)}|` +
      `${wmoWords(c.weather_code)}|${kmh(c.wind_speed_10m)}|${c.is_day ? 'day' : 'night'}|Open-Meteo`);
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
  // The sky, from the same forecast: the rain of the next 12 h (wx_rain=rain|words or dry|words),
  // today's sunrise and sunset, and the moon from the date (also with no forecast).
  if (wx.data) {
    const p = parts(now, tz), rain = rainWords(wx.data.hourly, now, tz), sun = sunTimes(dy, `${p.year}-${p.month}-${p.day}`);
    if (rain) out.push(`wx_rain=${rain.wet ? 'rain' : 'dry'}|${rain.words}`);
    if (sun) out.push(`wx_sun=${sun.rise}|${sun.set}`);
  }
  out.push(`wx_moon=${moonPhase(now)}`);
  out.push(`wx_age=${Math.max(0, wx.age | 0)}`, `wx_state=${stateLine(wx)}`);
  out.push(...newsLines(news, now, Math.min(8, Number(env.NEWS_MAX) || 8)), 'end');
  let text = out.join('\n') + '\n';
  // A body over MAX_BYTES would be refused whole by a Couch 0.1.0 ("the reply is over 4 KB": its
  // buffer is 4 KB) and cut by a newer one, so lines drop from the end until it fits: the news
  // first, then the money and the sky lines (a Couch 0.1.0 skips all three, so its Board stays
  // as it was), then the rate and day lines.
  const drop = [['news='], ['money='], ['wx_rain=', 'wx_sun=', 'wx_moon='], ['usd=', 'day=']];
  for (const keys of drop) {
    while (new TextEncoder().encode(text).length > MAX_BYTES) {
      const i = out.findLastIndex((l) => keys.some((k) => l.startsWith(k)));
      if (i < 0) break;
      out.splice(i, 1);
      text = out.join('\n') + '\n';
    }
  }
  return text;
}
