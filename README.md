# Couch

A PS3 homebrew app for the living-room TV: a dashboard of the time, the
weather, exchange rates, the next public holiday and the news, drawn at
1920 x 1080 by [ps3gfx](https://github.com/lucasdaddiego/ps3gfx) on the RSX
and the six SPUs. The data comes from a small Cloudflare Worker that you
deploy for your place (`worker/`).

<img src="docs/board.jpg" width="640" alt="The Board"> <img src="docs/waiting.jpg" width="640" alt="The waiting screen">

- **Board:** the clock in the console's time zone and the next holiday; the
  weather now with a moving icon, the rain of the next 2 hours, the next 12
  hours, the sun's arc, the moon and the air (AQI and UV), three days; a large rate tile with its 30-day chart and up to eight
  smaller tiles, each with its change since the day before; a news ticker.
  The sky behind the tiles follows the sun, the clouds and the rain.
- **News:** the latest headlines of your feeds. CROSS opens one in a reader
  with a QR code for the full article on a phone.
- **Futbol:** today's or tomorrow's games by league, with the time or the
  minute, the score and the TV channels; live games marked and refreshed
  every minute. It needs a Worker with `/futbol` (the template has the
  reply's format, not a source of fixtures).
- **Info:** the Board's last fetch, the console's time settings, the Cell
  and RSX temperatures and the fan duty, night mode's hours (TRIANGLE edits
  them), and the effects' and the display's times.
- **The waiting screen:** after 10 minutes with no button, a large clock, the
  weather, the next hours, the next holiday and one headline at a time, over
  the sky by day and an aurora by night. Every 2 minutes it fades out and back in at a slightly different place, to protect the TV from burn-in.
  In night mode's hours (`night.txt`) it is only a small dim clock on black.

<img src="docs/news.jpg" width="640" alt="The News page"> <img src="docs/article.jpg" width="640" alt="The reader">

(The screenshots come from the preview on the Mac, with the template
Worker's test data for Berlin and made-up headlines.)

## Install

1. A PS3 with PS3HEN or CFW. Download `Couch-<version>.pkg` from the
   [releases](https://github.com/lucasdaddiego/couch/releases) and install it
   with Package Manager (or webMAN).
2. Deploy the Board Worker for your place (below) and note its URL.
3. Put that URL in one line in `/dev_hdd0/tmp/couch/board_url.txt`, for
   example `http://board.example.com/board` (webMAN's FTP, or any FTP client).
   Without the file Couch shows these steps on the Board.
4. Start Couch from the Game column. L1/R1 change the page (Board, News,
   Futbol), SELECT opens Info, START exits.

Couch has no usable TLS, so the URL must be plain `http://`.

## The Board Worker

`worker/` is a Cloudflare Worker (the free plan is enough) that reads public
sources and answers Couch in one small text reply:

| | Source | Setting in `wrangler.jsonc` |
|---|---|---|
| Weather now, rain in the next 2 hours, 12 hours, 3 days, sun | [Open-Meteo](https://open-meteo.com) | `CITY`, `LAT`, `LON`, `TZ` |
| Air quality (US AQI) and UV index | [Open-Meteo](https://open-meteo.com) air quality | `LAT`, `LON`, `AIR_TTL` |
| Fixtures for the Futbol page and the header's futbol line | none in the template: `/futbol` documents the reply | |
| Exchange rates, 30-day chart, change | [Frankfurter](https://frankfurter.dev) (the ECB's reference rates) | `CURRENCY`, `RATES`, `FEATURE` |
| Next public holiday | [Nager.Date](https://date.nager.at) | `COUNTRY`, `HOLIDAY_NAMES` |
| Headlines and articles | your RSS feeds | `NEWS_FEEDS` |
| Number format | | `LOCALE` |

To deploy it:

1. Edit the vars in `worker/wrangler.jsonc` (each one is described there) and
   set `routes` to a host on a Cloudflare zone of yours.
2. Make that host answer plain HTTP. With **Always Use HTTPS** on, the zone
   redirects every `http://` request: turn it off and add one Single Redirect
   rule (HTTP to HTTPS) for every host but this one. A Configuration Rule
   cannot change that setting.
3. `cd worker && npm ci && npx wrangler deploy`.
4. Check it: `curl http://board.example.com/board` answers `couch-board 1`,
   key lines and `end`.

`worker/README.md` has the endpoints, the reply's keys, the cache and the
tests. Any server that answers the same text works too: Couch reads the keys
it knows and skips the rest.

## Files in /dev_hdd0/tmp/couch

| File | Content |
|---|---|
| `board_url.txt` | one `http://` line: the Board Worker's URL. Absent, or an `https://` URL or a bad line: the setup steps |
| `board_last.txt` | the last good Board reply: the next start shows it at once (same URL, at most 24 h old) until its first fetch |
| `couch.txt` | one block per start: the version, the URL, the network start, the first fetches, the reader's replies, the waiting screen, the frame times, the exit; emptied at start past 64 KB |
| `boot.txt` | a line per step of the start and of the exit, synced at once (it shows where a freeze stopped) |
| `starts.txt`, `journal.txt` | ps3gfx's freeze journal: each risky call is written before it runs, and a call that froze the console before is skipped (SQUARE twice on Info clears it). Since 0.5.0 also `temps` and `fan_policy`, the Info page's reads (syscalls 383 and 409, as PS3 Health makes them; the fan read asks first) |
| `safe_area.txt` | the TV's visible area, measured at the first start |
| `auto.txt` | for an unattended round: its seconds (60 to 600); Couch writes `0` at once |
| `night.txt` | `off` or `HH-HH` (for example `22-07`): night mode's hours, when the waiting screen is a small dim clock on black; absent: off. Info's Night row edits it (TRIANGLE) |

## Build

Couch builds with the SDK of [PS3 Health](https://github.com/lucasdaddiego/ps3-health)
(PSL1GHT's 2020 runtime and ps3gfx at the commit Health pins), and with its
checks:

```zsh
git clone https://github.com/lucasdaddiego/ps3-health ~/code/ps3-hdd-ssd-health
zsh ~/code/ps3-hdd-ssd-health/sdk_setup.sh          # the toolchain in ~/ps3dev
PS3_HEALTH=~/code/ps3-hdd-ssd-health zsh ./build.sh
```

`build.sh` checks the toolchain, the runtime and the ps3gfx stamp, runs
`make pkg`, audits the ELF's syscalls against `tools/syscalls.allow` and
checks the signed EBOOT against the ELF. Out: `couch.gnpdrm.pkg`.

## Development

- **Preview on the Mac:** `zsh tools/preview/build.zsh`, then
  `zsh tools/preview/run.zsh [scenario]` runs the app against ps3gfx's host
  backend and a fake Worker, and writes snapshots to
  `tools/preview/build/out` (PNG with Pillow). `PASSES=2` runs every
  scenario twice and compares the pictures; `AREA=narrow` uses the smallest
  visible area.
- **Host checks:** `zsh tools/hostcheck/run.zsh` builds the parsers
  (`source/parse.c`, `source/astro.c`) with the host's compiler, also under
  AddressSanitizer and UndefinedBehaviorSanitizer, and checks them against
  the Worker test's reply, the reader's replies and 20,000 mutated replies.
- **Worker tests:** `zsh worker/dev.zsh test` runs them in a throwaway
  container (`node --test` in `worker/` runs all but the workerd ones).

## Layout

`source/` (`main.c`: the start, the loop, the keys; `board.c`: the fetch
thread; `board_page.c`: the Board; `page.c`: the header, the waiting screen,
night mode, the background; `reader.c`: the News page and the reader;
`futbol.c`: the Futbol page; `info.c`: Info, the temperatures and the fan;
`parse.c`: the pure parsers, also built by the host checks; `sky.c`,
`spu_fx_run.c`, `astro.c`: the sky, the effects, the sun and the moon;
`http.c`, `clock.c`, `boot.c`, `stext.c`, `marquee.c`, `qrcodegen.c`),
`tools/preview/`, `tools/hostcheck/`, `tools/syscalls.allow`, `worker/`,
`Makefile`, `build.sh`, `sfo.xml`, `pkgfiles/`, `art/`, `docs/`.

## License

MIT (`LICENSE`). The pkg carries ps3gfx's fonts under the SIL Open Font
License (`OFL.txt`) and its icons under Phosphor's MIT license
(`PHOSPHOR.txt`); `source/qrcodegen.c` is Project Nayuki's QR Code generator
(MIT).
