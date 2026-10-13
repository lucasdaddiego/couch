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
  `Label=URL` or `Label*weight=URL` entries between commas (not `:`, which a URL holds; a URL
  with a comma cannot be written), at most 6; `NEWS_MAX` (8) lines. The weight (1 to 8, 1 when
  absent) is the feed's share of the lines: first each feed gives its weight of its newest items,
  in the order of `NEWS_FEEDS`, then each feed gives one item in turn until 8 lines, so a feed
  with no items leaves its share to the others; then the lines are sorted newest first. With
  weight 1 for every feed (the old `Label=URL` form) this is the fair share in turn of before.
  Each feed is an upstream of its own (`https://couch.internal/news?u=<URL>`), cached as its
  parsed items (at most 8, the most news lines of a reply), not as XML. The Worker reads only the
  head of every feed's body (`src/news.js`'s `readFeedHead`, for the free plan's 10 ms of CPU per
  request): it streams the body, and at the end of the 8th `</item>` it checks the dates of those
  8 items. When they are newest first, the read stops there (or after 96 KB, whichever comes
  first). Else (Google News' search sorts by relevance, BBC's world feed is not in order) the read goes on
  to the end of the 100th `</item>` or 256 KB. Then the rest of the stream is cancelled, and
  `parseFeed` sorts all the items that it read newest first (equal dates keep the feed's order,
  an item with no date goes last) and keeps 8. A feed whose first 8 items are in order by chance,
  but not its later items, loses its newer items. The count is of `</item>` tags, so an item with
  an empty title gives one item less. `src/news.js` reads RSS 2.0 with no library (Workers have
  no DOMParser): each `<item>`'s `<title>`, `<pubDate>` (or `dc:date`) and `<source>`, CDATA
  kept, entities decoded, tags stripped, `|` removed. When the item has a `<source>` (Google News
  names the publisher there), a title that ends with " - " and that publisher loses that end
  (also with "–" or "—"; a title that is only the publisher stays). Then the title is cut to 110
  code points at a word with "...". The age is in seconds at `now` (`-` with no date).
  `news_state` is `ok` when every feed is ok, else `stale` with the first failed feed's reason
  while any feed has items, else `none`.
  The mix (Lucas's choice, 2026-10-13: Infobae and a mix of sources, mostly economy and politics,
  also world affairs; English is acceptable): 3 economy lines (Infobae 2, Ambito 1), 3 politics
  lines (Infobae 2, Clarin 1), 2 world lines (Clarin 1, BBC 1). The label is the publisher's short
  name: the TV's source column has 99 px at the widest age ("23 h 59 min") in the 22 px font, and
  "Infobae" is 77 px, "Ámbito" 72, "Clarín" 59, "BBC" 44 (a section name such as "Economía", 100
  px, does not fit). **In use since the deploy of 2026-10-13:** `Ámbito*3` (economy), `Clarín*3`
  (politics), Clarín world and BBC World. Google News did not answer the deployed Worker (no
  answer in 4 s on every request, so every Board took 4.1 s), and Infobae's own feed answers 403
  to it: no path from Cloudflare reaches Infobae. The Infobae entries below stay as the record.
  The feeds, read with curl on 2026-10-13 22:04 UTC with the Worker's user
  agent `couch-board` (each answered 200 with it, no browser user agent needed):
  - Infobae's economy and politics through Google News' RSS search,
    `https://news.google.com/rss/search?q=site:infobae.com/economia+when:1d&hl=es-419&gl=AR&ceid=AR:es-419`
    and the same with `/politica`. Why: Infobae's own RSS (`https://www.infobae.com/arc/
    outboundfeeds/rss/...`) answers HTTP 403 to the deployed Worker (Cloudflare's egress), though
    it answers 200 from the Mac. Economy: 100 KB, 45 items; politics: 66 KB, 25 items. Both by
    relevance: 17 and 10 items newer than the item before them, and only 2 and 1 of the 8 newest
    items in the first 8 (6 of the 8 newest are items 30 to 37 of 45, 5 are items 21 to 25 of
    25), so both are read whole and sorted. `when:1d` also lets through old pages that Google
    keeps current (one dated 2019-12-09, one 2023-06-24): the sort puts them last. Each title
    ends with " - Infobae", and each item names `<source url="https://www.infobae.com">Infobae
    </source>`. The channel's copyright line allows the feed "for personal, non-commercial use"
    in a feed reader: Couch is a personal use.
  - Ambito's economy, `https://www.ambito.com/rss/pages/economia.xml`: 70 KB, 99 items, newest
    first, the 8th item ends at byte 6695, so the Worker reads about 7 KB. Ambito is behind
    Cloudflare (`server: cloudflare`, `cf-ray`).
  - Clarin's politics and world, `https://www.clarin.com/rss/politica/` (11.7 KB, 10 items, the
    8th ends at byte 9572) and `https://www.clarin.com/rss/mundo/` (10.9 KB, 10 items, the 8th
    at byte 8747): newest first, CDATA titles, RFC 2822 dates.
  - BBC's world news, `https://feeds.bbci.co.uk/news/world/rss.xml`: 18 KB, 23 items, not in
    order (its newest item was its 20th, 6 of the 8 newest in its first 8), read whole.
  A refresh of every feed reads about 210 KB and 117 items (Google News 100 + 66 KB, BBC 18 KB,
  Ambito 6.7 KB, Clarin 9.6 + 8.7 KB). Not used: Infobae's own feeds (403 to the Worker; the
  main feed is 687 KB of all of its editions, the section feeds about 1 MB each), Clarin's "lo
  ultimo" (the Board's feed until this mix), The Guardian's `/world/rss` (158 KB, not in order,
  its first 5 items 4 to 5 days old, with Australian and US home news), Al Jazeera's
  `/xml/rss/all.xml` (sport and video items in it), BBC Mundo's `/mundo/rss.xml` (Spanish, but
  of every topic), Google News for `infobae.com/america/mundo` (science and curiosities), La
  Nacion's economy feed (747 KB), El Cronista's `/files/rss/economia.xml` (404), Pagina/12's
  `/rss/secciones/el-pais/notas` (404), La Verdad of Junin (`https://laverdadonline.com/feed/`,
  local news), `diariolaverdad.com.ar` (San Nicolas, empty titles, dates of year -1), Democracia
  of Junin (no feed found).
  Not checked: that Google News, Ambito and BBC answer the deployed Worker. A 200 from the Mac
  does not prove it (Infobae gave 200 to the Mac and 403 to the Worker); only Clarin is known to
  answer the Worker. A feed that fails gives `news_state=stale|<host>: HTTP <status>` and leaves
  its share to the other feeds.

