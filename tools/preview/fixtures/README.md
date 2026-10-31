# Preview fixtures

The reader's replies the preview serves: `news.esc` (a `/news` reply, 30 items)
and `article_<id>.esc` (`/article` replies: ok, summary, cut, a long one). The
text is made up: each word is a word of the same length from Python's Zen, so
the lines keep the lengths, the wraps and the cuts of real headlines and
articles. Every byte from 0x80 is written as `\xNN` and the backslash as `\\`;
`run.zsh` decodes them into `build/fixtures` at each run.

`images/<id>.jpg` (0.6.0): the photo `/image?id=<id>` answers for an article
whose reply holds `photo=1` (`article_f80706d6.esc`): a made-up 1200 x 675
scene drawn by PIL (a sky, a sun, hills, a house), progressive JPEG, 21 KB,
which the console decodes to 960 x 540. `run.zsh` points `PV_IMAGES` at this
folder as it is: no decode step.
