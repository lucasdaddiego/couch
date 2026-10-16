// The Board's reply for Couch, version 1: UTF-8 text that the PS3 parses with no JSON library.
// Line 1 is "couch-board 1", then key=value lines with "|" between the fields of a value, then
// "end" (README.md, "The Board reply"). Pure functions: the Worker (index.js) gets the upstream
// data, this file only formats it. The weather comes from Open-Meteo, the moon from the date, the
// rates from Frankfurter (the ECB's reference rates), the holidays from Nager.Date, the news from
// RSS feeds (news.js), the air from Open-Meteo's air quality, the fútbol from futbol.js's day. Couch
// skips a key it does not know.

import { futbolLine } from './futbol.js';

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

// Text for a comparison: lower case, the accents removed ("M\u00fcnchen" and "MUNCHEN" give "munchen").
export function fold(s) {
  return String(s ?? '').normalize('NFD').replace(/[\u0300-\u036f]/g, '').toLowerCase();
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

// The rain of the next 2 h from Open-Meteo's minutely_15 values (since Couch 0.5.0): the words, or
// null. Open-Meteo stamps each 15 minutes at its end (the value at 18:45 is the sum of 18:30 to
// 18:45), so an interval counts when its end is after now and its start is before now + 2 h, by
// the times (as rainWords). An interval is wet from SOON_MM. The words: "Rain until 19:30" (the
// start of the first dry interval) when the interval of now is wet, "Rain for the next 2 h" when
// none of them is dry; else "Rain from 18:45" (the start of the first wet one). null when every
// interval is dry, or when the values do not cover now to now + 2 h (an old forecast).
export const SOON_MM = 0.1;
export const SOON_S = 7200;
export function soonWords(m15, now, tz) {
  if (!m15 || !Array.isArray(m15.time)) return null;
  const mm = Array.isArray(m15.precipitation) ? m15.precipitation : [];
  const wall = Date.parse(`${localStamp(now, tz)}:00Z`) + (now % 60) * 1000, step = 900000;   // the wall clock, as UTC
  const win = [];
  m15.time.forEach((t, i) => {
    const end = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}$/.test(String(t)) ? Date.parse(`${t}:00Z`) : NaN;
    if (end > wall && end - step < wall + SOON_S * 1000) win.push({ start: end - step, wet: Number.isFinite(mm[i]) && mm[i] >= SOON_MM });
  });
  win.sort((a, b) => a.start - b.start);
  if (!win.length || win[0].start > wall || win.at(-1).start + step < wall + SOON_S * 1000) return null;
  const at = (ms) => new Date(ms).toISOString().slice(11, 16);
  if (win[0].wet) {
    const dry = win.find((x) => !x.wet);
    return dry ? `Rain until ${at(dry.start)}` : 'Rain for the next 2 h';
  }
  const wet = win.find((x) => x.wet);
  return wet ? `Rain from ${at(wet.start)}` : null;
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

// holiday=YYYY-MM-DD|name|days: the next national holiday on or after now's local date (index.js's
// hol: its list, sorted), days 0 for today; none when the list holds none (or a stale one ends).
export function holidayLine(hol, now, tz) {
  const l = hol && Array.isArray(hol.data) ? hol.data : null;
  if (!l) return null;
  const today = localDate(now, tz), next = l.find((x) => x && x.fecha >= today);
  if (!next) return null;
  const days = Math.round((Date.parse(`${next.fecha}T12:00:00Z`) - Date.parse(`${today}T12:00:00Z`)) / 86400000);
  return `holiday=${next.fecha}|${clean(next.nombre).replaceAll('|', '/')}|${days}`;
}

// ---- dates, numbers and the weather's codes ----------------------------------------------------
// New keys only, so the version stays 1. Numbers in the C form (a dot for decimals, no thousands
// mark), "-" for a missing one; only the changes' pct and abs are LOCALE text with a sign.

// "2026-10-13" n days later (n < 0: before), by the calendar.
export function addDays(ymd, n) {
  return new Date(Date.parse(`${ymd}T12:00:00Z`) + n * 86400000).toISOString().slice(0, 10);
}

// now's local date in tz, "2026-10-13".
export function localDate(now, tz) {
  const p = parts(now, tz);
  return `${p.year}-${p.month}-${p.day}`;
}

// "2026-10-12" as "12 Oct" (a rate's date), "-" for a value that is not a date.
export function shortDate(ymd) {
  const m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(ymd ?? ''));
  return m && +m[2] >= 1 && +m[2] <= 12 ? `${+m[3]} ${MONTHS[+m[2] - 1]}` : '-';
}

