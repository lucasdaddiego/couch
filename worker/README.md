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
Since 2026-10-14 also the news reader: `GET /news` (30 headlines with a summary) and
`GET /article?id=<id>` (one item's text), each its own request (below, "The news reader").

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
  `Label=URL`, `Label*weight=URL` or (since 2026-10-14) `Label*weight@category=URL` and
  `Label@category=URL` entries between commas (not `:`, which a URL holds; a URL with a comma
  cannot be written; the first `=` ends the label part), at most 6; `NEWS_MAX` (8) lines. The
  category is 1 to 16 letters `a` to `z` (case not counted, kept in lower case), only for
  `/news` and `/article`; a bad category skips the entry; no `@`: no category. The weight (1 to 8, 1 when
  absent) is the feed's share of the lines: first each feed gives its weight of its newest items,
  in the order of `NEWS_FEEDS`, then each feed gives one item in turn until 8 lines, so a feed
  with no items leaves its share to the others; then the lines are sorted newest first. With
  weight 1 for every feed (the old `Label=URL` form) this is the fair share in turn of before.
  Each feed is an upstream of its own (`https://couch.internal/news2?u=<URL>`, `news2` since
  2026-10-14), cached as its parsed items, not as XML: at most 20 (`FEED_ITEMS`, for `/news`;
  8 before), each with its id, title, whole title, date, link and summary, the cache that
  `/board`, `/news` and `/article` share (6 to 13 KB of JSON per feed on 2026-10-14). The Worker
  reads only the head of every feed's body (`src/news.js`'s `readFeedHead`, for the free plan's
  10 ms of CPU per request): it streams the body, and at the end of the 20th `</item>` (the 8th
  before 2026-10-14) it checks the dates of those items. When they are newest first, the read
  stops there (or after 96 KB, whichever comes first). Else (Google News' search sorts by relevance, BBC's world feed is not in order) the read goes on
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

## The news reader (since 2026-10-14)

Lucas's ask: more news, open an item and read it, scroll for more. A Couch 0.2.0 refuses a Board
over 4096 bytes, so the reader has two requests of its own, in the Board's style: UTF-8 text,
LF line ends, line 1 the format and its version, then `key=value` lines with `|` between the
fields of a value, then `end` and an LF. No escapes: a value never holds `|`, CR, LF or another
control character (U+0000 to U+001F, U+007F: the Worker makes them spaces and collapses the
spaces), so a parser splits a line at its first `=`, then the value at `|`. A parser skips a key
that it does not know (new keys keep the version). Both answer HTTP 200 with
`content-type: text/plain; charset=utf-8` and `cache-control: no-store`, the state in the body,
as `/board` does; another method or path is 404. Code points (cp) below are Unicode characters;
the byte counts are of UTF-8.

`GET /news`:

```
couch-news 1
now=1792008077
item=565500e1|Ámbito|economy|1157|Organizaciones cuestionan el acuerdo con el FMI por su impacto social y ambiental|Un informe presentado por cinco entidades analiza las consecuencias del plan económico en la producción y la estabilidad del país. Además, advierte sobre posibles incumplimientos de compromisos internacionales en materia de derechos humanos y acción climática.
item=...                                   (0 to 30 lines, the newest first)
news_age=0
news_state=ok
end
```

- `now`: the Worker's Unix time in seconds.
- `item=id|source|category|age|title|summary`, six fields, in this order:
  - `id`: 8 characters `0-9a-f`: FNV-1a (32 bits) of the UTF-8 bytes of the item's link; of
    `title:` and the whole title for an item with no link. The same while the item is in its
    feed (the link does not change). `/article` takes it.
  - `source`: the feed's label in `NEWS_FEEDS` (`Ámbito`, `Clarín`, `BBC`).
  - `category`: the feed's category in `NEWS_FEEDS` (`economy`, `politics`, `world`; 1 to 16
    letters `a-z`), `-` when it has none.
  - `age`: whole seconds since the item's date (0 for a date ahead of the Worker's clock), `-`
    for an item with no date (it sorts last).
  - `title`: 1 to 110 cp (at most 330 bytes), cut at a word with `...` (three dots) when longer.
  - `summary`: the item's `<description>`, tags stripped (Clarin writes a `<ul>` of `<li>`),
    entities decoded, 0 to 280 cp (at most 840 bytes), cut at a word with `...`; empty when the
    item has none (the line then ends with `|`).
  - So a line has at most about 1.3 KB.
- The mix: each feed's quota of the 30 by its weight (largest remainder: `Ámbito*3`,
  `Clarín*3`, `Clarín`, `BBC` give 11, 11, 4, 4: 3/8 economy, 3/8 politics, 2/8 world), its
  newest items first; then one item per feed in turn until 30, so a feed with fewer items (Clarin
  politics has 10) or a dead feed leaves its share to the others. An item whose id is already in
  the list (the same link in two feeds) is skipped. Then the items are sorted newest first.
  `NEWS_LIST` (30) sets the count.