The keys of Couch 0.3.0 (`~/.claude/plans/2026-10-13-couch-revamp-design.md`, section 5; new keys
only, which Couch 0.1.0 and 0.2.0 skip, so the version stays 1). Numbers use a dot for decimals
and no thousands mark, `-` for a missing one; only a change's percent and amount are `LOCALE`
text with a sign.
- `geo=-34.58|-60.94`: `LAT` and `LON` with 4 decimals, after `city`. No line when either
  is not a number in range.
- `wx_fx=3|100|0|111|10|27`: the WMO code now, the cloud cover %, the precipitation in mm per
  hour, the wind's direction in degrees (where it comes from, 0 to 359), its speed and gusts in
  km/h, after `wx_now`. Open-Meteo's `current` (now also `cloud_cover`, `precipitation`,
  `wind_direction_10m`, `wind_gusts_10m`). The current precipitation is the sum of 15 minutes
  (`interval` 900), so the Worker scales it to an hour (design 3.5 counts the drops from an
  hourly rate). When the now line is SMN's, the code comes from SMN's sky words (`smnCode`:
  Cloudy and Overcast 3, Partly cloudy 2, Mist 45 as fog, haze and smoke take Open-Meteo's code);
  the wind stays Open-Meteo's (SMN's row has no gusts).
- `wx_hours=15|19 19 ...|55 63 ...|51 3 ...`: the first whole hour after now (local), then 12
  temperatures (deg C), 12 chances (%) and 12 WMO codes, each the value at that hour's stamp
  (`hourly` adds `temperature_2m` and `weather_code`). The hours count by their times, as
  `wx_rain`'s do; an hour past the forecast is `-`; no line when no hour has a temperature (a
  forecast cached before the deploy, for at most `WX_TTL`).
