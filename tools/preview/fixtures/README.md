# Preview fixtures

The preview's news list and article replies (`news.esc`, `article_*.esc`) hold
the publishers' text, so they are not in this public repo. `run.zsh` and the
host checks read them from `$COUCH_FIXTURES/preview`; without them the preview
stops with a note.