- `news_age` and `news_state`: as the Board's (`ok` when every feed is ok, else `stale|<reason>`
  of the first failed feed while there are items, else `none|<reason>`). No `NEWS_FEEDS`:
  `news_age=0` and `news_state=none|no NEWS_FEEDS`, no items.
- The reply is at most 32768 bytes (`NEWS_BYTES`): item lines drop from the end (the oldest)
  until it fits. The fixtures of 2026-10-14 give 30 items in about 10.2 KB.
- Each feed is read as for the Board (`readFeedHead`), its 20 newest items cached
  (`FEED_ITEMS`) under the Board's key: a `/news` after a `/board` reads the cache only, and the
  reverse.

`GET /article?id=<id>`:

```
couch-article 1
now=1792008077
id=2398f1f7
source=BBC
category=world
title=Pope walking and recovering 'as expected' after surgery, doctors say
date=Wed 14 Oct 12:12|17311
link=https://www.bbc.co.uk/news/articles/cwx28r0zvm6lo?at_medium=RSS&at_campaign=rss
state=ok
p=Pope Leo XIV's recovery from surgery to remove a lower lobe of his lung is "progressing as expected", his doctors have said.
p=...                                      (0 or more lines, in the page's order)
end
```

- The lines come in this order. `id`, `source`, `category`: as `/news`. `title`: the whole
  title, at most 300 cp (900 bytes), cut at a word with `...` when longer (the list's title is
  cut at 110). `date=<words>|<age>`: the item's date in `TZ` as `Wed 14 Oct 12:12` (English
  weekday and month, day without a zero, 24 h), and its age in seconds; `-|-` with no date.
- `link`: the item's link (a QR code on the TV), at most 1024 bytes with `link=`; no line when the
  item has none or it is longer. `|`, spaces and control characters in it are percent-encoded
  (`%7C`, `%20`).
- `state`: `ok` (the page's paragraphs), `cut` (the same, the end dropped at the cap), or
  `summary|<reason>`: the page failed, and the only `p=` line is the feed's summary (none when
  the item has no summary). Reasons: `<host>: HTTP 403` (the publisher refused the Worker,
  Infobae's case), `<host>: no answer in 8 s`, `<host>: no paragraphs` (no rule found 200 cp of
  text), `<host>: <error>`, `no link`.
- `p=`: one line per paragraph, its text with its children's text, entities decoded, spaces
  collapsed; each at most 1500 cp (cut at a word with `...`). The `p=` lines together, with
  `p=` and LF, are at most 12288 bytes (`TEXT_BYTES`): whole paragraphs, the one that crosses and
  the rest drop (`state=cut`). The whole reply is at most 16384 bytes.
- No item: only `couch-article 1`, `now`, `state=none|<reason>` and `end`. Reasons: `bad id`
  (not 8 characters `0-9a-f`), `unknown id` (no cached item of a feed of `NEWS_FEEDS` has it:
  the item left its feed, or the id is not one of ours), `worker: <error>`.
- No open proxy: the Worker fetches only the link of a cached feed item with that id. The id
  never carries a URL, and an unknown id makes no fetch but the feeds' (which `/board` makes too).
- The page: fetched with the user agent `couch-board` and an 8 s timeout (`ARTICLE_TIMEOUT_MS`;
  Lucas waits for an article, not for a Board), redirects followed. `src/article.js` streams it
  through the runtime's `HTMLRewriter` (lol-html, no DOM): the publisher's rule (`RULES`, by the
  final URL's host or a subdomain) and a generic rule, `article p` (paragraphs of 40 cp or more),
  in the same pass. The publisher's paragraphs (2 cp or less dropped: Clarin's author initials)
  are used when they hold 200 cp or more, else the generic rule's, else none. The read stops 64
  KB after the last body paragraph (`QUIET_BYTES`), at 16384 characters of body text
  (`ENOUGH_CHARS`), or at 768 KB (`PAGE_BYTES`); the rest of the body is cancelled.
- The rules, read on the pages of 2026-10-14 (curl with `couch-board`; each answered 200):
  - Ambito, `article.article-body > p`: the body comes in several `<article class="article-body
    ...">` between related links; the lead (`h2.news-headline__article-summary`), the photo
    caption, an embedded picture's caption (a `<p>` inside a `<div>`) and the subscriber note
    stay out. The page: 129 KB, the body from 67 KB to 81 KB: 10 paragraphs, 3.9 KB.
  - Clarin, `#cuerpo div.text-embed > p`: one `<div class="... text-embed">` per paragraph in
    `<div id="cuerpo">`; an embedded tweet (`<section class="TextParrafo...">`), the photo
    captions (`span.epigraphe`) and the newsletter box stay out; the last paragraphs are the
    sources ("Fuente: AFP y EFE"). Politics: 333 KB, the body at 205 KB to 212 KB, 13 paragraphs,
    3.5 KB; world: 335 KB, 22 paragraphs, 4.7 KB.
  - BBC (`bbc.co.uk`, `bbc.com`), `div[data-block="text"] p`: the text blocks; the video notes
    ("This video can not be played"), the captions and the promo links are other blocks. 398 KB,
    the body at 154 KB to 165 KB, 16 paragraphs, 2.5 KB.
  - The replies of those four items: 4.3 KB (Ambito), 4.0 KB and 5.2 KB (Clarin), 2.8 KB (BBC).
