# Couch

A PS3 homebrew app for the living-room TV: a dashboard of the time, the
weather, the dollar rates and the news for Argentina, drawn at 1920 x 1080 by
[ps3gfx](https://github.com/lucasdaddiego/ps3gfx) on the RSX and the six SPUs.

- **Board:** the clock (the console's time zone), the next holiday, the
  weather now with a moving icon, the next 12 hours, the sun's arc and the
  moon, three days, the Blue rate with its 30-day curve, the official, MEP,
  CCL, crypto and card rates, the country risk, the monthly inflation and the
  fixed-term rate, and a news ticker. The sky behind the tiles follows the
  sun, the clouds and the rain.
- **News:** the latest headlines of the feeds; CROSS opens one in a reader
  with a QR code for the full article on a phone.
- **Info:** the Board's last fetch, the console's settings, and the effects'
  and the display's times.
- **The waiting screen:** after 10 minutes with no button, a large clock, the
  weather, the next hours, the next holiday and one headline at a time, over
  the sky by day and an aurora by night. It drifts slowly to protect the TV.

The data comes over plain HTTP (the console has no usable TLS) from a
Cloudflare Worker in `worker/`, which reads the public sources (SMN,
Open-Meteo, DolarApi, argentinadatos, argentina.gob.ar, RSS feeds) and
answers one small text reply. The default is `http://couch.daddiego.com.ar/board`;
`board_url.txt` in the app folder points it to another Worker.

## Install

A console with HEN or CFW. Download `Couch-<version>.pkg` from the
[releases](https://github.com/lucasdaddiego/couch/releases), install it with
Package Manager (or webMAN), and start Couch from the Game column. L1/R1 change
the page, SELECT opens Info, START exits.

## License

MIT (`LICENSE`). The pkg carries the fonts of ps3gfx under the SIL Open Font
License (`OFL.txt`) and Project Nayuki's QR Code generator (MIT).

## History

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
- a News page: up to 8 headlines of RSS feeds (since 2026-10-13: 3 of economy,
  3 of politics, 2 of the world, from Infobae through Google News, Ambito,
  Clarin and BBC; worker/README.md), with their source and age;
- three pages with L1 and R1 (the Board, the News, the Info screen) and
  their dots in a new header with the clock;
- the internet's state in the header: ok, slow, or no internet since HH:MM;
- the ambient mode: after 10 min with no button the screen dims, the
  picture moves 6 px and the page changes every minute, so the TV is safe
  when Couch stays on;
- a new look on every page.
0.2.0 has not run on the console yet, and its Worker is not deployed (the
rounds to come are below).

Couch opens on the Board. L1 and R1 move between the Board and the News
(since 0.3.1 the Info page is not in the cycle). SQUARE refreshes the Board,
SELECT opens the Info page, START or Quit Game exits. On the Info page, SQUARE twice
clears the freeze journal and CIRCLE goes back to the Board. In the ambient
mode, the first button only wakes the screen. Since 0.3.1 the News page is
a reader: UP, DOWN, LEFT and RIGHT move the selection over the headlines,
CROSS opens one, UP and DOWN scroll it (L2, R2, LEFT and RIGHT by a page),
CIRCLE goes back to the list; SQUARE there refreshes the Board and the list.

What 0.2.0 does not do: no Music page and no LP10 client (no socket to port
2018, no `lp10.txt`, no address editor, no fake LP10, no `a` actions in
`auto.txt`); no TLS, so an `https://` URL is refused; no Config screen (the
Info page shows, it edits nothing); no setting on the console for the
city, the rates, the bank or the feeds (they are the Worker's vars: a change
is a deploy, not a console round); no headline opens (titles only: 0.3.1
opens them, "The reader" below).

## The pages (0.2.0)

(Since stage 9 of 0.3.0 the Board page is the dashboard tiles, and since
stage 10 the header, the News page, the Info page and the ambient mode are
0.3.0's: "The Board page" and "The frame" below. The rest of this section
is 0.2.0's, kept for the words and the rules that stay.)

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
  titles. The foot: the sources and the age ("Clarin, Infobae, 5 min
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
  when it is not writable. Leaving it (CIRCLE; since 0.3.1 L1 and R1 do
  nothing there) opens the Board as
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
  mode's step (`ambient_tick`), the effects' step (`fx_tick`), the draw of
  the page on the TV. Each page draws through `page_begin` (the ambient
  shift, `begin_frame`, the background of the effects, the header)
  and `page_end` (the dim layer, `ui_flip`, the shift taken back). Only the
  main thread calls `journal_*` and writes files (since 0.3.1 but
  `couch.txt`'s queued lines, which the Board thread writes: "The reader").
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
  `SHUT_RDWR`) and joins the thread with no time limit. Then `fx_stop`
  (the SPU group down, below). Then `net_end`:
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
  calls at once. The thread never journals and (until 0.3.1, which gives it
  `couch.txt`'s queue and the reader's two requests) never wrote a file. At the
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

## The effects (0.3.0, stage 8 of the revamp)

`source/spu_fx_run.c` drives ps3gfx's effects program (`spu_fx`,
`<ps3gfx/spu_fx.h>`; design `~/.claude/plans/2026-10-13-couch-revamp-design.md`,
sections 3 and 6): one group of six SPU threads, modelled on PS3 Health's
`spu_run.c` and Cell Bench's `run.c`. Threads 0 to 4 make the pictures, a
band each: the clouds and the frosted glass (`FX_SKY`, 480 x 270 and
240 x 135) or the aurora (`FX_AURORA`, 640 x 360), into two textures of
each that take turns. Thread 5 makes the rain or the snow (`FX_RAIN`) in
every frame, into the frame's vertex area (`gfx_block`). Stage 8 drew
only the background behind 0.2.0's cards: the gradient, the clouds, the
rain (and the aurora under the ambient mode's dim layer). Stage 9's Board
page (below) and stage 10's pages call the same API: `fx_draw_gradient`,
`fx_draw_clouds` (the moon and the stars go between the two: `sky.c`), `fx_draw_rain`,
`fx_draw_aurora`, `fx_glass` (a tile's frosted glass, or a solid panel),
`fx_flash` (lightning), `fx_sc` (the scene's colours), `fx_reduced` (the
frame budget's step 5).

- **The start** (design 6.3), in `board_enter` before the network start:
  the six textures (each pitch checked against `FX_*_PITCH`), then every
  LV2 call journaled by the main thread, each behind `run_job("Starting the
  effects")`: `fx_init` (`sysSpuInitialize`, once per process), `fx_import`,
  `fx_group` (the create and the six threads; a failed thread destroys the
  group inside the entry), `fx_start`. `fx_first` opens before the start's
  done line. Then the six start reports (2 s), and the known-answer test
  in each (`kat == FX_KAT`) before the first job.
- **No prompt** (Lucas's choice): an `fx_*` entry that froze before, no
  writable folder, or a quit before the start: no SPU call at all. SQUARE
  twice on the Info page clears the journal, and the next opening of the
  Board starts the SPUs (as `net_start`).
- **The frame** (design 3.14): `fx_tick` at the top of the loop, after the
  last `ui_flip`, swaps a picture's textures once its five reports came
  (the check row read back from RSX memory on the first picture and then
  once a second: every 60th sky picture, every 30th aurora picture), then
  sends the next: the sky 60 pictures a second, one each frame, since the
  round of 2026-10-14 (stage 9's 30 held each picture two frames; the
  console measured `FX_SKY` at 1.97 ms on the slowest SPU, so 60 a second
  is about 12 % of each SPU), the aurora 30 (not measured on the console
  yet). The rain is
  inside the frame: `gfx_block`, the control block, the mailbox word, the
  wait for thread 5's report, no gfx call in between. When it is dry the
  rain runs only while `fx_first` is open: 60 frames of 40 empty splash
  quads, so the DMA into the vertex area is proven on every start.
- **`fx_first`** closes when the first sky picture was drawn with its check
  row equal to the report's sum, and 60 frames of rain ran; `fx_aurora`
  when the first aurora picture was drawn and checked (once per journal).
  Their `couch.txt` lines come a frame after the journal lines: each file
  write waits for the drive (`fs_write_file`'s fsync), and two in one frame
  may miss its vsync.
  The exit closes either one that is still open (ok, no freeze), after the
  start line of the join or the terminate.
- **RSX only** for the rest of the session (design 6.5): a frozen entry, a
  failed call, a rain report later than 50 ms, a picture later than 2 s, a
  check that differs, a refused job, the frame budget's last step. The
  group is stopped (`fx_terminate`, `fx_join`, `fx_exit_status`,
  `fx_destroy`, `fx_close`); the background is the gradient, four cloud
  sprites (a 256 x 128 texture the PPU makes once) and the PPU's rain (300
  drops: the same `fx_rain_job` with an output that writes the block in
  32-bit stores, never `memcpy`); the glass is a solid panel; no aurora.
  The Info page's Effects row says why.
- **The frame budget** (design 3.13), each 2 s: more than 3 display periods
  over 20 ms, or a picture whose reports came after the next was due, is a
  step: from 60 sky pictures a second to 30, 20, 15 (the aurora stays at 30
  until the step to 20), the rain at half, no rays or twinkle or hero motion
  (`fx_reduced`), RSX only. 60 s with no slow period is a step back. The
  design's step 4 (the glass every 4th picture)
  is left out: `FX_SKY` writes the glass with every picture, so it would
  save no SPU time.
- **The exit** (design 6.4, `leave`): `board_stop`, then `fx_stop` (the
  exit flag in the six blocks, a mailbox word to each thread whose last
  report came, up to 1 s for the six exit reports, then `fx_join` and
  `fx_exit_status`; else `fx_terminate` (a failed one whose job counts still
  move: stuck, no join, no destroy), then `fx_join`; then `fx_destroy` and
  `fx_close`), then `net_end`, then `ui_end`. `couch.txt` gets each step,
  the exit statuses (0x700 to 0x705), and the busy times of the jobs.
- **The scene** (`fx_tick`, each minute and at each reply): the sun from
  `geo` (`sun_pos`), the gradient from `sky_colors`, the cover, the wind
  and the rain from `wx_fx` (`wx_precip`: 300 to 1,500 drops by the code
  and the hour's mm, 300 flakes), the cloud colours from the sky's (by
  night a dim blue-grey, 141a28 and 080c16, just over the night sky: stage
  8's 2a3550 and 121a2c made an overcast night twice as bright as a clear
  one), the sky's light for the glass tint (`light`, 0 from -6 deg down to 1
  from 12 deg up). A reply with no `geo` (a Worker before 0.3.0) takes the
  sun at 30 deg or -20 deg by `wx_now`'s day or night.
- **The clouds' motion** (since the round of 2026-10-14, where stage 9's
  sky read as still): they drift where the wind blows to, as seen facing
  the equator (north in the south: east is right, and away from the viewer
  is down the screen, toward the horizon, at 0.4 of the speed; stage 9
  moved a north wind's clouds down in the south). The speed grows with the
  root of the wind: 6.4 picture pixels a second at 10 km/h (the screen's
  width, 480 picture pixels, in 75 s: a light wind's calm crossing), 9.1
  at 20 km/h (53 s), at least 2.5 (calm or no wind: from the west, the width
  in about 3 min) and at most 14 (34 s). The PPU adds the wind times the
  frame's time to the offset each frame, so a wind change makes no jump.
  The offset goes into the noise as a float, so the picture moves by
  fractions of its pixels (no 1-pixel steps in the 4x upscale), and at 60
  pictures a second each frame shows a new place. The clouds also change
  shape: the job's time, which moves the kernel's warp, runs at 3 times the
  clock (`EVOLVE`, wrapped at 64,000 s, where the warp's offsets repeat), so
  a warp cell passes in about 16 s; the SPU program is unchanged. Lightning
  lights the clouds for 120 ms, fading (`fx_flash`; stage 9 lit one
  picture).

## The Board page (0.3.0, stage 9 of the revamp)

`source/board_page.c` draws the Board in the design's dashboard tiles
(section 1.4; the mockups `board_day_rain.png`, `board_night_clear.png` and
`board_day_narrow.png`), over the live sky. The header, the News page and
the Info page are 0.2.0's until stage 10.

- **The layout**, from `SW` and `SH` each frame: wide (`SH` 980 and up: the
  TV area and the default area, every row height times the content's height
  over 800) or narrow (the narrow area). The content from y 152 (narrow 132)
  to `SH` - 124 (narrow 112); with no headlines the ticker goes and the
  content grows to its place. Left, 0.585 of the width: the weather hero
  (440 px at the TV area, 360 narrow), the sky tile (160, 128), the three
  days (the rest). Right: Blue's tile (236, 196) and a grid of 2 x 4 small
  tiles. Under them the ticker (52 px, 48 narrow) and the bottom bar, which
  replaces 0.2.0's status, footer and note lines on the Board only: the
  status and the schedule on the left ("Board updated at 09:00, next update
  in 4 min"; `board_url.txt`'s note after them in YELLOW), the buttons on
  the right. The URL of `board_url.txt` shows on the Info page only.
- **The tiles**: frosted glass (`fx_glass`: the SPU's blurred sky under the
  tile's rect, then `GLASS_TINT` at 0x73 by night to 0x9c by day from the
  scene's `light`), a 1 px edge and a bottom line; in RSX-only mode a solid
  panel. The rounded corners get a 1 px fringe in the tint (`aa_corners`).
  Colour tokens in `couch.h` (design 1.10).
- **The hero**: the weather's icon of the L set in layers that move (the sun
  turns once a minute; the cloud drifts 8 px on 6 s, a grey one behind on 9
  s, both by fractions of a pixel since the L set binds bilinear; 10
  anti-aliased streaks under a rain cloud; the lightning layer flashes with a
  bolt and every 6.5 s on a storm; 6 flakes fall on snow; 3 bars slide on
  fog; the moon with two twinkling stars at night), the temperature in D_XL
  (D_L narrow; F_LARGE when `digits_init` failed), the words, "Feels like
  17 deg, humidity 61%" (two lines narrow, or when the row is too short), the
  min/max bar (today's min and max, 8 steps from TEMP_COLD to TEMP_HOT, the
  now marker), the wind dial (r 76, narrow 58 with N only; the arrow where
  the wind goes with a 2 deg wobble, the speed, "SE, gusts 34"), the rain
  line, and the 12 h curve (`wx_hours`: the line, the fill under it, a BLUE
  column per hour's chance, the hours, every second temperature, every
  third narrow).
- **The sky tile**: the sun arc from `wx_sun` (solid SUN to the sun's place,
  dashed DIM after, the sun with its glow; by night all dashed with a small
  moon at the night's fraction), "Sunset in 4 h 29 min" or "Sunrise in ...";
  the moon's sphere at the real phase (`sky.c`, 160 x 160, 16 rows a frame,
  a new picture each 0.25 day of age, lit on the left of a waxing moon south
  of the equator), its phase, "9% lit" and "New moon 11 Oct". The moon needs
  no key: Couch computes it from the clock.
- **The three days**: the day, the M icon from `days_fx` (bobbing 2 px on 4 s,
  a third of a period apart), max and min, the rain chip.
- **Blue's tile**: the sell in D_M with ",00" in F_LARGE, Buy and the time,
  the change chip from `usd_chg`, the 30 days' range and sparkline from
  `spark`. **The grid**: the other rates in the Worker's order, then the
  money extras, 8 places (more: a `couch.txt` line once); a small tile has
  its label, its chip, the value with its decimals smaller and a foot ("buy
  1.485,00, Fri 15:00", "12 Oct", "TNA, 14:47" for "Plazo fijo BNA (TNA)");
  narrow: the label's first word and the time on the label row, no foot. A
  stale value's foot in YELLOW; a none value "-" with its reason.
- **The change chips** (Lucas: amber up, teal down, a grey dash for flat):
  the dollars show the change in %, riesgo pais in points.
- **The changed tiles**: at each good reply the Board compares each rate's
  sell and each extra's value with the reply before (`board_prev`, by
  label). A changed value counts from the old value in 400 ms (`num_mix`,
  ease-out cubic) and its tile gets a halo (`glow9` with `sky.c`'s halo
  texture, UP or DOWN): up in 200 ms, two pulses of 500 ms, then a fade in
  400 ms to a lower glow that stays until the next reply, so "changed at the
  last refresh" still shows minutes later (the design faded it out by 6 s).
  Stage 9's 800 ms count and 3 s of pulses read as slow on the TV.
- **The ticker**: a glass band over the screen's whole width, the headlines
  moving 2 px each display period (119.9 px a second at 59.94 Hz; the
  count of steps comes from the clock, so a late frame moves it 4 px and
  the speed stays) apart by a dot, the pill of the headline at its right
  end (Clarin red, Infobae orange, others blue-grey, its ends anti-aliased)
  over a dark block from past the screen's left edge, so the text goes
  under the pill. Stage 9's 90 px a second moved 1 and 2 px in turn.
- **The lines** (since the round of 2026-10-14, "some lines are rough"):
  ps3gfx's anti-aliased helpers (`fx.h`'s `aa_*`: every edge fades over 1
  px, no pixel covered twice) draw the 12 h curve and its first point, the
  sparkline and its last point, the sun arc (solid to the sun's exact
  place, dashed after), the sun and the small moon on it, the wind dial's
  ring, ticks (none at N, E, S and W, as before under the ring), shaft and
  arrow head, the min/max bar's round ends and its marker, the hero's rain
  streaks and night stars, the sun's disc in the sky, and the header's page
  dots and internet dot. The rain columns are whole-pixel rects: exact
  already. The moon's sphere was smooth already (a soft terminator over 3
  texels, the rim over one).
- **The "-" forms** (a Worker before 0.3.0, or before the first reply): no
  weather code, no hero icon (the temperature takes its place) and no day
  icons; no `wx_hours`, the curve's axis with "12 h: -"; no `wx_fx`, the
  dial's speed from `wx_now`'s wind and no arrow; no `spark`, "30 d: -" and a
  dashed line; no chips; no `wx_sun`, a dashed arc and "-" times; no reply,
  every value "-" in DIM. A stale or none weather shows its reason at the
  rain line's right in YELLOW, a stale dollar feed at Blue's range.
- **The sky's objects** (`sky.c`, the same with or without the SPUs): the
  stars (200, by night under a thin cover, each with its own brightness each
  frame, twinkling 35 % at 0.2 to 0.5 Hz: stage 9's 7 levels made a star
  jump a level at a time), the moon's disc while it is up (`moon_pos`,
  under the clouds), the sun's glow and its disc (the disc only through a
  thin cover) and 9 rays turning 1 deg a second (a cover under 60 %),
  lightning on a storm code (95 to 99: a bolt at 6 s after the start and
  then every 6 to 20 s, 24 points by midpoint displacement drawn 3 times
  over the clouds, on for 42 ms, off to 75 ms, on to 108 ms; a white flash
  for 25 ms: the design's 3, 2 and 2 frames and its 2 frames at 60 Hz, as
  times since 2026-10-14, each bound half a frame from the frames;
  `fx_flash` lights the clouds for 120 ms), and the scrims behind the
  header and the bottom bar (strong to canvas y 120 and from `SH` - 130, at
  any visible area: the design's 0x70 and 0x80 from the screen's edges were
  too light on a clear day). Every motion reads `sky_time` (the system
  clock since the start), never a reply's landing time or a count of
  frames, and `fx_reduced` stops the rays, the twinkle and the hero's
  motion.
- **The sky's colours** (`sky_colors`, `parse.c`): the cover's grey is the
  design's from 6 deg up; below, it is scaled down by the light, to the
  clear sky's luminance at most, so an overcast night is not brighter than
  a clear one (the design's rule gave 434e61 against 050a18).

## The frame (0.3.0, stage 10 of the revamp)

Stage 10 of the design (`~/.claude/plans/2026-10-13-couch-revamp-design.md`)
and Lucas's notes of 2026-10-14 from the pre2 round ("the news section can
have more love. the background is too blurry, and the text marquee is fast.
and the icons moves in installments, and not smooth"; "maybe they can be a
bit bigger and have more range of movement"; "an slower marquee"; and of
the round before, "it had a nice black delay. can we add a loading
screen").

- **The loading screen** (`start.c`): the first frame comes right after
  `ui_init`, before `text_utf8`, `digits_init`, `icons_init`,
  `fs_selftest` and the SPUs: a sofa, "Couch" and the version, "Starting
  ..." and the steps under it (the picture, the accented letters, the
  display digits, the icons, the app folder, the effects, the network),
  each done (GREEN), running (a spinner) or to come (DIM). Only the kit's
  ASCII text and shapes: nothing else is loaded yet. The effects' and the
  network's starts run their calls in a job thread while this screen draws
  a counter (`start_job`: the kit's `run_job` in this look, the same
  thread priority and stack, the same freeze promise under it); a later
  start of either (after SQUARE twice on the Info page) shows the same
  screen with only its step.
- **The start's times** (`couch.txt`): each step is timed from main's first
  statement. After the start line: `start: ui_init 0.31 s, text_utf8 ...,
  fs_selftest ...; the loading screen's first frame on the TV at ... s from
  main`; at the first fetch's reply (or at the exit): `start: fx_start ...,
  net_start ...; from main: the first Board frame at ... s, the first
  fetch's reply at ... s`. The time before main (the ELF's load) is not
  measurable from Couch.
- **boot.txt** (`boot.c`, PS3 Health's `source/boot.c` with Couch's
  folder): Couch hung the whole console before its first file write once
  (2026-10-14 16:40). Each start writes `main` as main's first statement,
  then a mark after `ui_init`, `text_utf8`, `digits_init`, `icons_init`,
  `fs_selftest`, `fx_start`, `net_start`, `first_frame` (the first Board
  frame) and `first_fetch`; the exit writes `leave`, `fx_stop`, `net_end`,
  `ui_end` before each step and `left` at the end. Each line is the name and
  `sysGetSystemTime()` in microseconds; sysLv2Fs calls only, each mark
  synced; past 32 KB the `main` mark moves the file to `boot.old.txt`. ps3gfx's
  `ui.c` is unchanged (PS3 Health and Cell Bench stay byte-identical).
- **The start animation** (design 3.9), from the loop's first frame, 1.2 s,
  any button ends it: the loading screen's colours fade from the sky in 0.5
  s, the header comes down 24 px, the left column, the right column and the
  bottom rise 30 px into place a little apart (the design's tiles one by
  one, and their fade, are simplified to the columns and the screen's
  fade).
- **The header** (design 1.3): the clock, HH:MM in the display digits (D_L),
  whole, as the loading screen's (0.3.0's flip cards, with their middle line
  and the seconds, are gone since 0.3.1: Lucas, 2026-10-15, "looks cut in
  half"); AM/PM in F_MED GREY on a 12-hour console; the date (without the weekday when the row is too
  short) and the clock's source line. Right: the page's name and its dots;
  under them the city, the wifi icon (wifi-high, or wifi-slash with no
  internet) and the internet's words with their dot. No display digits
  (`digits_init` failed): 0.2.0's clock.
- **The slides** (design 3.8): L1, R1, SELECT and CIRCLE change the page
  with a slide of 280 ms (ease-out cubic): the old page goes out and the new
  one comes in, the sky, the header and the bottom bar stay. The move is the
  viewport's (`shift_push`, whole pixels), so the glass of a tile follows
  it; the Board's ticker band moves with its page. A button during a slide
  ends it at once (and does its own action).
- **The bottom bar** on every page: the status on the left (the Board and the
  News: "Board updated at 09:00, next update in 4 min", or the failure;
  the Info page: its messages), the buttons on the right.
- **The News page** ("more love"): the newest headline large in a tile of
  the whole width (its source's pill, its category, "Latest", the age with
  the clock icon, the title in two lines of F_TITLE where they fit, else
  F_MED; a 3 px top edge in the source's colour; a faint newspaper mark),
  then a grid of 2 x 4 glass cards: the next 7 headlines (the pill, the
  category, the age, the title in two lines of F_MED where they fit, else
  F_BODY) and a summary (the count, the sources, the age). The pills:
  Clarin red, Infobae orange, Ambito blue, BBC grey. The category comes from
  the source (`news_category` in `parse.c`): Ambito "Economy" (its one
  feed), BBC "World"; Clarin (politics and world) and Infobae (a search)
  have none, since the Worker sends no category and this stage adds no key.
  8 headlines fit at 1080p and at the narrow area. No headlines: one tile
  with the reason; before the first reply: the skeleton.
- **The Info page** (design 1.6): three glass tiles. "Board" and "Console"
  side by side (0.2.0's rows; a value wraps to 3 lines, F_SMALL at the
  narrow area), "Effects" under them: the SPUs' state or why RSX only, the
  pictures a second and the frame budget's step, the busy time of each job
  (the slowest SPU's mean), the rain, the display's rate (`ui_stats`).
- **The ambient mode** (design 3.12, Lucas: "the dim aurora (20 % max) with
  the drift"): with the SPUs, the aurora over the screen (its mean
  brightness at most 20 %: the kernel's cap), the rain at half over it, and
  a block: the clock in D_XL GREY, the date, and a line that changes each
  minute (the temperature and the words, Blue's price, the newest headline,
  skipping a line with no data). Each minute the block glides over 6 s to
  the next of the 9 places of the 3 x 3 grid (300 px apart across, 150 down:
  `ambient_place`), so no text stays in one place. This replaces 0.2.0's
  dim layer and stage 8's aurora under it. RSX only: 0.2.0's mode (the dim
  layer, the 6 px moves, the page each minute).
- **The skeleton** (design 3.9): before the first reply each tile shows grey
  bars in place of its values, with a light band that crosses the screen
  every 1.6 s (a gradient pair of quads: no hard edge, so it moves by
  fractions of a pixel).
- **The ticker**, slower and smooth: 55 px a second (`TICKER_PX_S` in
  `board_page.c`, the one number to tune after the console's round; stage
  9b moved 2 px a frame, 119.9 px a second). ps3gfx draws text pixel exact
  (nearest filtering at 1080p, each glyph on a whole pixel), so a speed that
  is not a whole number of pixels a frame judders at 59.94 Hz. `marquee.c`
  draws each headline once into a texture of its own, from the same
  atlases (`fonts.bin`, and `latin.bin` with UTF-8 on) and with ui.c's own
  glyph placement, and the ticker draws that texture bilinear at the exact
  fractional x of the clock (`ticker_offset`). At a whole pixel it is
  byte-identical to `text()` (checked once on 2026-10-14 with a temporary
  preview build that drew the four first headlines of the `accents` reply
  both ways, UTF-8 accents included; the hook is not kept); between two
  pixels each column mixes
  its two neighbours. Two textures of 4096 x 304 (A4R4G4B4, 2.5 MB each)
  take turns: when the headlines change, one headline a frame goes into
  the texture no frame shows (after `ui_flip`), then the swap; meanwhile
  the ticker draws them with `text()`.
- **The icons that move**, smooth (the console's video of pre2: the day
  icon held still 17 to 53 frames, then jumped a pixel): every moving icon
  goes through `icon_rot`, which binds every set bilinear since this stage
  (ps3gfx, below), at fractional places. Larger and with a wider, calm
  range (Lucas): the day icons at 1.25 times (80 px; 1.1 narrow), a bob of
  4 px on 5 s, a sway of 3 px on 7 s and a tilt of 2.5 deg on 6 s, each
  card a third of a period apart; the hero's box 184 px (140 narrow; stage
  9: 168 and 132), its cloud drifting 14 px on 8 s and the one behind 10 px
  on 11 s, the sun turning once a minute and breathing 3 % on 5 s, the rain
  falling 70 px, the snow 70 px with a sway, the fog's bars sliding 18 px
  as anti-aliased lines, the moon swaying 4 deg on 7 s. The sun's rays in
  the sky are soft wedges (their middle line 0x30, their sides 0): stage 9's
  hard-edged triangles crawled a pixel at a time as they turned.
- **The clouds, crisper** (ps3gfx's `spu_fx` kernel, below): five octaves
  of noise in place of three, the fine ones heavier, a coverage band half
  as wide, and the finest octaves shading the inside of a cloud. The glass
  behind the tiles stays blurred.

The ps3gfx changes of this stage are in ps3gfx's working tree (not
committed): `source/icons.c` (`icon_rot` always bilinear: a second
`gfx_tex` of each set's texels, no new RSX memory), `source/spu_fx_kern.h`
(the clouds: `FX_CLOUD_W1` to `W5`, `FX_CLOUD_BAND` 0.085, `FX_CLOUD_THICK`,
`FX_CLOUD_TEX`, the quantile table `FX_CLOUD_MID` re-measured), `data/spu_fx.spu`
rebuilt (26,136 B, `_end` 0x22890), `test/test_spu_fx.c` (the new golden
hashes, `--quantiles`), kitcheck (`icon_rot` at a fractional centre), the
README. Neither file is linked by PS3 Health or Cell Bench: their ELFs are
byte-identical against ps3gfx 7ad9547 (and their link maps hold no `icons.o`
or `spu_fx`). The SPU cost: about 84 cycles a cloud pixel in place of 72
(spu-objdump's inner loop), so about 2.2 ms a picture on the slowest SPU in
place of the console's 1.97 ms: 13 % of each SPU at 60 pictures a second.

## The reader (0.3.1, stage 11 of the revamp)

Lucas, 2026-10-14: "add more news and also, select a new and enter it? and
maybe scroll down for more". The Worker's side is live (worker/README.md,
"The news reader": `GET /news`, up to 30 items in `couch-news 1`, at most
32 KB; `GET /article?id=<id>`, the paragraphs in `couch-article 1`, at most
16 KB). Couch's side, `source/reader.c` and `source/stext.c`:

- The News page is the list of the last good `/news` reply: the featured
  card for the newest item and rows of two glass cards (stage 10's look and
  sizes: the first screen is stage 10's page), each with its source's pill,
  its category (the Worker's word), its age and its title in two lines
  ("..." when it goes on). Until the first list, the Board's 8 headlines
  fill the page; they have no id, so CROSS on one says the list is not in
  yet. A list that does not come (an old Worker's 404, no answer) leaves
  them, with the reason in the bottom bar.
- The list comes when the News page opens (unless one was asked in the last
  60 s) and every 5 min while the page shows (60 s after a failure), never
  while an article is open or in the ambient mode; SQUARE asks again (at most
  once in 10 s). A new list keeps the selection on its headline (by id).
- The selection: UP and DOWN move by a row, LEFT and RIGHT across the two
  columns (from the featured card DOWN goes to the first card; from a right
  card DOWN to a row with one card goes to it). The selected card has a
  lighter glass and a white ring that brightens in 150 ms. The list scrolls
  to keep it in view with 44 px of the next row showing (`scroll_keep`), by
  an ease (`scroll_follow`: the rest falls by e every 75 ms, so a new press
  mid-way stays smooth), by fractions of a pixel; a held direction repeats
  after 350 ms, then every 110 ms. A scroll bar on the right shows the part
  in view.
- CROSS opens the selected item: a glass panel over the content area, the
  title in `F_TITLE`, the source's pill, the category, the date and the
  age, then the paragraphs in `F_MED` (32 px, a line step of 1.34 lines, a
  column of at most 1,180 px: about 75 characters), word-wrapped with
  ps3gfx's `text_w` (`wrap_lines`, ps3gfx's own break rules). While it
  loads: the list's title and the skeleton bars. UP and DOWN scroll three
  lines by the same ease, a held one glides at 900 px a second, L2, R2, LEFT
  and RIGHT scroll a page. On the right, the QR code of the link (PS3
  Health's encoder, `source/qrcodegen.c`, a copy: the smallest version for
  ECC low, with a higher ECC when it fits in it, as Health's; version 25 at
  most; made by the Board thread), "Scan it with a phone to open the whole
  article"; the end of the text says so. `state=summary`: one YELLOW line
  under the meta ("Only the feed's summary: the page could not be read
  (reason)"); `state=cut` (or a text over Couch's 12 KB): "The article is cut
  at 12 KB: the QR code opens the whole text". No answer, a bad reply or
  `state=none`: the title and one line ("The article did not load: no
  answer in 10 s."), and "CIRCLE goes back to the list". CIRCLE goes back to
  the list, its selection and scroll as they were; a reply that comes after
  CIRCLE (or for another item) is dropped and logged.
- The ambient mode waits 20 min (`AMBIENT_READ_SEC`) while an article is
  open on the News page, 10 min otherwise (Lucas reads, and a page of text
  is still for a while); any press still only wakes the screen.
- The Board's 8 headlines stay as they were for the ticker.

The scroll by fractions of a pixel (`stext.h`): ps3gfx draws text pixel
exact (nearest filtering at 1080p, each glyph on a whole pixel), so an eased
scroll would end in whole-pixel steps. Couch keeps its own copy of four
fonts' atlases (`F_SMALL`, `F_BODY`, `F_MED`, `F_TITLE`, ASCII and Latin, as
`marquee.c` reads them), each glyph in a cell with a clear texel around it,
in one bilinear texture per font: 3,378 KB of RSX memory, built after
`ui_flip` 4 rows of cells a frame (about 15 frames) from the first News
page. A glyph lands at any fractional y; at a whole pixel its texels land
1:1: the list at rest and the back to the list are byte-identical to
`text()`'s (checked in the preview with the atlases off; the article
differs only in its faded last line). The cards, the pills and the rings
are drawn at the same fractional y; every rest position is a whole pixel,
so the text at rest is crisp. ps3gfx has no scissor, so the scrolling part
is drawn in a band: each glyph quad and each glass polygon is cut at the
band's edges and fades over up to 36 px there (per-vertex alpha; a glass
tile that crosses is a clipped rounded polygon in three slabs, so the fade
is exact). The list's fades grow with how far a card crosses the edge (none
on the first screen); a pill, a ring or an icon goes when it crosses. Until
the atlases are built (or with no RSX memory for them: a `couch.txt` line),
the text is `text()` at the nearest pixel, a line only when it is in the
band.

The requests (`board.c`): the Board thread makes all three, one at a time;
an article first (Lucas waits for it), then the Board when it is due, then
the list. The body buffer is 32 KB for all; the end line stops the read as
for the Board (the scan reads only the new bytes). The thread parses `/news`
and `/article` itself (`news_parse`, `article_parse`: pure, in `parse.c`)
and makes the QR code, so the frame only copies the reply (`reader_take`).
A news or article reply sets the header's internet state as a Board reply
does, except that an article's time is the Worker's page fetch (up to its
8 s): an answered article only ends "No internet", it never says slow.

couch.txt off the frame (the 0.3.0 round's slowest frame, 16.2 ms, was a
Board reply and a `couch_log` line, whose open, write, fsync and close ran
on the main thread): since 0.3.1 `couch_log` puts the line in a 16 KB queue
while the Board thread runs, and the thread writes the queue in one append
(the same `fs_write_file`, fsync'd) between its requests, at most every 50
ms, or after the request in flight (up to its 10 s timeouts). Written at
once as before, after the queued lines (couch.txt keeps the order of the
calls): every line before the thread starts (the start's lines), after the
exit begins (`leave()` clears `log_async`: the marks of a hang before the
join and `net_end` are on the disk before their call), with no thread (no
network), and when the queue is full. The journal is not queued. A freeze
loses the queued lines (at most the last 50 ms, or the request in
flight's). The slowest frames' "a log line" now means a queued line.

The pad: `reader_input` runs on the News page after L1, R1 and the Info
page's keys; the rest of the loop is 0.3.0's (L1 and R1 slide the pages,
also from an open article, which stays open; SELECT; START). The ambient
mode's wake clears the press first, as before.

## The waiting screen and the holidays (0.3.2)

Lucas, 2026-10-15: "center the clock on the waiting screen, add weather and
more news. but keep it aesthetic. and if you can make one day and one night
theme, or better, a theme that adapts to the sun".

- **Day and night** (`page.c`, `ambient_day`): the ambient mode shows the
  Board's sky (the gradient, the sun or the stars, the clouds, the rain)
  under a dim layer while the sun is up, and the aurora at night. The sun's
  elevation (`fx_sc.elev`, from geo and the clock) switches it: day above
  -2 degrees, night below -4, so a dusk does not flicker. The SPU job
  follows (`fx_tick` asks for the sky by day). The Board itself already
  follows the sun since 0.3.0: the sky's colours and the glass tint.
- **The block** (redone the same night after Lucas: "I see it
  disproportionate. make it more gorgeous, like apple"), AMB_W 1240 px in
  the middle: the date small over the clock (D_XL); under it two cards, the
  weather (the hero's moving icon at 120 px, the temperature in D_M, the
  words in F_LARGE, the high, the low and the rain's chance) and the next
  holiday ("Next holiday", "in 4 days", "Mon 12 Oct" and the name; else the
  sunrise and the sunset); then one card with a headline in two lines of
  F_MED, its source's pill and its age, the next every 10 s with a 0.6 s
  fade, and a dot per headline. The cards are light translucent rounded
  panels (28 px corners, a 1 px edge): the Board's glass needs the sky's
  blur, which the aurora does not make. WHITE by day; by night a little
  under it. The block drifts at most 40 px sideways and 24 px up and down,
  in whole pixels, on periods of 7 and 5 min (`ambient_drift`): the TV's
  burn-in guard, instead of 0.3.0's nine places.
- **The next hours** (2026-10-16, `board_ambient_hours`): a card between
  the two cards and the headline, one column per hour of `wx_hours` (12 from
  the next whole hour): the hour ("Now" once the reply is an hour old, and
  the hours that have passed left out), the M icon (the moon between the
  sunset and the sunrise), the rain's chance in blue from 20 %, the
  temperature. A reply whose hours have all passed shows no card. The block
  must fit the visible area with the drift: when it does not (the narrow
  area, 900 px high), the clock takes D_L; when that is not enough either,
  the card is left out and the clock stays D_XL.
- **The holidays**: the Worker's `holiday=` line (worker/README.md; the
  official list of argentina.gob.ar). The header's second line names the
  next one ("Next holiday: Monday 9 November, Visita de Su Santidad el Papa
  León XIV (in 25 days)", "Tomorrow is a holiday: ...", "Today is a holiday:
  ...") while the clock is the console's own; otherwise the clock's source,
  as before (the Info page always shows it). A reply whose holiday passed
  shows none.
- **The whole app in the same spirit** (Lucas: "also the full app should
  look like it"): tiles with 26 px corners (20 before), gaps of 24 px
  between the columns and 16 px between rows (16 and 12); a change chip only
  for a move (no grey minus for "no change"); the small money tiles in two
  rows, the label with the time or the date on its right (shorter when the
  label needs the room: "Aug", "14:47"), then the value; a rate's buy price
  is the Blue card's only.
- Preview: `ambient` (by day, with a holiday), `ambient_night` (the aurora,
  then the next three headlines), `holiday` (the header). PASSES=2 now
  counts a pixel only when it moves by more than 8 levels (the sky's blur
  varies by 1 or 2 with the host's timing) and names the box.

## Files in /dev_hdd0/tmp/couch

| File | Written by | Content |
|---|---|---|
| `boot.txt`, `boot.old.txt` | Couch (`boot.c`), since 0.3.0 | a line per step of the start and of the exit (the name and the console's microseconds), synced at once; past 32 KB the next start moves it to `boot.old.txt` |
| `starts.txt`, `journal.txt` | ps3gfx | `start` per start; the journal's `start`, `done`, `froze` and `skip` lines (`net_start`, `tz_params`; since 0.3.0 the effects' `fx_init`, `fx_import`, `fx_group`, `fx_start`, `fx_first`, `fx_aurora`, `fx_terminate`, `fx_join`, `fx_exit_status`, `fx_destroy`, `fx_close`: 13 names) |
| `safe_area.txt` | ps3gfx | the visible area, measured at the first start |
| `gfx_init.txt` | ps3gfx, only when the picture cannot start | the failed step |
| `board_url.txt` | FTP only, for test rounds | one `http://` line, e.g. `http://192.168.0.3:8787/board`. Absent: `http://couch.daddiego.com.ar/board`. An `https://` URL or a bad line: the default, and the note line says why |
| `auto.txt` | FTP, for unattended rounds; Couch writes `0` at once | the seconds of a round, 60 to 600 |
| `couch.txt` | Couch, appended | one block per start: the version and the ps3gfx stamp, the UTC time, `text_utf8`'s rc, the URL, the network start (the rcs of `netInitialize`, the netctl module load and `netCtlInit`) and the address, a quit before the Board opened, the three settings with their rcs, the first 50 fetches (time, duration, the connect's time or "no connection", HTTP status, bytes, the result, the Worker's `now` and `offset`, the console's clock minus the Worker's, the headlines), the ambient mode's starts and ends, the shots, the exit (a line before and after the Board thread's join, with its time; a line before and after `net_end`'s calls), `ui_stats` (since 2026-10-14 with the most vertices and draws of a frame) and the three slowest frames of the loop (the main thread's busy time, the frame's number and time, and what it did besides its draw: a reply, a log line, a journal line, a check row, the moon's upload, a texture made, a page change, a shot; each journal line waits for the drive's fsync, and since 0.3.1 a log line in the loop is queued: the Board thread writes it), and the exit's time. Since 0.3.1 also the reader's first 60 replies (`news N at T s: 0.31 s (connect 0.05 s), HTTP 200, 10495 bytes: ok; 30 items, state ok`; `article <id> at T s: ...: ok; state ok, 14 paragraphs, link 127 bytes, QR ok`, with "(closed: dropped)" after the id for a reply after CIRCLE), the atlases' line (`stext: the reader's atlases, 3378 KB of RSX memory`) and the ambient mode's 20 min ("an article open"). Emptied at start past 64 KB |
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
allowed; the EBOOT matches the ELF; no compiler warning). The allowlist had
39 numbers, every `sc` resolved: PS3
Health's runtime set without its drive, temperature and fan calls (libnet,
libnetctl, `sysModuleLoad` and `sysUtilGetSystemParamInt` are SPRX
imports; the exit's `netShutdown`, `netDeinitialize`, `netCtlTerm` and
`sysModuleUnload` add no number). The effects (stage 8, on ps3gfx 4a9f4d4)
add the nine SPU numbers of Health and Cell Bench: 165, 169, 170, 171, 172,
173, 177, 178 and 190 (the image's import and close are SPRX imports): 48
numbers at 93 call sites, every `sc` resolved. `build.sh` also copies
ps3gfx's `PHOSPHOR.txt` (the icons' licence) beside `OFL.txt`. Out: `couch.gnpdrm.pkg` (365 KB
since stage 9, which links the display digits and the icons) and the unstripped `couch.elf`. Stage 9
adds no syscall: still 48 numbers at 93 call sites.

Since the round of 2026-10-14 Couch calls ps3gfx's anti-aliased helpers
(`aa_line`, `aa_lines`, `aa_strip`, `aa_ring`, `aa_disc`, `aa_poly`,
`aa_corners` in `fx.c`), which are in ps3gfx's working tree, not yet
committed. Until the commit and the pin move, the build is
`PS3GFX_DEV=1` against that tree installed in `~/ps3dev` (stamp
`ae528b3...-dirty` in `couch.txt`), and the preview's build is
`PS3GFX=~/code/ps3gfx zsh tools/preview/build.zsh`. The pkg is 372 KB
(371,936 B); still 48 syscall numbers at 93 call sites, no compiler
warning. Health's and Cell Bench's ELFs and link maps are byte-identical
on that tree (no `fx.o` in either map).

Stage 10 (0.3.0, 2026-10-14) builds the same way against ps3gfx's working
tree with its stage 10 changes (`icons.c`, the `spu_fx` kernel and program:
"The frame" above), installed in `~/ps3dev` (stamp `02af1b5...-dirty`;
the library's code is 7ad9547's plus those files). The pkg is 389 KB
(388,896 B); 48 syscall numbers at 108 call sites (`boot.c`'s sysLv2Fs
calls and `start.c`'s job thread add call sites of numbers already in the
list, no new number), every `sc` resolved, the EBOOT matches the ELF, no
compiler warning.

Since 2026-10-15 Couch builds against ps3gfx 3ea00e1: the journal reads its
whole history, and a journal it cannot write makes the Info page's Journal
row YELLOW ("not written: a freeze may go unrecorded"); the calls still run
(the `nofolder` scenario). The pkg is 424,160 B.

Stage 11 (0.3.1, 2026-10-14) builds against the pinned ps3gfx a311375 (no
ps3gfx change). The pkg is 425 KB (425,376 B); 48 syscall numbers at 110
call sites (couch_log's queue and its wait add call sites of numbers in the
list), every `sc` resolved, the EBOOT matches the ELF, no compiler warning.
The first build had one `sc` the audit could not read: gcc turned the wait
loop of `log_now` so that `sysUsleep`'s `li r11,141` came after the `sc`
(0.1.0's first ELF had the same); the sleep is now `nap_ms`, out of line.
`source/qrcodegen.c` and `.h` are PS3 Health's copy of Project Nayuki's QR
encoder (MIT, the notice in the file's head; Health's NOTICE lists it).

`art/make_icon.py <Inter variable TTF>` draws `pkgfiles/ICON0.PNG` (a sofa,
"Couch" and "Board", in PS3 Health's icon style); the font is
`fonts/InterVariable.ttf` in the ps3sdk archive.

## Preview on the Mac

```zsh
zsh tools/preview/build.zsh                 # ps3gfx's preview: build/pv, and build/pv_utf8_off (-DCOUCH_UTF8_OFF)
zsh tools/preview/run.zsh                   # every scenario at the default visible area, one process each, NJOBS at a time (default:
                                            # the cores), the long ambient ones first; the wall time is reader_ambient's (76,000
                                            # frames: the SPU twin makes a sky picture every frame since 2026-10-14)
zsh tools/preview/run.zsh pages             # one scenario: the fast path while a change is made
AREA=narrow zsh tools/preview/run.zsh       # the same at the narrowest area, 1600 x 900 (snapshots *_narrow; not tv)
PASSES=2 zsh tools/preview/run.zsh          # twice, and the snapshots that differ
```

The PNGs need Pillow: `PYTHON=~/.local/share/ps3dev/venv/bin/python3` (else the snapshots stay
`.ppm`). `tools/preview/build/stage9/` keeps stage 9's snapshots (before the round of 2026-10-14) and
`compare/` sheets of the lines before and after. `tools/preview/build/before/` keeps the 0.1.0 snapshots (2026-10-12) for the look's
before and after.

`tools/preview/pv_app.c` is the Board's HTTP peer: a connect to port 80 or
8787 gets one answer and a close, every other port refuses. Each request
goes to `peer.log` in the scenario's sandbox (`build/sbx/<name>`).
`PV_BOARD=<file>` is the body (default: the Worker test's reply),
`PV_BOARD2=<file>` the body of the second and later connects (a refresh
with new values: the Board's counting and glow), and
`PV_BOARD_FAIL` gives each connect its answer, comma-separated, the last
repeating: `ok`, `refused`, `silent` (the 10 s timeout at once), `cut` (the
body without its last 30 bytes), `301`, `v2`, `hang` (the recv waits until a
`netShutdown`, at most 30 s of host time), `slow` (the answer comes 4 s of
the preview's clock after the connect). Since 0.3.1 the peer answers by path: `GET /news` gets `PV_NEWS`'s file
(absent or `404`: HTTP 404, a Worker before the reader), `GET
/article?id=<id>` gets `<PV_ARTICLES>/<id>.txt`, or the Worker's
`state=none|unknown id` reply for an id with no file. `PV_NEWS_FAIL` and
`PV_ARTICLE_FAIL` give those requests their answers in their own counts
(`PV_BOARD_FAIL`'s kinds but `refused`, which a connect cannot know, and
`404`); without them a reader request takes its connect's `PV_BOARD_FAIL`
kind. `run.zsh` passes the fixtures: `tools/preview/fixtures/` holds live
replies of 2026-10-14 saved with curl (`news.esc`, the 30 items;
`article_f80706d6.esc` Ambito, `article_451cc556.esc` Clarin,
`article_bb423307.esc` BBC; `article_01104e5c.esc` made from a Clarin one in
the summary form with the item's summary, `article_d8f03105.esc` in the cut
form, its last 4 paragraphs dropped), every byte from 0x80 as `\xNN` and the
backslash as `\\` (the repository stays ASCII), decoded into
`build/fixtures` at each run; `NEWS=none` or `NEWS=404` in a scenario gives
the list of dead feeds or no `/news`. `PV_TZ`, `PV_SUMMER`,
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

`tools/preview/pv_fx.c` is the SPU twin of the effects (with
`stub/sys/spu.h` and `stub/lv2/spu.h`, Cell Bench's): it answers the SPU
syscalls for one group and runs ps3gfx's own job drivers
(`<ps3gfx/spu_fx_kern.h>`) inside the mailbox call, so a job's picture or
quads are there when the word returns and the preview stays deterministic;
the busy ticks are fixed (sky 1.2 ms, aurora 0.8, rain 0.09). Each SPU call
but the mailbox words goes to `spu.log` in the sandbox. `PV_SPU_FAIL=<n>`
fails syscall n, `PV_FX_LATE=sky|aurora|rain` stops the reports of that
kind from the thread's `PV_FX_LATE_AT`-th job (`start`: thread 2 has no
start report), `PV_FX_KAT=bad` gives SPU 4 a wrong known-answer word. The
build adds `-ffp-contract=off`, as ps3gfx's host test of the kernels.

Scenarios: `board_ok` (the clock, both cards, the Info page, back; the
request line), `board_stale` (the dollar card's foot in YELLOW),
`board_none` (both upstreams without data), `pages` (R1 to the News, R1 wraps to
the Board, L1 to the News; SELECT opens the Info page, R1 there does
nothing, CIRCLE back to the Board),
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
exit at 60 s, `couch.txt`), `tv` (Lucas's area, `0 2 1920 1078`); the
effects: `fx_rain` (WMO 61, 88 %, 20 km/h from the south-east: the slanted
rain behind the tiles; the Info page's Effects row; the exit's reports, the
join, the statuses), `fx_snow`, `fx_frozen` (`start fx_first` in the
journal: no SPU call, the sprites and the PPU's rain; SQUARE twice, CIRCLE:
the SPUs start), `fx_late` (SPU 5 never reports its 30th rain job: the 50
ms deadline, the terminate, the join, `fx_first` failed, RSX only),
`fx_late_sky` (the 10th picture never comes: RSX only 2 s later), `fx_kat`,
`fx_fail` (the group's start fails: the destroy, the close), `fx_exit`
(START at 0.5 s during a fetch: the Board thread, then the SPUs with
`fx_first` closed, then `net_end`). Each prints its `effects` lines, the
journal's `fx_` lines and the SPU calls. The Board page of stage 9:
`board_rain` (the mockup's rainy day with a chip of each kind; SQUARE at
11.7 s brings a reply where Oficial went up and MEP down: at 19 s they show
the resting glow, amber and teal; `board_rain_first` is the first reply),
`board_rain_tv` (the same at Lucas's area; not in the narrow pass),
`board_night` and `board_ocnight` (23:00 clear and overcast: the reply puts
the place at longitude 165 with the offset and `PV_TZ` at +11 h, since the
preview's clock is fixed at 12:00 UTC), `board_storm` (WMO 95; frame 360 is
the first bolt with its flash), `board_snow` (-1 deg: the digits' minus
sign), `board_old` (the reply without the keys of 0.3.0: the "-" forms),
`board_fxoff` (`start fx_first` in the journal: RSX only, solid panels,
the glow too). Stage 10's: `start` (the loading screen at frame 3, during
the effects' job with its counter at frame 7, the start animation half way
at frame 30, the Board at 120; it prints the `start:` lines of `couch.txt`
and `boot.txt`), `skeleton` (a 4 s answer: the skeleton on the Board and
on the News page), `news_rain` (the News page with mixed sources: Ambito
"Economy", BBC "World", Infobae, Clarin), `pages_slide` (in `pages`: 6
frames into the slide to the News page), `ambient_rsx` (`start fx_first`
in the journal: 0.2.0's dimmed mode), `tv_news` and `tv_info`. The
reader's (0.3.1): `reader` (`news_list`: the 30 items, the featured card
selected; DOWN four times and RIGHT: `news_scrolling` 3 frames after the
last press, `news_scrolled` at rest; SQUARE 10 s later: a second `/news`),
`reader_article` (CROSS on Ambito's item with a 4 s answer:
`article_loading`, the skeleton; `article` with its QR code; DOWN, then
DOWN held: `article_scrolled`; CIRCLE: `article_back`, the selection
kept), `reader_states` (`article_summary`, `article_cut`, `article_none`
for an id the Worker does not know, `article_error` for no answer and
`article_error_back`, then `article_bbc`), `reader_first` (a 4 s `/news`:
the Board's headlines, `news_first`; CROSS: `news_first_cross`, the bar's
note; `news_first_full`), `reader_ambient` (an article open: no ambient
mode at 11 min, `reader_noambient`; the aurora at 21 min, `reader_ambient`;
one `/news`, 76,000 frames: not in the narrow pass). `news_rain`,
`pages_news`, `tv_news` and `skeleton_news` show the reader's list since
0.3.1, `news_none` the list of dead feeds, `news_v1` an old Worker's 404. The
`ambient` snapshots now show the aurora with the block at three of its
places. Since stage 10 a snapshot after a page change waits 28 frames (the
slide is 17), and `frozen`, `tz_skip` and `tz_12h` start later (the loading
screen's frames come before the prompt and the Board). Every text fits at
1786 and at 1600 wide, the News page's 8 headlines too. With `PASSES=2`,
the snapshots repeat byte for byte, at both areas: 95 at the default area
and 91 narrow (2026-10-14, stage 10; about 26 min for both areas twice;
stage 9: 84 and 81). Stage 11 (0.3.1): 114 at the default area and 108 narrow, about 36 min
for both areas twice (`reader_ambient` alone 6 min a pass); every snapshot
repeated byte for byte but `net_slow_narrow` and `net_slow_info_narrow` in
that run: `net_slow`'s reply lands 4 s of the preview's clock after the
connect, polled in host time, and the frame it lands in moves the sky's
scene by up to 2 levels. 0.3.0 (1870e85, from `git archive`) gives the same
two pictures (the same hashes) at the same rate, about 1 run in 5, at
either area: a race of that scenario, not of the reader. Before, an Info page after a
fetch could differ at times (`board_cut_info`, `board_stale301_info`,
`tz_12h_info`, `tz_skip_info`): the Board thread runs in host time while the
preview's clock moves per frame, so a fetch's duration reads 0.00 or 0.02 s.
The effects' start does not sleep when the six start reports are in (the
twin puts them in the start call): a sleep of real time inside a job screen
draws a varying count of frames, and 22 snapshots differed.

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
the 3 x 3 grid once, 6 px a step) and page; `wx_precip` (the effects'
drops by the code and the hour's mm, the flakes); and, in the sanitizer build,
20,000 mutated replies with every field inside its buffer. Last,
`old_check.c` builds Couch 0.1.0's `parse.c` (commit 129a6b7, read with
`git show`) and parses the same reply: within 4 KB, `BP_OK`, every old field.

Since 0.3.1, `tools/hostcheck/reader_check.c` checks the reader's pure code
in both builds: `news_parse` and `article_parse` against the Worker test's
texts (worker/test/reader.test.js: the `/news` item line and the
`/article` texts, ok, summary, cut, no date and no link, which `run.zsh`
extracts with its `NEWS_NOW` and `LINKS` put in) and the live replies of
the preview's fixtures (30 items; a Clarin article), each cut at every byte
(the whole reply, and the reply without its last LF, are the only two
`BP_OK`), CR LF, another version, the wrong first line, unknown keys, bad
ids, `-` fields, a 7th field, 31 items, long fields cut on a code point,
items at the Worker's caps, a link that would be cut (none), a link never
folded, 500 paragraphs (400 kept, marked cut), 12 KB of text; the fold;
`wrap_lines` (10 px a code point: the breaks, a long word cut by code
points, never inside a sequence, no stall, the cap),
`grid_move`, `scroll_keep`, `scroll_clamp`, `scroll_follow`,
`url_sibling`; and, in the sanitizer build, 20,000 mutated replies of the
live list and of the live article.
The repository's CI runs no app's C host checks, so this one is not in CI.

## The console rounds

Round 1, 2026-10-11: rc1 (f1df8e9), unattended, on the deployed Worker (no
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

The round of 0.3.2, 2026-10-16: rc2 ran 9 h 39 min overnight (59.94 fps, 1
frame over 25 ms: the first; the waiting screen switched from the aurora to
the sky after sunrise). rc4 (9d0c747, the hourly card on top of rc3's Board
restyle; sha256 9d0e9a90deaaca69...) installed with the install routine
(install, a quick status answer, the dialog closed, 35 s, CROSS on the icon)
and started cleanly. Every page was checked with webMAN's virtual pad on a
camera: the Board woke from the waiting screen, R1/L1, a News article with
its QR and back, Info and back; the waiting screen showed the next 12 hours
by day. Lucas: "looks very nice and works nice too". Released as 0.3.2 with
the rc4 bytes.

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
   OK. The LAN Worker also shows whether the feeds answer `wrangler dev`'s
   fetch, from the Mac: not what Cloudflare's egress gets (Infobae's own
   feed answered 200 to the Mac and 403 to the deployed Worker).
2. Unattended, `auto.txt` = `150`, `ps3run round`: `board.ppm` with the
   money extras and the sky, `info.ppm` with the Internet row; in
   `couch.txt`, the fetch lines with "connect N s" and "8 news".
3. By hand: R1 to the News (the accents of the titles), R1 back to the
   Board, L1 to the News, SELECT to the Info page, CIRCLE back. Then no button for 10 min: the screen dims; a
   minute later the News, moved 6 px; START wakes the screen and Couch does
   not exit; `couch.txt` has the two `ambient:` lines. On the TV, check the
   dim layer's strength and the 6 px moves.
4. The internet: `board_url.txt` with a dead address: "No internet since
   HH:MM" after the first fetch. The fetch and connect times in `couch.txt`
   tell whether 3 s (`NQ_SLOW_US`) fits this line.

The round of 0.3.1 (the reader; install by hand as for 0.3.0, the pkg as
`Couch-0.3.1-rcN.pkg`):
1. R1 to the News page: the list comes (`news 1: 30 items` in `couch.txt`);
   the first News page builds the atlases (`stext:` line, about 15 frames):
   watch the slowest frames for "a texture made" there.
2. DOWN, RIGHT, a held DOWN: the selection and the scroll (smooth, no step
   at the end of a move; the text sharp at rest); the scroll bar.
3. CROSS on an Ambito, a Clarin and a BBC item: the skeleton, then the text
   from the couch (is `F_MED` big enough?), UP/DOWN, a held DOWN, L2/R2;
   the QR code with a phone from the couch; CIRCLE back to the same card.
4. `couch.txt`: the `article` lines (the times include the Worker's page
   fetch, up to 8 s, and a DNS lookup per request), the slowest frames (no
   frame with "a log line" near 16 ms any more), `ui_stats`' vertices and
   draws with an article and the list on the TV.
5. An article open and no button: no ambient mode at 10 min, the aurora at
   20 min.

## Layout

`Makefile`, `build.sh`, `sfo.xml`, `pkgfiles/ICON0.PNG`, `art/make_icon.py`,
`source/` (`main.c`: the start, the loop, the pages' keys, the unattended
round, `couch.txt` and its queue; `board.c`: the thread, its three requests,
the handoff, the internet's state, the status words; `board_page.c`: the
Board page; `page.c`: the header, the ambient mode, the background;
`reader.c`: the News page, the reader (0.3.1); `stext.c`: the text and the
glass that scroll by fractions of a pixel; `qrcodegen.c`: PS3 Health's QR
encoder; `sky.c`: the
stars, the moon, the sun, the lightning, the scrims and the shared
textures; `spu_fx_run.c`: the effects' SPU group and their RSX-only
fallbacks; `http.c`: the network start and the HTTP client; `clock.c`: the
settings, the offset, the clock; `info.c`: the Info page; `parse.c`: the
pure functions, also built by the host check), `tools/preview/`,
`tools/hostcheck/` (with `old_check.c`, 0.1.0's parser, and `reader_check.c`), `tools/syscalls.allow`,
and `worker/` (the Board's Worker, its own README).
