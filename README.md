# Couch

A PS3 homebrew app for the living-room TV, drawn by
[ps3gfx](https://github.com/lucasdaddiego/ps3gfx). Title `Couch`, APPID
`COUC00001`, app folder `/dev_hdd0/tmp/couch`, pkg `couch.gnpdrm.pkg`. The
design is `~/.claude/plans/2026-10-10-couch-design.md`. Lucas cut it to the
Board on 2026-10-10, so 0.1.0 is the Board only: a clock in the console's
time zone, the dollar rates and the weather, which come over plain HTTP from
the Worker in `worker/` (live on `http://couch.daddiego.com.ar/board`), and
an Info screen. 0.1.0 has not run on the console yet (the rounds to come are
below).

Couch opens on the Board. SQUARE refreshes it, SELECT opens the Info screen,
START or Quit Game exits. On the Info screen, SQUARE twice clears the freeze
journal and CIRCLE goes back to the Board.

What 0.1.0 does not do: no Music page and no LP10 client (no socket to port
2018, no `lp10.txt`, no address editor, no fake LP10, no `a` actions in
`auto.txt`); no TLS, so an `https://` URL is refused; no Config screen (the
Info screen shows, it edits nothing); no setting on the console for the
city or the rates (they are the Worker's vars: a change is a deploy, not a
console round).

## The Board (0.1.0)

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
- **The dollar card**: one row per rate (label, buy, sell, the rate's own
  time), at most 8, in the Worker's order. **The weather card**: now (the
  temperature in F_LARGE, the words, day or night, feels like, humidity,
  wind) and three days (words, max, min, the chance of rain). The foot of
  each card names its source and the data's age: the Worker's `usd_age` or
  `wx_age` plus the seconds since Couch got the reply. A `stale` upstream
  shows its reason and the real age in YELLOW; a `none` upstream shows "No
  dollar data: <reason>." in the card.
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
- **The Info screen**: the Board URL and its source, the last fetch (time,
  duration, HTTP status, bytes, fetches and failures, the schedule) and its
  result, the Worker's `now` and `offset` with the console's clock minus the
  Worker's; the clock's source and the three settings with their rcs; the
  console's address, the journal states of `net_start` and `tz_params`, the
  `text_utf8` rc; the folder's rcs when it is not writable.

## How it works

- **The frame loop** (`main.c`): `read_pad`, the exit, `board_take` (a new
  reply from the thread), the input, the draw. Only the main thread calls
  `journal_*` and writes files.
- **The network start** (`http.c`): PS3 Health's `net_start` (`mod_net.c` at
  5becd5d): `netInitialize`, the netctl module, `netCtlInit` and the first
  address read as one journaled call. The main thread writes `start
  net_start`, the calls run behind ps3gfx's job counter (`run_job`), the main
  thread writes the `done`. A frozen or skipped entry runs no network call
  at all. After SQUARE twice on the Info screen, the start runs again when
  the Board opens.
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
  calls at once. The thread never journals and never writes a file. The exit waits at most 1 s for the thread
  (`board_stop`); a thread still in DNS, connect or recv does not hold it,
  and `exit()` ends the process.
- **The HTTP client** (`http.c`): Health's `resolve`, `tcp_connect` and GET
  loop. `GET /board HTTP/1.0` with `Host:` (with the port when it is not 80),
  `User-Agent: Couch/0.1.0` and `Connection: close`; `SO_RCVTIMEO` and
  `SO_SNDTIMEO` of 10 s; status 200 needed; a head of at most 4 KB and a body
  of at most 4 KB; reading stops at the close or at the body's `end` line
  (`wrangler dev` closes about 5 s after the body). `Content-Length` is not
  used; a chunked answer is refused.
- **The reply** (`parse.c`, format version 1, design section 4): `key=value`
  lines after `couch-board 1`, `|` between fields, `end` last. An unknown key
  or a line with no `=` is skipped, a repeated key keeps its order, at most 8
  rate lines and 3 day lines, a missing field reads `-`. Text fields lose the
  bytes below 0x20 and 0x7f and are cut on a code point boundary.
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
| `couch.txt` | Couch, appended | one block per start: the version and the ps3gfx stamp, the UTC time, `text_utf8`'s rc, the URL, the network start and the address, the three settings with their rcs, the first 50 fetches (time, duration, HTTP status, bytes, the result, the Worker's `now` and `offset`, the console's clock minus the Worker's), the shots, `ui_stats` and the exit. Emptied at start past 64 KB |
| `board.ppm`, `info.ppm` | Couch, unattended only (`ui_shot`) | the two screens |

An unattended round (Cell Bench's rule): `auto.txt` holds the seconds; Couch
reads it before the visible-area screen, writes `0` into it at once, skips
the visible-area screen and the clock's prompt (the round is the consent:
`tz_params` runs, journaled), takes `board.ppm` at 30 s, opens the Info
screen at 40 s, takes `info.ppm` at 45 s, goes back to the Board at 50 s and
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
`verify_self.py`. The allowlist has 39 numbers, every `sc` resolved: PS3
Health's runtime set without its drive, temperature and fan calls (libnet,
libnetctl, `sysModuleLoad` and `sysUtilGetSystemParamInt` are SPRX
imports). `board_stop`'s wait calls `sysUsleep` through an out-of-line
function: inline, gcc rotated that loop and put the call's `li r11` after
its `sc`, which the scanner cannot resolve. Out: `couch.gnpdrm.pkg` (about
240 KB) and the unstripped `couch.elf`.