- The cache: each article's `{paras, cut}` under `https://couch.internal/article?u=<link>` for
  `ARTICLE_TTL` (3600 s). A failed page is remembered as any upstream's failure (the negative
  cache below): the next `/article` of it answers the summary at once, and tries the page again
  after the reply at most once per 300 s.

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
time, or `wrangler tail`). Each request has its own 50: `/news` on a cold cache makes 4 fetches,
4 reads and 4 writes (12), warm 4 reads; `/article` on a cold cache 5 fetches (4 feeds, the
page), 5 reads and 5 writes (15, and one more per redirect of the page), with the feeds cached 7,
with the article cached 5 reads (the tests count them). CPU, estimated (not measured): a warm
`/news` parses 4 cache entries (33 KB of JSON) and writes 30 lines; a cold one also parses 4 feed
heads (about 60 KB, regular expressions, as the Board's); `/article` runs lol-html over the read
part of the page (Ambito all of its 129 KB, Clarin about 280 KB of 333, BBC about 230 KB of 398),
in native code, with a JavaScript call per paragraph and per text chunk. Measure both after the
deploy. A `/board` now parses 20 items per feed instead of 8 (with the summaries) and reads 33 KB
of cached JSON instead of about 4 KB.

The 4 KB guard: a Couch 0.1.0 refuses a body over 4096 bytes whole, so the reply stays within
4096 bytes. Lines drop from the end in this order (design 5.5): the news, the Blue series, the 12
hours, the changes (`usd_chg`, `money_chg`), the money lines, the sky lines (`wx_rain`, `wx_sun`,
`wx_moon`, `wx_fx`, `days_fx`), then the rate and day lines. `geo` stays with `city`. The reply of
2026-10-13 with every upstream has 2219 bytes (1618 without the keys of 0.3.0); with 8 titles of
110 three-byte characters it has 4050 bytes and nothing drops.

- `src/board.js`: the reply's format, the sky, the money lines, the news lines, pure functions.
- `src/smn.js`: SMN's zip, its Latin-1 text and its rows, pure functions.
- `src/news.js`: the head-only read of a feed's body, then the RSS feeds (ids, links, summaries)
  and `NEWS_FEEDS`, pure functions.
- `src/reader.js`: the `/news` and `/article` replies, the mix's quotas, the paragraph caps, pure
  functions.
- `src/article.js`: a page's paragraphs with `HTMLRewriter`, the publishers' rules.
- `src/series.js`: the tail read of a long list and its dated entries.
- `src/index.js`: the Worker: the routes, the upstreams, the cache and the negative cache.
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
- `test/reader.test.js` (since 2026-10-14): `/news` and `/article` against the feeds of
  `NEWS_FEEDS` of 2026-10-14 20:01 UTC (`news_ambito.xml`, `news_clarin_politica.xml`,
  `news_clarin_mundo.xml`, `news_bbc.xml`: whole, as the feeds gave them), with the fake fetch
  and cache: the ids and their stability, the categories, the mix, the shared cache, the
  fallback and the negative cache, the open-proxy refusal, the size caps, UTF-8. Node has no
  `HTMLRewriter`, so there `/article` uses a stand-in extraction (every `<p>` of a small page).
- `test/article.workerd.test.js` and `test/extract.worker.js`: the real extraction in workerd
  through miniflare (installed with wrangler by `dev.zsh`'s `npm ci`) on the four publishers'
  pages of 2026-10-14 (`article_*.html`, trimmed: no head, scripts, styles, SVGs, images or
  comments, cut about 12 KB after the body; 36 to 53 KB each), a page in 7-byte chunks, the
  generic rule and the early stops. CI runs `node --test` with no npm install: these tests are
  skipped there (their reason says so); `zsh couch/worker/dev.zsh test` runs them.
- `dev.zsh`: the tests and `wrangler dev` in a throwaway container from a git archive (commit
  first), published on the Mac's LAN address, port 8787, for the PS3 and for curl.

The keys of Couch 0.3.0 and the negative cache are deployed since 2026-10-13 (version 5bb13ba4,
commit e058547). The news reader (`/news`, `/article`) is not deployed yet (the deploy waits for
Lucas's OK), and it is not checked yet that Ambito's, Clarin's and BBC's pages answer the
deployed Worker (a 200 from the Mac does not prove it).
Deployed on `http://couch.daddiego.com.ar/board` (2026-10-10, Lucas's OK): `wrangler deploy
--env-file ~/.config/cloudflare/personal.env` from this folder, after a commit. Plain HTTP works
because the zone `daddiego.com.ar` has Always Use HTTPS off and one Single Redirect rule
(HTTP to HTTPS for every host but this one): a Configuration Rule cannot change that setting.
A new host on the zone gets the redirect by itself. Turning Always Use HTTPS on again breaks
Couch, which has no TLS.