// A whole number in lo..hi (rounded), else "-".
function whole(v, lo, hi) {
  if (typeof v !== 'number' || !Number.isFinite(v)) return '-';
  const r = Math.round(v);
  return r >= lo && r <= hi ? String(r === 0 ? 0 : r) : '-';      // no "-0"
}

// A WMO code (0 to 99, whole), else "-".
function wmoCode(v) {
  return Number.isInteger(v) && v >= 0 && v <= 99 ? String(v) : '-';
}

// air=aqi|uv from Open-Meteo's air quality (since Couch 0.5.0): the US AQI and the UV index now,
// whole, "-" for a missing one. null when the data is older than AIR_MAX_AGE (3 h) or holds
// neither value.
export const AIR_MAX_AGE = 10800;
export function airLine(air) {
  const c = air && air.data && air.data.current;
  if (!c || !(Math.max(0, air.age | 0) <= AIR_MAX_AGE)) return null;
  const aqi = whole(c.us_aqi, 0, 999), uv = whole(c.uv_index, 0, 30);
  return aqi === '-' && uv === '-' ? null : `air=${aqi}|${uv}`;
}

// geo=lat|lon from LAT and LON (4 decimals, about 11 m), or null when either is not a number in
// range.
export function geoLine(env) {
  const n = (s, max) => {
    const t = String(s ?? '').trim();
    const v = /^[-+]?\d+(\.\d+)?$/.test(t) ? Number(t) : NaN;
    return Math.abs(v) <= max ? String(Math.round(v * 1e4) / 1e4 || 0) : null;
  };
  const la = n(env.LAT, 90), lo = n(env.LON, 180);
  return la !== null && lo !== null ? `geo=${la}|${lo}` : null;
}

// wx_fx=code|cover|mm|dir|speed|gusts from Open-Meteo's current values. mm is the rate in mm per
// hour: Open-Meteo's current precipitation is the sum of its interval (900 s), so it is scaled to
// an hour (Couch counts its rain drops from an hourly rate). dir is where the wind comes from, 0
// to 359. null with no current values.
export function fxLine(c) {
  if (!c) return null;
  const code = wmoCode(c.weather_code);
  const p = c.precipitation, iv = Number.isFinite(c.interval) && c.interval > 0 ? c.interval : 3600;
  const rate = typeof p === 'number' && Number.isFinite(p) && p >= 0 ? Math.round((p * 3600) / iv * 10) / 10 : NaN;
  const mm = rate >= 0 && rate <= 1000 ? String(rate) : '-';
  const d = c.wind_direction_10m, dir = whole(d, 0, 360) === '-' ? '-' : String(Math.round(d) % 360);
  return `wx_fx=${code}|${whole(c.cloud_cover, 0, 100)}|${mm}|${dir}|${whole(c.wind_speed_10m, 0, 500)}|${whole(c.wind_gusts_10m, 0, 500)}`;
}

export const HOURS = 12;

// wx_hours=start|t1 .. t12|p1 .. p12|c1 .. c12 from Open-Meteo's hourly arrays: the 12 hours
// from the first whole hour after now (local), by the times, not the index (as rainWords): each
// list holds the value at that hour's stamp ("-" for an hour the forecast does not hold).
// Temperatures in deg C, whole; chances in %; WMO codes. null when no hour has a temperature.
export function hoursLine(h, now, tz) {
  if (!h || !Array.isArray(h.time)) return null;
  const from = localStamp(now, tz);
  const first = Date.parse(`${from.slice(0, 13)}:00:00Z`) + 3600000;   // the wall clock, as UTC
  const at = new Map(h.time.map((t, i) => [String(t), i]));
  const arr = (k) => (Array.isArray(h[k]) ? h[k] : []);
  const temps = [], pops = [], codes = [];
  for (let k = 0; k < HOURS; k++) {
    const i = at.get(new Date(first + k * 3600000).toISOString().slice(0, 16));
    temps.push(i === undefined ? '-' : whole(arr('temperature_2m')[i], -90, 70));
    pops.push(i === undefined ? '-' : whole(arr('precipitation_probability')[i], 0, 100));
    codes.push(i === undefined ? '-' : wmoCode(arr('weather_code')[i]));
  }
  if (temps.every((t) => t === '-')) return null;
  return `wx_hours=${new Date(first).getUTCHours()}|${temps.join(' ')}|${pops.join(' ')}|${codes.join(' ')}`;
}

