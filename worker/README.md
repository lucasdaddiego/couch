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
- The 4 KB guard: a Couch 0.1.0 refuses a body over 4096 bytes whole, so the reply stays within
  4096 bytes. Lines drop from the end in this order: the news, then the money, then the sky,
  then the rate and day lines. The reply of 2026-10-13 with every upstream has 1618 bytes.

- `src/board.js`: the reply's format, the sky, the money lines, the news lines, pure functions.
- `src/smn.js`: SMN's zip, its Latin-1 text and its rows, pure functions.
- `src/news.js`: the head-only read of a feed's body, then the RSS feeds and `NEWS_FEEDS`, pure
  functions.
- `src/index.js`: the Worker: the route, the upstreams, the cache.
- `test/`: `node --test` against the upstream answers of 2026-10-10, SMN's zip of 2026-10-12
  (`fixtures/tiepre.zip`), the answers of 2026-10-13 17:47 UTC for Junin (`meteo_junin.json`,
  `riesgo.json`, `inflacion.json`: the last 24 entries of the list, `plazofijo.json`,
  `clarin.xml`), Infobae's feed of 2026-10-13 21:32 UTC (`infobae.xml`: the head of the feed,
  its first 61440 bytes, 10 items and part of the 11th, so the tests run the cut; its items are
  newer than the Junin fixtures' time, so their age in the first reply is 0), the feeds of
  `NEWS_FEEDS` of 2026-10-13 22:04:30 UTC (`gn_infobae_economia.xml`, `gn_infobae_politica.xml`,
  `bbc_world.xml`: every item in the feed's order, the first item whole, the others only their
  title, link (none for Google News), pubDate and source, 7 KB to 14 KB; `ambito_economia.xml`:
  its first 12 items of 99; `clarin_politica.xml`, `clarin_mundo.xml`: whole; the handle test
  reads `NEWS_FEEDS` from `wrangler.jsonc`) and a fake fetch and cache, with bodies in chunks for
  the head-only read. The first test still uses `clarin.xml` and `infobae.xml`: its reply is the
  host check's (`board_check.c` names its lines). The first test's reply is also the host
  check's and the preview's (`body_ok`), so it keeps its text as UTF-8. CI runs them on every push (no npm install: the tests need only
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
