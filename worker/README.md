# Couch's Board Worker

A Cloudflare Worker that answers Couch in plain text over plain HTTP (the PS3
has no usable TLS). It reads public sources, keeps each answer in the Cache
API, and formats one reply of at most 4 KB.

| Endpoint | Reply |
|---|---|
| `GET /board` | the Board: the place, the rates, the weather, the next holiday, 8 headlines |
| `GET /news` | the News page: up to `NEWS_LIST` (30) headlines with their ids and categories |
| `GET /article?id=<8 hex>` | one headline's article: its page's paragraphs (at most 12 KB), else the feed's summary |
| `GET /futbol?d=0` or `d=1` | the Fútbol page: today's or tomorrow's fixtures by league (at most 16 KB); this template has no source, so its state is `none` |

Every reply is 200 with the state in its body; any other path or method is
404. `/article` fetches only the link of an item in a configured feed, so the
Worker is no open proxy. It also opens a headline that `/news` listed after
the item left its feed: `/news` keeps the feeds' last 150 items in one cache
entry.

## Settings

All in `wrangler.jsonc` as vars (no secrets); its comment describes each one.

| Var | Use |
|---|---|
| `CITY`, `LAT`, `LON`, `TZ` | the place: the city's name, the coordinates for Open-Meteo, an IANA time zone |
| `LOCALE` | the numbers' format (BCP 47, for example `de-DE`, `en-US`) |
| `CURRENCY`, `RATES`, `FEATURE` | your currency, up to 8 others (`USD,GBP:Pound,JPY`), the large tile's code |
| `COUNTRY`, `HOLIDAY_NAMES` | the holidays' country (ISO 3166 alpha-2), `local` or `english` names |
| `NEWS_FEEDS`, `NEWS_MAX`, `NEWS_LIST` | `Label*weight@category=URL` between commas, at most 6; the Board's and the News page's counts |
| `WX_TTL`, `AIR_TTL`, `FX_TTL`, `NEWS_TTL`, `ARTICLE_TTL` | seconds an answer stays fresh (900, 3600, 3600, 600, 3600; an absent `AIR_TTL` takes `WX_TTL`'s) |

An empty `RATES`, `COUNTRY` or `NEWS_FEEDS` leaves that part out.

## The sources

- **Open-Meteo** (`api.open-meteo.com`): the current weather, 4 days of daily
  values, the hourly values and 24 values of 15 minutes (the rain of the next
  2 h), in one request.
- **Open-Meteo's air quality** (`air-quality-api.open-meteo.com`): the US AQI
  and the UV index now.
- **Frankfurter** (`api.frankfurter.dev`): the ECB's reference rates, one
  time series of the last 32 days for every code of `RATES` against
  `CURRENCY`. Each value is shown as `CURRENCY` per one unit, per 100 units
  under 0.1 and per 1000 under 0.01 ("1000 JPY"). The ECB publishes once a
  working day: weekends and holidays have no point on the chart, and the
  change compares the latest day with the one before it.
- **Nager.Date** (`date.nager.at`): the public holidays of `COUNTRY` for the
  year, and the next year once this year's are over; only the nationwide ones
  of type Public.
- **RSS feeds**: each feed's head only (up to its 20th item, or 100 items when
  they are not newest first), with no XML library. A feed's weight is its
  share of the Board's 8 headlines; headlines older than 6 h
  (`NEWS_FRESH`) only fill the places left. An article page is read with
  HTMLRewriter (`src/article.js`): a publisher's rule (`RULES`, by host) or the
  generic one (`article p`).

## The cache

Each upstream is one Cache API entry with its time. A fresh entry answers at
once; an old one is fetched again, and when the upstream fails the old values
go out marked `stale` with their real age and the reason. A failure is
remembered: the next requests answer at once with the old entry, and the
upstream is tried again after the reply at most once per 300 s. A cold
`/board` with 3 feeds makes 7 fetches and 14 cache calls, far below the free
plan's 50 subrequests (6 feeds and both holiday years: 31). At most 6 fetches
and Cache API calls wait for their answer at once (the runtime's limit of
open connections), each fetch with its 4 s limit from its own start. The
rates' and the holidays' entries have no date in their keys: after midnight,
or on 1 January, a failed upstream still gives the old values, stale.

## The Board reply

UTF-8 text: the line `couch-board 1`, then `key=value` lines (fields between
`|`), then `end`. Couch skips a key it does not know. A reply over 4 KB drops
lines from the end: the news first, then the air, the fútbol, the holiday and
the chart, the hours, the changes, the sky lines, then the rates and days.