// The change of cur against prev, compared at `dec` decimals (4 for most rates, 0 for an
// index): {dir, pct, abs}, dir up, down or flat (equal at those decimals), pct with 1 decimal (2
// when 1 would show 0,0 for a change that is not 0) and abs with `dec`, both in the locale with a
// sign ("+0,3%", "-3,29"; flat: "0,0%", "0,00"). null when either value is missing.
export function change(cur, prev, dec, locale) {
  if (typeof cur !== 'number' || typeof prev !== 'number' || !Number.isFinite(cur) || !Number.isFinite(prev) || prev <= 0) return null;
  const k = 10 ** dec, d = Math.round(cur * k) - Math.round(prev * k);
  const sign = d > 0 ? '+' : d < 0 ? '-' : '';
  const p = (Math.abs(d) / k / prev) * 100, pd = d !== 0 && Math.round(p * 10) === 0 ? 2 : 1;
  return {
    dir: d > 0 ? 'up' : d < 0 ? 'down' : 'flat',
    pct: `${sign}${num(p, locale, pd, pd)}%`,
    abs: `${sign}${num(Math.abs(d) / k, locale, dec, dec)}`,
  };
}


// ---- the rates ----------------------------------------------------------------------------------

// RATES: "USD,GBP:Pound,JPY", at most 8 ISO codes, each with an optional label (the code when
// absent). Codes Frankfurter does not know give no line.
export const RATES_MAX = 8;
export function parseRates(spec) {
  return String(spec || '').split(',').map((x) => x.split(':')).map(([code, label]) => [String(code || '').trim().toUpperCase(), clean(label)])
    .filter(([code]) => /^[A-Z]{3}$/.test(code)).slice(0, RATES_MAX).map(([code, label]) => ({ code, label: label || code }));
}

// The symbol of a currency for the hero tile, "" when none is known (Couch then shows no sign).
const SYMBOLS = { USD: '$', CAD: '$', AUD: '$', NZD: '$', MXN: '$', HKD: '$', SGD: '$', EUR: '\u20ac', GBP: '\u00a3', JPY: '\u00a5', CNY: '\u00a5', CHF: 'Fr', INR: '\u20b9', KRW: '\u20a9', TRY: '\u20ba', ILS: '\u20aa', PLN: 'z\u0142', SEK: 'kr', NOK: 'kr', DKK: 'kr', BRL: 'R$', ZAR: 'R' };
export const currencySymbol = (code) => SYMBOLS[code] || '';

// The decimals of a rate: 2 from 100 up, 3 from 10, else 4 (0.9234 EUR a dollar, 1.2345 a pound).
export const rateDec = (v) => (v >= 100 ? 2 : v >= 10 ? 3 : 4);

export const SPARK_DAYS = 30;

// The rate lines (since Couch 0.4.0) from fx.data {dates, rates: {code: [value per date]}} (index.js's
// fx: CURRENCY per one unit of each code, the ECB's working days, oldest first):
// - rate=label|-|value|date per code of RATES, the latest value (a mid rate: no buy price); a
//   value under 0.1 is given per 100 units (under 0.01 per 1000), "100 JPY" the label;
// - feature=label|symbol: FEATURE's code (else the first rate), its symbol;
// - rate_chg=label|dir|pct|abs|date: the latest value against the day before it;
// - spark=label|yyyy-mm-dd|v1 .. v30|dec: the feature's value at each of the 30 days up to the
//   latest date, times 10^dec, "-" for a day with none (weekends, holidays);
// - rate_age and rate_state.
export function fxLines({ fx, env, locale }) {
  const out = [], d = fx && fx.data, rates = parseRates(env.RATES);
  const shown = [];
  if (d && Array.isArray(d.dates) && d.dates.length && d.rates) {
    const last = d.dates.length - 1, date = d.dates[last];
    for (const r of rates) {
      const raw = Array.isArray(d.rates[r.code]) ? d.rates[r.code] : null;
      if (!raw || !Number.isFinite(raw[last]) || raw[last] <= 0) continue;
      // a small unit (a yen is 0.0056 euros) shows per 100 or 1000 of them, in the label
      const unit = raw[last] < 0.01 ? 1000 : raw[last] < 0.1 ? 100 : 1, v = raw.map((x) => (Number.isFinite(x) ? x * unit : x));
      const label = unit > 1 ? `${unit} ${r.label}` : r.label;
      out.push(`rate=${label}|-|${num(v[last], locale, rateDec(v[last]), rateDec(v[last]))}|${shortDate(date)}`);
      shown.push({ ...r, label, v });
    }
    const fc = String(env.FEATURE || '').trim().toUpperCase(), f = shown.find((r) => r.code === fc) || shown[0];
    if (f) out.push(`feature=${f.label}|${currencySymbol(f.code)}`);
    for (const r of shown) {
      const g = last > 0 ? change(r.v[last], r.v[last - 1], rateDec(r.v[last]), locale) : null;
      if (g) out.push(`rate_chg=${r.label}|${g.dir}|${g.pct}|${g.abs}|${shortDate(d.dates[last - 1])}`);
    }
    if (f) {
      const dec = rateDec(f.v[last]), by = new Map(d.dates.map((x, i) => [x, f.v[i]])), start = addDays(date, 1 - SPARK_DAYS);
      const vals = Array.from({ length: SPARK_DAYS }, (_, k) => by.get(addDays(start, k)))
        .map((x) => (Number.isFinite(x) ? String(Math.round(x * 10 ** dec)) : '-'));
      if (vals.filter((x) => x !== '-').length >= 2) out.push(`spark=${f.label}|${start}|${vals.join(' ')}|${dec}`);
    }
  }
  if (rates.length) out.push(`rate_age=${Math.max(0, (fx && fx.age) | 0)}`, `rate_state=${stateLine(fx || NONE)}`);
  return out;
}

