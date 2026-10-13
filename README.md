# Couch

A PS3 homebrew app for the living-room TV, drawn by
[ps3gfx](https://github.com/lucasdaddiego/ps3gfx). Title `Couch`, APPID
`COUC00001`, app folder `/dev_hdd0/tmp/couch`, pkg `couch.gnpdrm.pkg`. The
design is `~/.claude/plans/2026-10-10-couch-design.md`. Lucas cut it to the
Board on 2026-10-10: a clock in the console's time zone, the dollar rates
and the weather, which come over plain HTTP from the Worker in `worker/`
(live on `http://couch.daddiego.com.ar/board`), and an Info screen. That
was 0.1.0, which passed round 1 on the console on 2026-10-11. Since
2026-10-12 the place is Junin: the weather now comes from SMN's station
there (else Open-Meteo), the three days from Open-Meteo.

0.2.0 (Lucas's choices of 2026-10-13) adds, on the same reply format
(version 1, new keys only):
- the rain of the next 12 h, today's sunrise and sunset and the moon's phase
  on the weather card;
- the money extras under the dollar rates: riesgo pais, the last monthly
  inflation (INDEC, with its month) and Banco Nacion's fixed-term rate (TNA);
- a News page: up to 8 headlines of two RSS feeds (Clarin and La Verdad of
  Junin), with their source and age;
- three pages with L1 and R1 (the Board, the News, the Info screen) and
  their dots in a new header with the clock;
- the internet's state in the header: ok, slow, or no internet since HH:MM;
- the ambient mode: after 10 min with no button the screen dims, the
  picture moves 6 px and the page changes every minute, so the TV is safe
  when Couch stays on;
- a new look on every page.
0.2.0 has not run on the console yet, and its Worker is not deployed (the
rounds to come are below).

Couch opens on the Board. L1 and R1 move between the three pages (R1 from
the Info page wraps to the Board). SQUARE refreshes the Board, SELECT opens
the Info page, START or Quit Game exits. On the Info page, SQUARE twice
clears the freeze journal and CIRCLE goes back to the Board. In the ambient
mode, the first button only wakes the screen.

What 0.2.0 does not do: no Music page and no LP10 client (no socket to port
2018, no `lp10.txt`, no address editor, no fake LP10, no `a` actions in
`auto.txt`); no TLS, so an `https://` URL is refused; no Config screen (the
Info page shows, it edits nothing); no setting on the console for the
city, the rates, the bank or the feeds (they are the Worker's vars: a change
is a deploy, not a console round); no headline opens (titles only).

## The pages (0.2.0)

- **The header** (every page, `page.c`): on the left the clock and the date,
  and the clock's source line under them; on the right the page's name with
  three dots (the page on the TV in BLUE), and under it the city (cut at 560
  px) and the internet's state with a dot: "Internet ok" (GREEN), "Internet
  slow (4.0 s)" (YELLOW), "No internet since 08:41" (RED), "No network"
  (RED), "Internet: checking" (before the first fetch). A rule under the
  header; the pages start at y 140. The look of 0.2.0: each card has a
  colour mark beside its title (the dollar GREEN, the weather BLUE, the news
  pink), rows are parted by a thin line instead of stripes, numbers keep
  tabular digits. Before and after: `tools/preview/build/before/` holds the
  0.1.0 snapshots, `tools/preview/build/out/` the new ones.
- **The internet's state**: the Board thread's fetches measure it. A fetch
  with no answer (the DNS lookup, the socket, the connect, the send or the
  recv failed, also the 10 s timeout) is "no internet", from the console's
  clock at the first such fetch after the last answered one. Any answer,
  also an HTTP error or a bad reply, reached the Board's host: then "slow"
  when the fetch took 3 s or more, else "ok". 3 s is four times round 1's
  0.75 s (the only fetch time measured on the console) and far under the
  10 s socket timeouts. The connect's time (the DNS lookup and the TCP
  connect) goes to `couch.txt` and the Info page. With `board_url.txt` on
  the LAN, "internet" means the Mac's Worker.
- **The clock**: the console's UTC (`sysGetCurrentTime`) plus the offset of
  the first source that works (design section 4): the console's time zone
  setting (TIMEZONE in minutes, plus 60 when SUMMERTIME is on, accepted from
  -720 to 840, PS3 Health's rule), then the Board's `offset` for its city,
  else UTC, and the source line says which and why. The time is in F_LARGE
  with the seconds and the date beside it. TIME_FORMAT picks 12 or 24 hours
  (0 is taken as 12 h, 1 as 24 h: not checked on the console).
- **The clock settings** (`tz_params`): `sysUtilGetSystemParamInt` for
  0x0115 (TIME_FORMAT), 0x0116 (TIMEZONE) and 0x0117 (SUMMERTIME), one
  journal entry, behind the first-run prompt as the design says. CIRCLE at
  the prompt writes `skip tz_params`, and the clock takes the Board's offset.
  PS3 Health 2.0.3 already reads TIMEZONE and SUMMERTIME at start, with no
  journal, and its round of 2026-10-10 got -180 here: the unit is minutes.
  TIME_FORMAT is new on this console. With no writable folder there is no
  journal to record a freeze, so Couch does not read the settings at all
  and takes the Board's offset.
- **The Board page, the dollar card**: one row per rate (label, buy, sell,
  the rate's own time), at most 8, in the Worker's order; the column heads
  on the title's row. Under a rule, the money extras in the same columns:
  the label, the value under Sell and its date under Updated ("Riesgo pais
  589 12 Oct", "Inflacion mensual 1,7% Aug 2026", "Plazo fijo BNA (TNA)
  19,25% 14:47": the fixed-term table has no date, so its date is the
  Worker's read). A stale extra has its date in YELLOW; a none extra shows
  "-" and "no data". The foot: "dolarapi.com, 43 s old" on the left,
  "argentinadatos.com" on the right. At most 48 px a row: 6 rates and 3
  extras fit at 1600 x 900.
- **The Board page, the weather card**: now (the temperature in F_LARGE, the
  words, day or night, feels like, humidity, wind); the sky: a raindrop and
  the rain of the next 12 h ("Rain now (75%)", "Rain from 18:00 (70%)" in
  BLUE, "No rain in the next 12 h" in GREY), "Sunrise 06:24, sunset 19:16"
  and the moon's phase on the right ("Waxing crescent moon"); then three
  days (words, max, min, the chance of rain). A Worker of before 2026-10-13
  sends no sky lines: the card shows what came. The foot of each card names
  its source and the data's age: the Worker's `usd_age` or `wx_age` plus
  the seconds since Couch got the reply. The weather's source is "SMN 23:00,
  days Open-Meteo" when the now line is SMN's (`wx_now`'s 7th field, the
  observation's time), else "Open-Meteo"; the age stays Open-Meteo's. A
  `stale` upstream shows its reason and the real age in YELLOW; a `none`
  upstream shows "No dollar data: <reason>." in the card.
- **The News page**: one card, a row per headline: the source (BLUE) and the
  age ("22 min", the Worker's age plus the seconds since the reply) on the
  left, the title in up to two lines. A row that does not fit whole is left
  off: 8 one-line rows fit at 1786 wide, 6 at 1600 wide with Clarin's long
  titles. The foot: the sources and the age ("Clarin, La Verdad, 5 min
  old"), or the first failed feed's reason in YELLOW. No headlines: "No
  news data: <reason>.", or "The Board's Worker sends no news" for a Worker
  with no feeds (or of before 2026-10-13).
- **The status line**: "Board updated at 09:00.", or the failure: "No Board
  data yet: <error>." before the first good reply, "The last fetch failed:
  <error>. The Board shows the data of 09:00." after one (the last good data
  stays). The errors in words: "DNS failed for <host>", "no connection to
  <host>:<port> (refused)", "no answer in 10 s", "the reply was cut (no end
  line)", "Board format 2: update Couch", "HTTP 301 to https://...: the host
  still redirects plain HTTP" (PS3 Health's redirect note: Always Use HTTPS
  back on; after a good reply the status line drops the Location, so the
  data's time fits at 1600 wide, and the Info screen and `couch.txt` keep
  it), "HTTP 503". No address: "No network: the console has no IP
  address." The network start failed: "No network: netInitialize failed (rc
  0x...). SQUARE tries again." It froze before: "Network not started:
  net_start froze this console before. SELECT info, then SQUARE twice clears
  the journal." The note line under the footer has the schedule on the
  right ("Next update in 4 min", "Next try in 58 s", "Refresh in 7 s") and,
  when `board_url.txt` is in use, the URL on the left.
- **The Info page** (page 3, also SELECT): the Board URL and its source, the
  last fetch (time, duration, HTTP status, bytes, fetches and failures, the
  schedule) and its result, the internet (ok or slow with the fetch's and
  the connect's time, or "no answer since 09:00"), the Worker's `now` and
  `offset` with the console's clock minus the Worker's; the clock's source
  and the three settings with their rcs; the console's address, the journal
  states of `net_start` and `tz_params`, the `text_utf8` rc; the folder's rcs
  when it is not writable. Leaving it (CIRCLE, L1 or R1) opens the Board as
  CIRCLE did in 0.1.0: after a clear of the journal the network start runs.
- **The ambient mode** (`page.c`; `AMBIENT_SEC` 600, `AMBIENT_STEP_SEC` 60,
  `AMBIENT_PX` 6 in `parse.h`): after 10 min with no pressed button, a black
  layer at 66 % covers the screen (a third of the light), and each minute
  the whole picture moves to the next place of a 3 x 3 grid of -6, 0 and 6
  px (one 6 px move a minute, back after 9 minutes: the move is the
  viewport's, as the visible area's, so the header and the footer move too)
  and the page changes between the Board and the News (the News only when
  the reply has headlines). This page change is the "rotation": it moves
  every card, every rule and every label of the screen, which a swap of
  two cards would not, and it needs no layout of its own. The Board thread
  goes on: the fetches every 5 min, the clock each second. Any button ends
  the mode, puts back the page of before and does nothing else (START does
  not exit, SQUARE does not refresh); Quit Game still quits. `couch.txt`
  gets a line at each start and end of the mode. An unattended round (at
  most 600 s) ends before the mode can start.

## How it works

- **The frame loop** (`main.c`): `read_pad`, the ambient mode's wake
  (`ambient_input`: a press that wakes the screen is cleared before the
  exit reads it), the exit, `board_take` (a new reply from the thread), the
  input (L1 and R1, the Info page's keys, SELECT, SQUARE), the ambient
  mode's step (`ambient_tick`), the draw of the page on the TV. Each page
  draws through `page_begin` (the ambient shift, `begin_frame`, the header)
  and `page_end` (the dim layer, `ui_flip`, the shift taken back). Only the
  main thread calls `journal_*` and writes files.
- **The network start** (`http.c`): PS3 Health's `net_start` (`mod_net.c` at
  5becd5d): `netInitialize`, the netctl module, `netCtlInit` and the first
  address read as one journaled call. The main thread writes `start
  net_start`, the calls run behind ps3gfx's job counter (`run_job`), the main
  thread writes the `done`. A frozen or skipped entry runs no network call
  at all. After SQUARE twice on the Info page, the start runs again when
  the Board opens. A failed `netInitialize` runs again at the next start
  (SQUARE, or leaving the Info page); the netctl module load and
  `netCtlInit` run until each succeeded once, `netCtlInit` only with the
  module loaded, the address read only after `netCtlInit` succeeded.
- **Quit Game** (the XMB's quit event, the `quit` flag the frame loop
  reads): the Board's entry reads the system's events, then does not start
  the network or the Board thread when the system asked Couch to quit, and
  reads the flag again after the network start, whose job screen draws
  frames (each frame reads the events). `couch.txt` says which.
- **The exit** (`leave`, from `exit()` once main returned): `board_stop`
  sets `board_on` to 0, shuts the open socket down (`netShutdown`,
  `SHUT_RDWR`) and joins the thread with no time limit. Then `net_end`:
  `netCtlTerm`, `netDeinitialize` (PSL1GHT's: `netFinalizeNetwork`, its
  128 KB pool, the net module), then the netctl module's unload, each only
  where its start succeeded (PSL1GHT's samples: httptest ends the libraries,
  then unloads the modules; ps3load joins its thread, then calls
  `netDeinitialize`, then ends the RSX). Its flags clear first, so a second
  call does nothing. Then `ui_end`. A failed join of a thread that had not
  left skips `net_end`.
- **The Board thread** (`board.c`), made as Cell Bench's temps sampler
  (priority 1500, joinable; a 128 KB stack, PS3 Health's job thread's, which
  ran the same DNS and recv on this console): it fetches at start, every 5
  min after a whole reply (also one of another version), 60 s after a
  failure, and at SQUARE, at most once in 10 s. It reads `want` and
  `board_on` every 50 ms and between the steps of a fetch. The handoff is one
  record and a ready word (Cell Bench's `run.c`): the thread fills the record
  only while `ready` is 0, then sets it after a fence; the main thread parses
  the body into a second record and swaps it in only when line 1 is
  `couch-board 1` and the `end` line came, then reads the console's address
  again (`netCtlGetInfo`) while the thread waits: Couch makes no two network
  calls at once. The thread never journals and never writes a file. At the
  exit, a thread in a recv or a send returns at once (the shutdown; the
  10 s socket timeouts in any case), one in its 50 ms sleep after the
  sleep. A DNS lookup and a connect do not end at a shutdown: they run to
  the resolver's or the TCP stack's own timeout, not measured on this
  console, so the exit can wait that long (the last frame stays on the
  TV). The thread reads `board_on` after each step and makes no further
  call.
- **The HTTP client** (`http.c`): Health's `resolve`, `tcp_connect` and GET
  loop. `GET /board HTTP/1.0` with `Host:` (with the port when it is not 80),
  `User-Agent: Couch/0.2.0` and `Connection: close`; `SO_RCVTIMEO` and
  `SO_SNDTIMEO` of 10 s; status 200 needed; a head of at most 4 KB and a body
  of at most 4 KB; reading stops at the close or at the body's `end` line
  (`wrangler dev` closes about 5 s after the body). `Content-Length` is not
  used; a chunked answer is refused. Since 0.2.0 it keeps the connect's time
  (`connect_us`: from the GET's start to the connect's end, the DNS lookup
  included).
- **The reply** (`parse.c`, format version 1, design section 4): `key=value`
  lines after `couch-board 1`, `|` between fields, `end` last. An unknown key
  or a line with no `=` is skipped, a repeated key keeps its order, at most 8
  rate lines and 3 day lines, a missing field reads `-`. `wx_now` has a 7th
  field since 2026-10-12, the now line's source; absent or empty, it reads
  as no source (the foot says "Open-Meteo"). A Couch that splits 6 fields
  reads "day" from `day|SMN 23:00`: each field ends at its `|`. Text fields
  lose the bytes below 0x20 and 0x7f and are cut on a code point boundary.
  The keys of 2026-10-13, which 0.1.0's parser skips (checked: 0.1.0's
  `parse.c` reads the new reply of 1595 bytes as `BP_OK` with every old
  field), so the version stays 1: `money=label|value|date|state|reason` (at
  most 4; a state that is not `ok` or `stale` is none), `wx_rain=rain|words`
  or `dry|words`, `wx_sun=rise|set`, `wx_moon=words`,
  `news=source|age|title` (at most 8; the age in seconds or `-`, a line with
  no title skipped, a title of up to 335 bytes), `news_age`, `news_state`
  (absent: the Worker has no feeds). The reply stays at most 4096 bytes:
  0.1.0 refuses a longer body whole ("the reply is over 4 KB"), so a larger
  buffer in 0.2.0 would not help an old Couch. The Worker's guard drops the
  news first.
- **UTF-8**: Couch calls `text_utf8()` after `ui_init`. When it fails (a bad
  `latin.bin`, short RSX memory), the parser folds the text to ASCII: a
  Latin-1 letter becomes its base letter, the degree sign is dropped (every
  temperature has one: the design said `?`, which read "11?"), a no-break
  space becomes a space, any other code point becomes `?`.

## Files in /dev_hdd0/tmp/couch

| File | Written by | Content |
|---|---|---|
| `starts.txt`, `journal.txt` | ps3gfx | `start` per start; the journal's `start`, `done`, `froze` and `skip` lines (`net_start`, `tz_params`) |
| `safe_area.txt` | ps3gfx | the visible area, measured at the first start |
| `gfx_init.txt` | ps3gfx, only when the picture cannot start | the failed step |
| `board_url.txt` | FTP only, for test rounds | one `http://` line, e.g. `http://192.168.0.3:8787/board`. Absent: `http://couch.daddiego.com.ar/board`. An `https://` URL or a bad line: the default, and the note line says why |
| `auto.txt` | FTP, for unattended rounds; Couch writes `0` at once | the seconds of a round, 60 to 600 |
| `couch.txt` | Couch, appended | one block per start: the version and the ps3gfx stamp, the UTC time, `text_utf8`'s rc, the URL, the network start (the rcs of `netInitialize`, the netctl module load and `netCtlInit`) and the address, a quit before the Board opened, the three settings with their rcs, the first 50 fetches (time, duration, the connect's time or "no connection", HTTP status, bytes, the result, the Worker's `now` and `offset`, the console's clock minus the Worker's, the headlines), the ambient mode's starts and ends, the shots, the exit (a line before and after the Board thread's join, with its time; a line before and after `net_end`'s calls), `ui_stats` and the exit's time. Emptied at start past 64 KB |
| `board.ppm`, `info.ppm` | Couch, unattended only (`ui_shot`) | the two screens |

An unattended round (Cell Bench's rule): `auto.txt` holds the seconds; Couch
reads it before the visible-area screen, writes `0` into it at once, skips
the visible-area screen and the clock's prompt (the round is the consent:
`tz_params` runs, journaled), takes `board.ppm` at 30 s, opens the Info
page at 40 s, takes `info.ppm` at 45 s, goes back to the Board at 50 s and
quits to the XMB at the file's seconds. A file that is not a number (or no
writable folder): a line in `couch.txt`, the Board with the reason for 30
s, then the XMB.

## Build

PS3 Health's SDK and checks, from the Health folder (`$PS3_HEALTH`, default
`~/code/ps3-hdd-ssd-health`; Lucas, 2026-10-09: no copies), as Cell Bench:

```zsh
PS3DEV=~/ps3dev PSL1GHT=~/ps3dev PYTHON=~/.local/share/ps3dev/venv/bin/python3 zsh ./build.sh
```

`build.sh` checks the toolchain, the 2020 runtime and the ps3gfx stamp
against the commit Health's `sdk_setup.sh` pins (`PS3GFX_DEV=1` skips it;
Couch needs `text_utf8`, `fs_read_line` and `ui_shot`, ps3gfx 6b94bbd or
later), copies ps3gfx's `OFL.txt` into the pkg, runs `make pkg`, then
Health's `tools/syscalls.py --allow tools/syscalls.allow` and
`verify_self.py` (0.2.0 on 2026-10-13: 39 syscalls at 78 call sites, all
allowed; the EBOOT matches the ELF; no compiler warning). The allowlist has
39 numbers, every `sc` resolved: PS3
Health's runtime set without its drive, temperature and fan calls (libnet,
libnetctl, `sysModuleLoad` and `sysUtilGetSystemParamInt` are SPRX
imports; the exit's `netShutdown`, `netDeinitialize`, `netCtlTerm` and
`sysModuleUnload` add no number). Out: `couch.gnpdrm.pkg` (about
245 KB) and the unstripped `couch.elf`.

`art/make_icon.py <Inter variable TTF>` draws `pkgfiles/ICON0.PNG` (a sofa,
"Couch" and "Board", in PS3 Health's icon style); the font is
`fonts/InterVariable.ttf` in the ps3sdk archive.

## Preview on the Mac

```zsh
zsh tools/preview/build.zsh                 # ps3gfx's preview: build/pv, and build/pv_utf8_off (-DCOUCH_UTF8_OFF)
zsh tools/preview/run.zsh                   # 32 scenarios at the default visible area (about 90 s: ambient runs 12 min of frames)
AREA=narrow zsh tools/preview/run.zsh       # the same at the narrowest area, 1600 x 900 (snapshots *_narrow; not tv)
PASSES=2 zsh tools/preview/run.zsh          # twice, and the snapshots that differ
```

The PNGs need Pillow: `PYTHON=~/.local/share/ps3dev/venv/bin/python3` (else the snapshots stay
`.ppm`). `tools/preview/build/before/` keeps the 0.1.0 snapshots (2026-10-12) for the look's
before and after.

`tools/preview/pv_app.c` is the Board's HTTP peer: a connect to port 80 or
8787 gets one answer and a close, every other port refuses. Each request
goes to `peer.log` in the scenario's sandbox (`build/sbx/<name>`).
`PV_BOARD=<file>` is the body (default: the Worker test's reply), and
`PV_BOARD_FAIL` gives each connect its answer, comma-separated, the last
repeating: `ok`, `refused`, `silent` (the 10 s timeout at once), `cut` (the
body without its last 30 bytes), `301`, `v2`, `hang` (the recv waits until a
`netShutdown`, at most 30 s of host time), `slow` (the answer comes 4 s of
the preview's clock after the connect). `PV_TZ`, `PV_SUMMER`,
`PV_TIMEFMT` set the three settings (default -180, 0, 1), `PV_TZ_FAIL=1`
fails them, `PV_NOIP=1` takes the address away, `PV_NET_FAIL=1` fails
`netInitialize`, `PV_NETCTL_FAIL=1` fails `netCtlInit`, `PV_QUIT=net` sets
`quit` inside `netInitialize` (the preview's system sends no events, so a
quit before the first frame is not testable here). `peer.log` also gets a
`pv: ` line for each `netCtlInit` and each exit call (`netShutdown`,
`netCtlTerm`, `netDeinitialize`, `sysModuleUnload`) and the end of a hang;
`board_ok`, `net_fail` and the three exit scenarios print them with their
`couch.txt` lines. The library's `sysModuleLoad` is not weak, so a failed
module load is not testable here. The preview has no DNS: the network library's host entry
holds 32-bit pointers, which a 64-bit host cannot fake, so the scenarios
write `board_url.txt` with `http://192.168.0.3:8787/board` (the console
round's path) and `board_dns` shows the default host's DNS failure (so
`PV_BOARD_FAIL` has no `dns`: the design's list had one). The
preview's clock starts at 2026-10-08 12:00 UTC (09:00 at UTC-3), five days
before the reply's `now`, so the Info page says the console's clock is 5 d
5 h behind the Worker's. `run.zsh` takes `body_ok` from the Worker test's
first reply (the host check's way, written to `build/worker_reply.txt`) and
makes the other bodies from it line by line, so the sources stay ASCII and
a format change in the Worker shows here too.
`text_utf8` cannot fail at run time in the preview, so `build.zsh` makes a
second binary with `-DCOUCH_UTF8_OFF` for `utf8_off`.

Scenarios: `board_ok` (the clock, both cards, the Info page, back; the
request line), `board_stale` (the dollar card's foot in YELLOW),
`board_none` (both upstreams without data), `pages` (R1 to the News and the
Info page, R1 wraps to the Board, L1 back to the Info page),
`board_extras` (riesgo pais none, the fixed-term rate stale, a dry
forecast, the feeds failed: the News page's reason), `board_v1` (a reply of
0.1.0's time: no extras, no sky, "sends no news"), `net_slow` (a 4 s answer:
"Internet slow (4.0 s)"), `net_down` (a good reply, then SQUARE and a
refused connect: "No internet since 09:00"), `ambient` (`PV_MAXFRAMES=44000`:
10 min with no button, the Board dimmed, a minute on the News moved 6 px,
then the Board moved again; START wakes the screen and Couch does not exit;
3 requests in the 12 min, the `couch.txt` lines), `board_smn` (Junin, U+00ED, with the now
line from SMN: the foot "SMN 23:00, days Open-Meteo") and
`board_smn_utf8_off` (the same folded: "Junin"), `board_301`, `board_stale301` (a
good reply, SQUARE after 10 s, a 301: the status line keeps the data's time,
the Info screen the Location), `board_cut` (a good reply, SQUARE after 10 s,
a cut one: the old Board stays), `board_v2`,
`board_silent`, `board_refused`, `board_dns`, `frozen` (`start net_start` in
the journal: the Board says so; SELECT, SQUARE twice, CIRCLE: the start runs
and the Board fills), `tz_skip` (the prompt, CIRCLE, the Board's offset),
`tz_12h` (CROSS, TIME_FORMAT 0 at UTC+5:30), `utf8_off` (the fold) and
`accents` (the same reply in UTF-8), `refresh` (SQUARE 3 s after the first
fetch: "Refresh in 7 s", then the second request at the 10 s mark),
`net_fail` (CIRCLE and SQUARE try again: one `netCtlInit`; the exit ends
netctl only), `netctl_fail` (the Board runs, the address is not read, no
`netCtlTerm`), `hang` (START while the fetch waits in recv: the shutdown
ends the recv, the join, `net_end`), `quit_net` (Quit Game during the
network start: no Board thread, no fetch, `net_end`), `noip`, `nofolder`
(the app folder is a file: no journal, no counter, no prompt), `auto` (`auto.txt` 60: the shots at 30 and 45 s, the
exit at 60 s, `couch.txt`), `tv` (Lucas's area, `0 2 1920 1078`). Every text
fits at 1786 and at 1600 wide (at 1600 the News page shows 6 of the 8
headlines, two of them in two lines). With `PASSES=2`, the 62 snapshots repeat byte
for byte except, at times, an Info page after a fetch (`board_cut_info` on
2026-10-13; before, also `board_stale301_info`, `tz_12h_info`,
`tz_skip_info`): the Board thread runs in host time while the preview's
clock moves per frame, so a fetch's duration reads 0.00 or 0.02 s.

## The host checks

```zsh
zsh tools/hostcheck/run.zsh          # a few seconds: PASS or FAIL per group, the exit status counts the failures
```

`tools/hostcheck/board_check.c` checks `source/parse.c` with the host's cc
(`-std=c11 -Wall -Wextra -Werror`), and again under AddressSanitizer and
UndefinedBehaviorSanitizer. `run.zsh` takes the Worker test's reply from
`worker/test/board.test.js` (its first test's expected text, line for line),
so a format change in the Worker fails here too. The groups: every field of
that reply, the reply folded, CR LF line ends, the reply cut at every byte
(only the cut before the last LF has the end line), the version line
(version 2 and 10, not a Board, no end), unknown keys and lines with no `=`,
nothing after `end`, at most 8 rate and 3 day lines, long fields cut on a
code point boundary, a body of exactly 4096 bytes, the states and their
reasons, missing fields, `wx_now`'s 7th field (the source, absent or
empty as no source, an 8th field dropped, 24 accented letters of words),
control bytes; the fold; `http_head` (the status,
Location, chunked, an incomplete head, not HTTP, bare LF); `url_split` (a
port, no path, https, bad ports, spaces, a long host); the clock (dates and
weekdays from 1969 to 2038, 12 and 24 hours, the UTC label, the TIMEZONE
range); the ages, the error words, `auto.txt`, the end line; the keys of
2026-10-13 (the money lines and their states, at most 4; `wx_rain` rain or
dry, `wx_sun` with both times, `wx_moon`; the news lines, at most 8, no
title skipped, the age or -1, the state, a long title cut whole);
`net_quality` (slow from 3 s) and the ambient mode's shift (each place of
the 3 x 3 grid once, 6 px a step) and page; and, in the sanitizer build,
20,000 mutated replies with every field inside its buffer. Last,
`old_check.c` builds Couch 0.1.0's `parse.c` (commit bcf4243, read with
`git show`) and parses the same reply: within 4 KB, `BP_OK`, every old field.
The repository's CI runs no app's C host checks, so this one is not in CI.

## The console rounds

Round 1, 2026-10-11: rc1 (d6e171a), unattended, on the deployed Worker (no
`board_url.txt`): passed. `auto.txt` "60", then `ps3run round` (`pull/rc1`). The
start did not take: `/play.ps3?COUC00001`, 19 s after the install, started the
icon that had the XMB's focus (Cell Bench) instead of the new app. Cell Bench
was quit with `/xmb.ps3$exit`, and webMAN's virtual pad started Couch from its
icon (UP, CROSS). The run: `net_start` and `tz_params` done with rc 0, no
freeze; the console resolved `couch.daddiego.com.ar` and fetched the Board in
0.75 s (HTTP 200, 467 bytes, offset -10800); TIMEZONE -180, SUMMERTIME 0,
TIME_FORMAT 1 (24-hour); the console's clock 27 s ahead of the Worker's; 57.3
fps over 60 s, the 2 frames over 25 ms are the two shots (1.3 s each). Both
shots read right: the clock, six rates and the weather with their ages, and
the Info screen.

## The console rounds to come

Round 1, unattended, with no deploy (the design's round 1 without the LP10):
1. On the Mac, start the Worker on the LAN: `zsh worker/dev.zsh` (a
   container from a git archive: commit first).
2. By FTP, list `/dev_hdd0/game` (`COUC00001` must be absent) and make
   `/dev_hdd0/tmp/couch`. Write `board_url.txt` = `http://<the Mac's
   IP>:8787/board` and `auto.txt` = `150`.
3. `ps3run round` in this folder: build, upload, install, start, the journal
   tail, the pull. The pkg goes up as `Couch-0.1.0-rcN.pkg`.
4. Couch runs 150 s: the shots at 30 and 45 s, then the XMB.
5. Get the two PPMs by FTP (`ps3run pull` takes only `*.txt`).
6. Check: `net_start` and `tz_params` done and ok in the journal, no
   `froze`; the fetches in `couch.txt` ok with HTTP 200; the three raw
   settings (TIMEZONE -180, TIME_FORMAT 0 or 1 against the console's clock
   setting) and the Worker's `offset=-10800`; the console's clock against
   the Worker's `now`; `ui_stats` near 60 fps, slow frames only at the shots
   and the journal frames; the shots against the preview.

Round 2, on the deployed Worker:
1. Delete `board_url.txt`, so Couch uses `http://couch.daddiego.com.ar/board`.
   Delete `journal.txt` too (round 1's pull has a copy): round 1 ran
   `tz_params` with no prompt, and the clock's prompt shows only for a call
   the journal does not know.
2. A start by hand: the visible-area screen (round 1 skipped it: CROSS
   saves the area), the clock's prompt (CROSS), the Board, the Info screen,
   SQUARE (a refresh), SQUARE again within 10 s ("Refresh in N s").
3. The exit during a fetch, which no round has tested (since 2026-10-11 the
   exit shuts the socket down and waits for the thread with no limit):
   a. On the Mac, `nc -l 8787` (it accepts and never answers), and
      `board_url.txt` = `http://<the Mac's IP>:8787/board`. Start Couch and
      press START while the status line says "Fetching the Board ..." (at
      most 10 s): the thread is in its recv, the shutdown ends it, and
      Couch exits at once (`board thread: joined in 0.0N s, it was in a
      fetch`).
   b. `board_url.txt` with a dead address (`http://192.168.0.250:8787/board`),
      START during a fetch: the thread is in the connect, which a shutdown
      may not end, so the exit waits for the console's connect timeout.
      `couch.txt` has the time (`board thread: joined in N s, it was in a
      fetch`): that measures the timeout. Then `net_end`'s line, and the
      XMB.

Round 3, 0.2.0 (the pkg goes up as `Couch-0.2.0-rcN.pkg`):
1. The Worker of 2026-10-13: commit, then `zsh worker/dev.zsh test` (the
   tests run only in its container). Then either the LAN Worker (`zsh
   worker/dev.zsh` and `board_url.txt`) or the deploy, which needs Lucas's
   OK. The LAN Worker also shows whether Clarin and La Verdad answer
   `wrangler dev`'s fetch.
2. Unattended, `auto.txt` = `150`, `ps3run round`: `board.ppm` with the
   money extras and the sky, `info.ppm` with the Internet row; in
   `couch.txt`, the fetch lines with "connect N s" and "8 news".
3. By hand: R1 to the News (the accents of the titles), R1 to the Info page,
   R1 back to the Board, L1. Then no button for 10 min: the screen dims; a
   minute later the News, moved 6 px; START wakes the screen and Couch does
   not exit; `couch.txt` has the two `ambient:` lines. On the TV, check the
   dim layer's strength and the 6 px moves.
4. The internet: `board_url.txt` with a dead address: "No internet since
   HH:MM" after the first fetch. The fetch and connect times in `couch.txt`
   tell whether 3 s (`NQ_SLOW_US`) fits this line.

## Layout

`Makefile`, `build.sh`, `sfo.xml`, `pkgfiles/ICON0.PNG`, `art/make_icon.py`,
`source/` (`main.c`: the start, the loop, the pages' keys, the unattended
round, `couch.txt`; `board.c`: the thread, the handoff, the internet's
state, the Board page; `page.c`: the header, the ambient mode, the News
page; `http.c`: the network start and the HTTP client; `clock.c`: the
settings, the offset, the clock; `info.c`: the Info page; `parse.c`: the
pure functions, also built by the host check), `tools/preview/`,
`tools/hostcheck/` (with `old_check.c`, 0.1.0's parser), `tools/syscalls.allow`,
and `worker/` (the Board's Worker, its own README).
