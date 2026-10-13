# Couch's Board Worker

`GET /board` answers the Board's data for Couch on the PS3 as plain text (format version 1:
`~/.claude/plans/2026-10-10-couch-design.md`, section 4): the dollar rates of dolarapi.com, the
weather now of SMN (Argentina's Servicio Meteorologico Nacional), else Open-Meteo, the days, the
rain of the next 12 h and the sun of Open-Meteo, the moon from the date, the money extras of
argentinadatos.com and the headlines of RSS feeds. Each upstream is kept in the Cache API with its
time (`USD_TTL` 300 s, `SMN_TTL` 600 s, `WX_TTL` 900 s, `RP_TTL` 900 s, `PF_TTL` 3600 s,
`IPC_TTL` 21600 s, `NEWS_TTL` 600 s per feed), so a failed upstream gives the old values marked
`stale` with their real age. Every setting is a var in `wrangler.jsonc` (the city, its
coordinates and time zone, the SMN station, the rates and their labels, the bank, the feeds). The place is Junin, Buenos Aires (since 2026-10-12, Lucas's choice): `CITY`
`Junín`, Open-Meteo at the coordinates of SMN's station there (-34.58, -60.94, WMO 87548).

The now line (`wx_now`):
- SMN's open data: `https://ssl.smn.gob.ar/dpd/zipopendata.php?dato=tiepre`, a zip (about 3 KB)
  of one Latin-1 text file with one line per station (`station;date;time;sky;visibility;
  temperature;feels like;humidity;wind;pressure /`, the time in Argentina's). `src/smn.js` reads
  the zip's central directory, inflates the entry with `DecompressionStream('deflate-raw')` and
  decodes the Latin-1 by hand. `SMN_STATION` picks the row: the station's exact name after a
  trim, case and accents not counted (`Junin` finds `Junín`). The cache holds the parsed row,
  not the zip, under the folded station, for `SMN_TTL` (600 s). An empty `SMN_STATION` makes no
  SMN fetch.
- The row is used when its observation is at most 3 h old (and at most 1 h ahead of the
  Worker's clock) and it has a temperature: the temperature, the feels-like temperature (the
  temperature when SMN writes "No se calcula"), the humidity, the sky in English words (by
  keyword, `src/board.js`'s `SKY`; an unknown state keeps its text, cut to 24 code points), the
  wind in km/h ("Calma" is 0), and day or night from Open-Meteo's `is_day` when Open-Meteo
  answered (`ok`), else from the local hour (06:00 to 19:59 is day).
- Else (no row, an old row, SMN down) the now line is Open-Meteo's `current`, as before.
- A 7th field names the source: `SMN 23:00` (the observation's local time) or `Open-Meteo`.
  A Couch from before 2026-10-12 reads 6 fields and ignores it. SMN has no state line in the
  reply: the row's own time decides, and its reason (`ssl.smn.gob.ar: no row for ...`,
  `ssl.smn.gob.ar: bad zip`) shows nowhere. `wx_age` and `wx_state` stay Open-Meteo's.
- Not used: SMN's 5-day file (`dato=pron5d`) is empty, `ws.smn.gob.ar/map_items` stopped in
  2022, and `ws1.smn.gob.ar/v1` needs a token. So the days stay on Open-Meteo.

The keys of 2026-10-13 (Lucas's choices; a Couch 0.1.0 skips them, so the version stays 1):
- The sky, from the same Open-Meteo fetch (`hourly=precipitation_probability,precipitation`,
  `daily=...,sunrise,sunset`: 3.8 KB for Junin): `wx_rain=rain|Rain now (75%)`,
  `wx_rain=rain|Rain from 18:00 (70%)` or `wx_rain=dry|No rain in the next 12 h`. An hour is wet
  at a chance of 40 % or more (where weather apps start to show rain) or 0.2 mm or more (the
  smallest amount a rain gauge counts). Open-Meteo stamps each hour at its end, so the words
  name the wet hour's start, and "now" when that is now's hour; the percentage is the highest
  chance in the 12 h. The hours count by their times, not by their index (a cached forecast
  starts on an earlier day). `wx_sun=06:24|19:16` is today's entry; `wx_moon=Waxing crescent
  moon` comes from the date alone (the age since the new moon of 2000-01-06 18:14 UTC, synodic
  month 29.530588853 d, eight phases), also with no forecast. A forecast cached before the deploy
  has no hourly and no sun: no rain or sun line until its `WX_TTL` ends (the cache key stays).
- The money extras (`money=label|value|date|state`, the reason after `stale` or `none`), from
  argentinadatos.com (keyless JSON, `access-control-allow-origin: *`, read with curl on
  2026-10-13): `/v1/finanzas/indices/riesgo-pais/ultimo` (`{"valor":589,"fecha":"2026-10-12"}`:
  "Riesgo pais|589|12 Oct"); `/v1/finanzas/indices/inflacion` (53 KB, monthly since 1943, no
  `/ultimo`: the cache keeps only the latest entry, `{"fecha":"2026-08-31","valor":1.7}`:
  "Inflacion mensual|1,7%|Aug 2026", INDEC's August, September's comes mid-October);
  `/v1/finanzas/tasas/plazoFijo` (20 KB, one row per bank, `tnaClientes` 0.1925 for Banco de la
  Nacion Argentina: "Plazo fijo BNA (TNA)|19,25%|14:47"). The fixed-term table has no date, so
  its date is the Worker's read (the cache's time). `PF_BANK` names the bank as the table writes
  it (case and accents not counted; empty: no line), `PF_LABEL` its label. Not used:
  `/tasas/depositos30Dias` (BCRA's 30-day rate, dated, but 366 KB), and no `/ultimo` for
  inflation or deposits (both 404).
- The news (`news=source|age|title`, then `news_age` and `news_state`): `NEWS_FEEDS` is
  `Label=URL` pairs between commas (not `:`, which a URL holds), at most 4; `NEWS_MAX` (8) lines.
  Each feed is an upstream of its own (`https://couch.internal/news?u=<URL>`), cached as its
  parsed items (at most 20), not as XML. `src/news.js` reads RSS 2.0 with no library (Workers have
  no DOMParser): each `<item>`'s `<title>` and `<pubDate>` (or `dc:date`), CDATA kept, entities
  decoded, tags stripped, `|` removed, the title cut to 110 code points at a word with "...".
  Each feed gets a fair share in turn, then the newest first; the age is in seconds at `now`
  (`-` with no date). `news_state` is `ok` when every feed is ok, else `stale` with the first
  failed feed's reason while any feed has items, else `none`. The feeds (read with curl on
  2026-10-13): Clarin's "lo ultimo" `https://www.clarin.com/rss/lo-ultimo/` (12 KB, 10 items,
  CDATA titles, RFC 2822 dates), and La Verdad of Junin `https://laverdadonline.com/feed/`
  (WordPress, 188 KB, 40 items). Not used: La Nacion's and Infobae's feeds (about 800 KB each,
  long for a 4 s budget), Pagina/12's `/rss/portada/` (404), `diariolaverdad.com.ar` (San
  Nicolas, empty titles, dates of year -1), Democracia of Junin (no feed found).
  Not checked: that each host answers a Worker's fetch (only curl from the Mac, with the
  Worker's user agent `couch-board`).
- The 4 KB guard: a Couch 0.1.0 refuses a body over 4096 bytes whole, so the reply stays within
  4096 bytes. Lines drop from the end in this order: the news, then the money, then the sky,
  then the rate and day lines. The reply of 2026-10-13 with every upstream has 1595 bytes.

- `src/board.js`: the reply's format, the sky, the money lines, the news lines, pure functions.
- `src/smn.js`: SMN's zip, its Latin-1 text and its rows, pure functions.
- `src/news.js`: the RSS feeds and `NEWS_FEEDS`, pure functions.
- `src/index.js`: the Worker: the route, the upstreams, the cache.
- `test/`: `node --test` against the upstream answers of 2026-10-10, SMN's zip of 2026-10-12
  (`fixtures/tiepre.zip`), the answers of 2026-10-13 17:47 UTC for Junin (`meteo_junin.json`,
  `riesgo.json`, `inflacion.json`: the last 24 entries of the list, `plazofijo.json`,
  `clarin.xml`, `laverdad.xml`: the channel and the first 8 of its 40 items) and a fake fetch
  and cache. The first test's reply is also the host check's and the preview's (`body_ok`), so
  it keeps its text as UTF-8. CI runs them on every push (no npm install: the tests need only
  Node).
- `dev.zsh`: the tests and `wrangler dev` in a throwaway container from a git archive (commit
  first), published on the Mac's LAN address, port 8787, for the PS3 and for curl.

The Worker of 2026-10-13 (the keys above) is not deployed yet: the live one sends none of them.
Deployed on `http://couch.daddiego.com.ar/board` (2026-10-10, Lucas's OK): `wrangler deploy
--env-file ~/.config/cloudflare/personal.env` from this folder, after a commit. Plain HTTP works
because the zone `daddiego.com.ar` has Always Use HTTPS off and one Single Redirect rule
(HTTP to HTTPS for every host but this one): a Configuration Rule cannot change that setting.
A new host on the zone gets the redirect by itself. Turning Always Use HTTPS on again breaks
Couch, which has no TLS.