// numfmt=<thousands><decimal>: LOCALE's marks for Couch's number parser, "_" for a thousands mark
// that is not one ASCII character (a narrow no-break space in fr-FR: Couch then shows the value
// as text and writes its own numbers without one).
export function numfmtLine(locale) {
  const p = new Intl.NumberFormat(locale).formatToParts(12345.6);
  const g = (p.find((x) => x.type === 'group') || {}).value || '', dc = (p.find((x) => x.type === 'decimal') || {}).value || '.';
  const ascii = (c) => c.length === 1 && c.charCodeAt(0) > 32 && c.charCodeAt(0) < 127;
  return `numfmt=${ascii(g) ? g : '_'}${ascii(dc) ? dc : '.'}`;
}

// ---- the news -----------------------------------------------------------------------------------

// The news lines from the feeds' results ([{label, weight, r}], r as get() gives it, r.data the
// items of news.js's parseFeed): first each feed's share, its weight (1 when absent) of its newest
// items, in the feeds' order; then one item per feed in turn until NEWS_MAX, so a feed with no
// items leaves its share to the others. Then the newest first. With weight 1 for every feed this
// is a fair share in turn. news=source|age|title, the age in seconds at now ("-" for an item with
// no date), then news_age (the oldest feed's) and news_state: ok when every feed is ok, else stale
// with the first failed feed's reason while any feed has items, else none. The
// shares take only the items of the last `fresh` seconds (NEWS_FRESH, 6 h: a slow feed
// keeps no stale headline on the Board); older items and items with no date fill the places left, newest first.
export const NEWS_FRESH = 21600;
export function newsLines(feeds, now, max, fresh = NEWS_FRESH) {
  if (!Array.isArray(feeds) || !feeds.length) return [];
  const all = feeds.map(({ label, r }) => ((r && Array.isArray(r.data) ? r.data : [])
    .filter((x) => x && typeof x.title === 'string' && clean(x.title))
    .map((x) => ({ label: clean(label), title: clean(x.title), at: Number.isFinite(x.at) ? x.at : null }))
    .sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity))));
  const isFresh = (x) => x.at !== null && now - x.at <= fresh;
  const lists = all.map((l) => l.filter(isFresh));
  const picked = [], taken = lists.map(() => 0);
  const take = (j, k) => {
    for (; k > 0 && taken[j] < lists[j].length && picked.length < max; k--) picked.push(lists[j][taken[j]++]);
  };
  feeds.forEach((f, j) => take(j, Number.isInteger(f.weight) && f.weight > 0 ? f.weight : 1));
  while (picked.length < max && lists.some((l, j) => taken[j] < l.length)) lists.forEach((_, j) => take(j, 1));
  const old = all.flat().filter((x) => !isFresh(x)).sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity));
  picked.push(...old.slice(0, Math.max(0, max - picked.length)));
  picked.sort((a, b) => (b.at ?? -Infinity) - (a.at ?? -Infinity));
  const out = picked.map((x) => `news=${x.label}|${x.at === null ? '-' : Math.max(0, now - x.at)}|${x.title}`);
  const rs = feeds.map((f) => f.r || NONE);
  const bad = rs.find((r) => r.state !== 'ok');
  const age = Math.max(0, ...rs.filter((r) => r.data).map((r) => r.age | 0));
  out.push(`news_age=${age}`, `news_state=${!bad ? 'ok' : `${picked.length ? 'stale' : 'none'}|${clean(bad.reason)}`}`);
  return out;
}