- `days_fx=51|3|51`: the WMO code of each `day` line, in order (`daily.weather_code`).
- `usd_chg=Cripto|down|-0,2%|-3,29|12 Oct`: per rate with a `usd` line and a previous close, its
  sell against the close of the previous calendar day (Lucas's choice): `up`, `down` or `flat`
  (equal in cents), the change in % (1 decimal, 2 when 1 decimal shows 0,0 for a change that is
  not 0) and in pesos, and the compared close's date. No line without a close. **Which entry
  holds a close:** argentinadatos.com's daily job (GitHub Actions, cron 22:10 UTC) copies
  dolarapi's rates and dates them with its run's UTC date, and GitHub starts it late: the
  entries dated 2026-10-06 to 2026-10-13 were written between 22:27 and 00:17 ART on the day
  before (commit times of `datos/v1/cotizaciones/dolares/oficial/...`). So the entry dated D
  holds the close of the local day before D (the entry of Friday 9 Oct has Thursday's 1.540 for
  the oficial, the entry of Saturday 10 Oct Friday's 1.535). The Worker fetches the entries
  dated today, one per rate of `CASAS` (`/v1/cotizaciones/dolares/<casa>/<yyyy>/<mm>/<dd>`, about
  85 B each), in parallel, and caches them as one entry
  (`https://couch.internal/closes?d=<today>&c=<casas>`) for 7 days (a past close does not change;
  the key holds the date). A rate with no entry yet (404: the job has not run) is fetched again
  with all the others after 15 minutes; no entry at all is `none`. On a Monday the entry dated
  Monday holds Sunday's close, which is Friday's rate. If GitHub starts the job on time one day
  (before 21:00 ART), the entry dated today holds today's close in the evening: that evening's
  chips show flat. Blue takes its close from the same fetch (one fetch more than the design: the
  series is cached 6 h and can lack today's entry).
- `money_chg=Riesgo país|flat|0,0%|0|9 Oct`: riesgo pais against the entry of its list before the
  current value's date (the list has no weekend or holiday entries: a Monday compares with
  Friday), whole points. The list is `/v1/finanzas/indices/riesgo-pais` (413 KB, 7,721 entries),
  read by its tail, its 10 newest entries cached 6 h (`https://couch.internal/rplist`); a newest
  entry more than 7 days old is a bad read.
- `spark=Blue|2026-09-14|1555 1560 ... 1540`: the Blue dollar's sell at the close of each of the
  30 local days to today, whole pesos, oldest first, from the first day's date, `-` for a missing
  day. A past day X is the series' entry dated X + 1 (above); yesterday falls back to the closes'
  Blue; today is the live sell of dolarapi (argentinadatos copies it once a day: at 19:38 ART on
  2026-10-13 its entry of today had 1.540, dolarapi 1.550). The series is
  `/v1/cotizaciones/dolares/blue` (523 KB, 5,763 entries since 2011, 25 KB with gzip), never
  `JSON.parse`d: `src/series.js` streams the body, keeps only its last 8 KB (about 90 entries),
  and reads the whole `{...}` objects there with regular expressions (the cut first object is
  skipped). Its 31 newest entries are cached 6 h (`https://couch.internal/blue30`). Fewer than 25
  entries in the tail, or a newest entry more than 3 days old, is a bad read: the old entry stays
  as `stale`.
- No KV: argentinadatos keeps dated history, so the Cache API only saves fetches (design 5.3).

The negative cache: a failed upstream (an HTTP error, no answer in 4 s, a bad body) is remembered
in its cache entry (`fail: {at, reason}`, next to the old data, if any). While it is, a request
does not wait for that upstream: it answers at once with the old data (`stale`) or `none`, with the
failure's reason, and at most once per 300 s (`RETRY_S`) it tries the upstream again after the
reply (`waitUntil`). A success removes the mark, and the next Board has the data. 300 s because
Couch asks every 5 minutes and `USD_TTL` is 300 s: a dead upstream gets one try per Board fetch at
most, in the background. So a dead feed costs its 4 s timeout on its first failure only (and once
a day, when the Cache API drops the entry), not on every request as before (Google News made every
Board 4.1 s).

The free plan's limits: the Cache API's `match` and `put` count with `fetch` in the 50
subrequests of a request. With the vars of `wrangler.jsonc` and a cold cache: 18 fetches (usd,
wx, smn, riesgo pais, inflation, plazo fijo, 4 feeds, 6 closes, the 2 lists), 13 reads and 13
writes, 44 in all (a test counts them); warm, 13 reads. With 6 feeds (`FEEDS_MAX`) it is 50,
the limit. CPU: the two tail reads run once per 6 h. Measure after the deploy (the dashboard's CPU
time, or `wrangler tail`).

The 4 KB guard: a Couch 0.1.0 refuses a body over 4096 bytes whole, so the reply stays within
4096 bytes. Lines drop from the end in this order (design 5.5): the news, the Blue series, the 12
hours, the changes (`usd_chg`, `money_chg`), the money lines, the sky lines (`wx_rain`, `wx_sun`,
`wx_moon`, `wx_fx`, `days_fx`), then the rate and day lines. `geo` stays with `city`. The reply of
2026-10-13 with every upstream has 2219 bytes (1618 without the keys of 0.3.0); with 8 titles of
110 three-byte characters it has 4050 bytes and nothing drops.

- `src/board.js`: the reply's format, the sky, the money lines, the news lines, pure functions.
- `src/smn.js`: SMN's zip, its Latin-1 text and its rows, pure functions.
- `src/news.js`: the head-only read of a feed's body, then the RSS feeds and `NEWS_FEEDS`, pure
  functions.
- `src/series.js`: the tail read of a long list and its dated entries.
- `src/index.js`: the Worker: the route, the upstreams, the cache and the negative cache.
- `test/`: `node --test` against the upstream answers of 2026-10-10, SMN's zip of 2026-10-12
  (`fixtures/tiepre.zip`), the answers of 2026-10-13 17:47 UTC for Junin (`meteo_junin.json`,
  `riesgo.json`, `inflacion.json`: the last 24 entries of the list, `plazofijo.json`,
  `clarin.xml`), Infobae's feed of 2026-10-13 21:32 UTC (`infobae.xml`: the head of the feed,
  its first 61440 bytes, 10 items and part of the 11th, so the tests run the cut; its items are
  newer than the Junin fixtures' time, so their age in the first reply is 0), the feeds of
  `NEWS_FEEDS` of 2026-10-13 22:04:30 UTC (`gn_infobae_economia.xml`, `gn_infobae_politica.xml`,
  `bbc_world.xml`: every item in the feed's order, the first item whole, the others only their
  title, link (none for Google News), pubDate and source, 7 KB to 14 KB; `ambito_economia.xml`:
  its first 12 items of 99; `clarin_politica.xml`, `clarin_mundo.xml`: whole; the mix test
  keeps the `NEWS_FEEDS` of those fixtures, the cold-cache test reads `wrangler.jsonc`'s), the
  answers of 2026-10-13 22:38 UTC for the keys of 0.3.0 (`meteo_junin.json` with the new current
  and hourly fields; `dolares_dia.json`: the per-day entries of the six rates dated 2026-10-12
  and 2026-10-13; `blue_tail.json` and `riesgo_tail.json`: the last 45 and 15 entries of the two
  lists, as the API writes them) and a fake fetch and cache (the cache counts its calls), with
  bodies in chunks for the head-only and the tail reads. The first test still uses `clarin.xml` and `infobae.xml`: its reply is the
  host check's (`board_check.c` names its lines). The first test's reply is also the host
  check's and the preview's (`body_ok`), so it keeps its text as UTF-8. CI runs them on every push (no npm install: the tests need only
  Node).
- `dev.zsh`: the tests and `wrangler dev` in a throwaway container from a git archive (commit
  first), published on the Mac's LAN address, port 8787, for the PS3 and for curl.

The keys of Couch 0.3.0 and the negative cache are not deployed yet: the live Worker sends none
of them (the deploy waits for Lucas's OK).
Deployed on `http://couch.daddiego.com.ar/board` (2026-10-10, Lucas's OK): `wrangler deploy
--env-file ~/.config/cloudflare/personal.env` from this folder, after a commit. Plain HTTP works
because the zone `daddiego.com.ar` has Always Use HTTPS off and one Single Redirect rule
(HTTP to HTTPS for every host but this one): a Configuration Rule cannot change that setting.
A new host on the zone gets the redirect by itself. Turning Always Use HTTPS on again breaks
Couch, which has no TLS.