| Key | Fields |
|---|---|
| `now`, `offset`, `city` | the Worker's Unix time, the time zone's offset in seconds, the city |
| `numfmt` | the thousands mark (`_` for none) and the decimal mark of the numbers |
| `geo` | `lat\|lon` |
| `rate` | `label\|buy\|value\|date` (`buy` is `-` for a mid rate; Couch 0.5.1 does not read it) |
| `feature` | `label\|symbol`: the large tile's rate, `$` for Couch's dollar icon, other text as text, empty for none |
| `rate_chg` | `label\|up, down or flat\|percent\|amount\|date` (Couch reads the label, the direction and the percent) |
| `spark` | `label\|first date\|30 values\|decimals` (the values times 10^decimals, `-` for none; Couch 0.5.1 does not read the first date) |
| `rate_age`, `rate_state` | the rates' age in seconds, `ok` or `stale\|reason` or `none\|reason` |
| `wx_now` | `temperature\|feels like\|humidity\|words\|wind\|day or night` |
| `wx_fx` | `WMO code\|cloud cover\|mm per hour\|wind from (degrees)\|wind km/h\|gusts km/h` |
| `day`, `days_fx` | `label\|words\|max\|min\|chance of rain`, and each day's WMO code |
| `wx_rain`, `wx_sun` | `rain or dry\|words`, `sunrise\|sunset` (Couch works out the moon from its clock) |
| `wx_soon` | the rain of the next 2 h in words: `Rain from 18:45`, `Rain until 19:30` or `Rain for the next 2 h`; absent when dry (Couch 0.5.0) |
| `wx_hours` | `first hour\|12 temperatures\|12 chances\|12 WMO codes` |
| `wx_age`, `wx_state` | as the rates' |
| `air` | `US AQI\|UV index`, whole, `-` for a missing one; absent when the data is over 3 h old (Couch 0.5.0) |
| `holiday` | `date\|name\|days until it` (Couch only checks that the days are 0 to 400: it counts them from the date) |
| `futbol` | `games live\|words` (`2 live`, `Next: Home–Away 21:00`, `No more games today`); only from a Worker with a fútbol source (Couch 0.5.0) |
| `news`, `news_age`, `news_state` | `source\|age in seconds\|title`, as the rates' |

`/news` is `couch-news 1`, then `now`, `item=id|source|category|age|title`
lines, `news_age`, `news_state`, `end`. `/article` is `couch-article 1`, then
`now`, `id`, `source`, `category`, `title`, `date` (`words|age`), `link`,
`state` (`ok`, `cut`, `summary|reason` or `none|reason`) and one `p=` line per
paragraph, then `end`. Couch 0.5.1 reads neither reply's `now` nor
`/article`'s `id`: it matches an article to its request's id.

`/futbol` is `couch-futbol 1`, then `now`, `day` (`Today, Fri 16 Oct`), per
league a `league=name|country` line and its `game=when|home|away|home
goals|away goals|TV|kind` lines (`when`: the local start of a game to come,
else its minute, `HT`, `Final`, `Pens 4-3`, `Postponed`; the goals `-` before
the start; `kind`: `pre`, `live`, `end` or `off`), a `more=leagues|games` line
when the 16 KB cap left whole leagues out, `futbol_age`, `futbol_state`, `end`.
`src/futbol.js` formats it from one day in a plain form (its first comment): a
Worker with a source of fixtures fills that form; this template has none.

`/article` since Couch 0.6.0 adds `photo=1` after `link` when the item has a
picture: the feed's (`src/news.js`'s `itemImage`: the first `media:content`
that is an image, else `media:thumbnail`, else an image `enclosure`, else the
first `<img>` of the description) or the page's `og:image` (read by
`extractPage` into the article's cache entry). `GET /image?id=<item id>`
then answers the picture's own bytes (JPEG or PNG only, 1.5 MB at most, read
once and kept in the Cache API for a day) with its type and length, or a
text 404 (no picture, too large, not an image, the fetch failed, an unknown
id). Only an item of a cached feed is served: the Worker is no open proxy. The
feeds' cache entries moved to `news3` (the items gained `image`).

`/economia` (Couch 0.6.0) is `couch-economia 1`, then `now`, an
`ind=key|label|section|unit|value|date|previous|previous date|source` line
per indicator (the sections `Precios`, `Dólar`, `Tasas y BCRA` and
`Actividad`; the units `%`, `ARS`, `USDM`, `ARSM`, `pts` and `index`; the
numbers in the C form, a dot decimal; `-` for no previous reading), a
`hist=inflacion|v1,v2,...` line (the monthly inflation of the last 13
months, oldest first), `econ_missing=ids` when a source gave nothing,
`econ_age`, `econ_state`, `end`; at most 8 KB. `src/economia.js` holds the
catalogue (the same as the `dolar economia` command's) with each source's
id: the BCRA's statistics API v4 by variable, INDEC's series on datos.gob.ar,
DolarAPI's blue and oficial for the gap, ArgentinaDatos' country risk, all
public and keyless. This template answers with no source (state `none`); a
Worker with them fetches the readings into one cache entry and gives
`economiaText` that entry with the dollar quotes and the country risk.

## Tests and deploy

- `zsh dev.zsh test` (from the repo: `zsh worker/dev.zsh test`) runs every
  test in a throwaway container, the HTMLRewriter ones in workerd through
  miniflare; `zsh dev.zsh` then serves the Worker on the Mac's LAN address
  at port 8787 for a console on the same network (`board_url.txt` with that
  URL).
- `node --test test/*.test.js` runs all but the workerd tests, with no
  `npm install` (as CI does).
- The fixtures are public answers fetched on 2026-10-16 (Open-Meteo for
  Berlin, Frankfurter, Nager.Date for Germany) and made-up feeds and pages;
  the air quality and the fútbol day of the tests are made up too (fictional
  teams).
- Deploy: `npm ci && npx wrangler deploy`, after the routes and the plain
  HTTP setup of the main README.