// The reply. fx, wx and hol are {state: 'ok'|'stale'|'none', reason, age, data}, as index.js's
// get() gives them: fx.data as fxLines reads it, wx.data Open-Meteo's forecast, hol.data the
// holidays [{fecha, nombre}] sorted; news is [{label, weight, r}], one per feed of NEWS_FEEDS (none:
// no news lines). Since Couch 0.5.0 air (air.data Open-Meteo's air quality) and fut (fut.data the
// day of futbol.js), results too (absent: no line). env: the vars of wrangler.jsonc.
export function boardText({ now, fx = NONE, wx, news = [], hol = NONE, air = NONE, fut = NONE, env }) {
  const tz = env.TZ || 'UTC', locale = env.LOCALE || 'en-US';
  // The offset at now, not Open-Meteo's utc_offset_seconds: a cached forecast keeps the offset of
  // its fetch, which is wrong after a DST change. The city is cut to 100 code points, the only
  // line that the 4 KB guard below cannot drop (with geo and numfmt).
  const offset = tzOffset(now, tz);
  const city = Array.from(clean(env.CITY || '')).slice(0, 100).join('').trim();
  const out = [`couch-board ${VERSION}`, `now=${now}`, `offset=${offset}`, `city=${city}`, numfmtLine(locale)];
  const geo = geoLine(env);
  if (geo) out.push(geo);
  out.push(...fxLines({ fx, env, locale }));
  const c = wx.data && wx.data.current, dy = wx.data && wx.data.daily;
  const deg = (v) => (Number.isFinite(v) ? `${Math.round(v)}°` : '-');
  const pct = (v) => (Number.isFinite(v) ? `${Math.round(v)}%` : '-');
  const kmh = (v) => (Number.isFinite(v) ? `${Math.round(v)} km/h` : '-');
  // The 7th field names the now line's source (Couch's footer).
  if (c) {
    out.push(`wx_now=${deg(c.temperature_2m)}|${deg(c.apparent_temperature)}|${pct(c.relative_humidity_2m)}|` +
      `${wmoWords(c.weather_code)}|${kmh(c.wind_speed_10m)}|${c.is_day ? 'day' : 'night'}|Open-Meteo`);
  }
  const wfx = fxLine(c);
  if (wfx) out.push(wfx);
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
    // days_fx: the WMO code of each day line, in order.
    if (days.length) out.push(`days_fx=${days.map(([, i]) => wmoCode(dy.weather_code && dy.weather_code[i])).join('|')}`);
  }
  // The sky, from the same forecast: the rain of the next 12 h (wx_rain=rain|words or dry|words),
  // today's sunrise and sunset, the next 12 hours (wx_hours), and the moon from the date (also
  // with no forecast).
  let hours = null;
  if (wx.data) {
    const p = parts(now, tz), rain = rainWords(wx.data.hourly, now, tz), sun = sunTimes(dy, `${p.year}-${p.month}-${p.day}`);
    if (rain) out.push(`wx_rain=${rain.wet ? 'rain' : 'dry'}|${rain.words}`);
    const soon = soonWords(wx.data.minutely_15, now, tz);       // since Couch 0.5.0: the next 2 h by 15 minutes
    if (soon) out.push(`wx_soon=${soon}`);
    if (sun) out.push(`wx_sun=${sun.rise}|${sun.set}`);
    hours = hoursLine(wx.data.hourly, now, tz);
  }
  out.push(`wx_moon=${moonPhase(now)}`);
  if (hours) out.push(hours);
  out.push(`wx_age=${Math.max(0, wx.age | 0)}`, `wx_state=${stateLine(wx)}`);
  // Since Couch 0.5.0: the air now (air=aqi|uv), and after the holiday the fútbol of today
  // (futbol=live|total|words, futbol.js).
  const a = airLine(air);
  if (a) out.push(a);
  const h = holidayLine(hol, now, tz);
  if (h) out.push(h);
  const f = futbolLine(fut, now, tz);
  if (f) out.push(f);
  out.push(...newsLines(news, now, Math.min(8, Number(env.NEWS_MAX) || 8), Number(env.NEWS_FRESH) || NEWS_FRESH), 'end');
  let text = out.join('\n') + '\n';
  // A body over MAX_BYTES would be cut by Couch (its buffer is 4 KB), so lines drop from the end
  // until it fits: the news first, then the air, the fútbol, the holiday and the series, the 12
  // hours, the changes, the sky lines, then the rate and day lines. city, numfmt and geo stay.
  const drop = [['news='], ['air='], ['futbol=', 'holiday=', 'spark='], ['wx_hours='], ['rate_chg='],
    ['wx_rain=', 'wx_soon=', 'wx_sun=', 'wx_moon=', 'wx_fx=', 'days_fx='], ['rate=', 'feature=', 'day=']];
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
