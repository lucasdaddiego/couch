# Couch's Board Worker

A Cloudflare Worker that answers Couch in plain text over plain HTTP (the PS3
has no usable TLS). It reads public sources, keeps each answer in the Cache
API, and formats one reply of at most 4 KB.

| Endpoint | Reply |
|---|---|
| `GET /board` | the Board: the place, the rates, the weather, the next holiday, 8 headlines |
| `GET /news` | the News page: up to `NEWS_LIST` (30) headlines with their ids, categories and summaries |
| `GET /article?id=<8 hex>` | one headline's article: its page's paragraphs (at most 12 KB), else the feed's summary |

Every reply is 200 with the state in its body; any other path or method is
404. `/article` fetches only the link of an item in a configured feed, so the
Worker is no open proxy.

## Settings

All in `wrangler.jsonc` as vars (no secrets); its comment describes each one.

| Var | Use |
|---|---|
| `CITY`, `LAT`, `LON`, `TZ` | the place: the city's name, the coordinates for Open-Meteo, an IANA time zone |
| `LOCALE` | the numbers' format (BCP 47, for example `de-DE`, `en-US`) |
| `CURRENCY`, `RATES`, `FEATURE` | your currency, up to 8 others (`USD,GBP:Pound,JPY`), the large tile's code |
| `COUNTRY`, `HOLIDAY_NAMES` | the holidays' country (ISO 3166 alpha-2), `local` or `english` names |
| `NEWS_FEEDS`, `NEWS_MAX`, `NEWS_LIST` | `Label*weight@category=URL` between commas, at most 6; the Board's and the News page's counts |
| `WX_TTL`, `FX_TTL`, `NEWS_TTL`, `ARTICLE_TTL` | seconds an answer stays fresh (900, 3600, 600, 3600) |

An empty `RATES`, `COUNTRY` or `NEWS_FEEDS` leaves that part out.

## The sources

- **Open-Meteo** (`api.open-meteo.com`): the current weather, 4 days of daily
  values and the hourly values, in one request.
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
`/board` with 3 feeds makes 6 fetches and 12 cache calls, far below the free
plan's 50 subrequests (6 feeds and both holiday years: 28).

## The Board reply

UTF-8 text: the line `couch-board 1`, then `key=value` lines (fields between
`|`), then `end`. Couch skips a key it does not know. A reply over 4 KB drops
lines from the end: the news first, then the holiday and the chart, the
hours, the changes, the sky lines, then the rates and days.

| Key | Fields |
|---|---|
| `now`, `offset`, `city` | the Worker's Unix time, the time zone's offset in seconds, the city |
| `numfmt` | the thousands mark (`_` for none) and the decimal mark of the numbers |
| `geo` | `lat\|lon` |
| `rate` | `label\|buy\|value\|date` (`buy` is `-` for a mid rate) |
| `feature` | `label\|symbol`: the large tile's rate, `$` for Couch's dollar icon, other text as text, empty for none |
| `rate_chg`, `extra_chg` | `label\|up, down or flat\|percent\|amount\|date` |
| `spark` | `label\|first date\|30 values\|decimals` (the values times 10^decimals, `-` for none) |
| `extra` | `label\|value\|date\|state\|reason`: a value of another kind (an index, a rate of interest); this Worker sends none |
| `rate_age`, `rate_state` | the rates' age in seconds, `ok` or `stale\|reason` or `none\|reason` |
| `wx_now` | `temperature\|feels like\|humidity\|words\|wind\|day or night\|source` |
| `wx_fx` | `WMO code\|cloud cover\|mm per hour\|wind from (degrees)\|wind km/h\|gusts km/h` |
| `day`, `days_fx` | `label\|words\|max\|min\|chance of rain`, and each day's WMO code |
| `wx_rain`, `wx_sun`, `wx_moon` | `rain or dry\|words`, `sunrise\|sunset`, the moon's phase in words |
| `wx_hours` | `first hour\|12 temperatures\|12 chances\|12 WMO codes` |
| `wx_age`, `wx_state` | as the rates' |
| `holiday` | `date\|name\|days until it` |
| `news`, `news_age`, `news_state` | `source\|age in seconds\|title`, as the rates' |

`/news` is `couch-news 1`, then `item=id|source|category|age|title|summary`
lines, `news_age`, `news_state`, `end`. `/article` is `couch-article 1`, then
`id`, `source`, `category`, `title`, `date` (`words|age`), `link`, `state`
(`ok`, `cut`, `summary|reason` or `none|reason`) and one `p=` line per
paragraph, then `end`.

## Tests and deploy

- `zsh dev.zsh test` (from the repo: `zsh worker/dev.zsh test`) runs every
  test in a throwaway container, the HTMLRewriter ones in workerd through
  miniflare; `zsh dev.zsh` then serves the Worker on the Mac's LAN address
  at port 8787 for a console on the same network (`board_url.txt` with that
  URL).
- `node --test test/*.test.js` runs all but the workerd tests, with no
  `npm install` (as CI does).
- The fixtures are public answers fetched on 2026-10-16 (Open-Meteo for
  Berlin, Frankfurter, Nager.Date for Germany) and made-up feeds and pages.
- Deploy: `npm ci && npx wrangler deploy`, after the routes and the plain
  HTTP setup of the main README.
