# Couch's Board Worker

`GET /board` answers the Board's data for Couch on the PS3 as plain text (format version 1:
`~/.claude/plans/2026-10-10-couch-design.md`, section 4): the dollar rates of dolarapi.com and
the weather of Open-Meteo, each kept in the Cache API with its time (300 s and 900 s), so a
failed upstream gives the old values marked `stale` with their real age. Every setting is a var
in `wrangler.jsonc` (the city, its coordinates and time zone, the rates and their labels).

- `src/board.js`: the reply's format, pure functions.
- `src/index.js`: the Worker: the route, the upstreams, the cache.
- `test/`: `node --test` against the upstream answers of 2026-10-10 and a fake fetch and cache.
  CI runs them on every push (no npm install: the tests need only Node).
- `dev.zsh`: the tests and `wrangler dev` in a throwaway container from a git archive (commit
  first), published on the Mac's LAN address, port 8787, for the PS3 and for curl.

Not deployed: the deploy and the zone's per-host rule (plain HTTP for the Worker's host) wait
for Lucas's OK.