`art/make_icon.py <Inter variable TTF>` draws `pkgfiles/ICON0.PNG` (a sofa,
"Couch" and "Board", in PS3 Health's icon style); the font is
`fonts/InterVariable.ttf` in the ps3sdk archive.

## Preview on the Mac

```zsh
zsh tools/preview/build.zsh                 # ps3gfx's preview: build/pv, and build/pv_utf8_off (-DCOUCH_UTF8_OFF)
zsh tools/preview/run.zsh                   # 21 scenarios at the default visible area
AREA=narrow zsh tools/preview/run.zsh       # the same at the narrowest area, 1600 x 900 (snapshots *_narrow; not tv)
PASSES=2 zsh tools/preview/run.zsh          # twice, and the snapshots that differ
```

`tools/preview/pv_app.c` is the Board's HTTP peer: a connect to port 80 or
8787 gets one answer and a close, every other port refuses. Each request
goes to `peer.log` in the scenario's sandbox (`build/sbx/<name>`).
`PV_BOARD=<file>` is the body (default: the Worker test's reply), and
`PV_BOARD_FAIL` gives each connect its answer, comma-separated, the last
repeating: `ok`, `refused`, `silent` (the 10 s timeout at once), `cut` (the
body without its last 30 bytes), `301`, `v2`. `PV_TZ`, `PV_SUMMER`,
`PV_TIMEFMT` set the three settings (default -180, 0, 1), `PV_TZ_FAIL=1`
fails them, `PV_NOIP=1` takes the address away, `PV_NET_FAIL=1` fails
`netInitialize`. The preview has no DNS: the network library's host entry
holds 32-bit pointers, which a 64-bit host cannot fake, so the scenarios
write `board_url.txt` with `http://192.168.0.3:8787/board` (the console
round's path) and `board_dns` shows the default host's DNS failure (so
`PV_BOARD_FAIL` has no `dns`: the design's list had one). The
preview's clock starts at 2026-10-08 12:00 UTC (09:00 at UTC-3), two days
before the reply's `now`, so the Info screen says the console's clock is 1
d 18 h behind the Worker's. `run.zsh` writes the reply files at run time
(the degree sign is the bytes C2 B0), so the sources stay ASCII.
`text_utf8` cannot fail at run time in the preview, so `build.zsh` makes a
second binary with `-DCOUCH_UTF8_OFF` for `utf8_off`.

Scenarios: `board_ok` (the clock, both cards, the Info screen, back; the
request line), `board_stale` (the dollar card's foot in YELLOW),
`board_none` (both upstreams without data), `board_301`, `board_stale301` (a
good reply, SQUARE after 10 s, a 301: the status line keeps the data's time,
the Info screen the Location), `board_cut` (a good reply, SQUARE after 10 s,
a cut one: the old Board stays), `board_v2`,
`board_silent`, `board_refused`, `board_dns`, `frozen` (`start net_start` in
the journal: the Board says so; SELECT, SQUARE twice, CIRCLE: the start runs
and the Board fills), `tz_skip` (the prompt, CIRCLE, the Board's offset),
`tz_12h` (CROSS, TIME_FORMAT 0 at UTC+5:30), `utf8_off` (the fold) and
`accents` (the same reply in UTF-8), `refresh` (SQUARE 3 s after the first
fetch: "Refresh in 7 s", then the second request at the 10 s mark),
`net_fail`, `noip`, `nofolder` (the app folder is a file: no journal, no
counter, no prompt), `auto` (`auto.txt` 60: the shots at 30 and 45 s, the
exit at 60 s, `couch.txt`), `tv` (Lucas's area, `0 2 1920 1078`). Every text
fits at 1786 and at 1600 wide. With `PASSES=2`, the 41 snapshots repeat
byte for byte except, at times, an Info screen after a fetch
(`board_cut_info`, `board_stale301_info`, `tz_12h_info`): the Board thread
runs in host time while the preview's clock moves per frame, so a fetch's
duration reads 0.00 or 0.02 s.

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
reasons, missing fields, control bytes; the fold; `http_head` (the status,
Location, chunked, an incomplete head, not HTTP, bare LF); `url_split` (a
port, no path, https, bad ports, spaces, a long host); the clock (dates and
weekdays from 1969 to 2038, 12 and 24 hours, the UTC label, the TIMEZONE
range); the ages, the error words, `auto.txt`, the end line; and, in the
sanitizer build, 20,000 mutated replies with every field inside its buffer.
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
3. Write `board_url.txt` with a dead address (`http://192.168.0.250:8787/board`)
   and press START during a fetch: Couch must exit within 1 s. No test on
   this console has ended a process while a thread was in a socket call.

## Layout

`Makefile`, `build.sh`, `sfo.xml`, `pkgfiles/ICON0.PNG`, `art/make_icon.py`,
`source/` (`main.c`: the start, the loop, the unattended round, `couch.txt`;
`board.c`: the thread, the handoff, the page; `http.c`: the network start
and the HTTP client; `clock.c`: the settings, the offset, the clock;
`info.c`: the Info screen; `parse.c`: the pure functions, also built by the
host check), `tools/preview/`, `tools/hostcheck/`, `tools/syscalls.allow`,
and `worker/` (the Board's Worker, its own README).
