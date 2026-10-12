# Couch's Board Worker

`GET /board` answers the Board's data for Couch on the PS3 as plain text (format version 1:
`~/.claude/plans/2026-10-10-couch-design.md`, section 4): the dollar rates of dolarapi.com, the
weather now of SMN (Argentina's Servicio Meteorologico Nacional), else Open-Meteo, and the days
of Open-Meteo, each upstream kept in the Cache API with its time (300 s, 600 s and 900 s), so a
failed upstream gives the old values marked `stale` with their real age. Every setting is a var
in `wrangler.jsonc` (the city, its coordinates and time zone, the SMN station, the rates and
their labels). The place is Junin, Buenos Aires (since 2026-10-12, Lucas's choice): `CITY`
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

- `src/board.js`: the reply's format, pure functions.
- `src/smn.js`: SMN's zip, its Latin-1 text and its rows, pure functions.
- `src/index.js`: the Worker: the route, the upstreams, the cache.
- `test/`: `node --test` against the upstream answers of 2026-10-10, SMN's zip of 2026-10-12
  (`fixtures/tiepre.zip`) and a fake fetch and cache. CI runs them on every push (no npm
  install: the tests need only Node).
- `dev.zsh`: the tests and `wrangler dev` in a throwaway container from a git archive (commit
  first), published on the Mac's LAN address, port 8787, for the PS3 and for curl.

Deployed on `http://couch.daddiego.com.ar/board` (2026-10-10, Lucas's OK): `wrangler deploy
--env-file ~/.config/cloudflare/personal.env` from this folder, after a commit. Plain HTTP works
because the zone `daddiego.com.ar` has Always Use HTTPS off and one Single Redirect rule
(HTTP to HTTPS for every host but this one): a Configuration Rule cannot change that setting.
A new host on the zone gets the redirect by itself. Turning Always Use HTTPS on again breaks
Couch, which has no TLS.
