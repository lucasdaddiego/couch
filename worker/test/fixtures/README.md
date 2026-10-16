# Worker test fixtures

The JSON and ZIP files here are public data answers (SMN, Open-Meteo, DolarApi,
argentinadatos). The news feeds and article pages the tests also read
(`*.xml`, `*.html`) are the publishers' text, so they are not in this public
repo: without them, the tests that need them are skipped with that reason.
`worker/dev.zsh` adds them from `$COUCH_FIXTURES/worker` when that folder is
there.
