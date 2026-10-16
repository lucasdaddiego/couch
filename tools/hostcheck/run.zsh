#!/bin/zsh
# Couch's host checks (README: "The host checks"): source/parse.c, the
# Board parser and the pure helpers, and source/astro.c, the sun and the
# moon, against the Worker test's reply. The reply comes from
# worker/test/board.test.js (the expected text of its first test, line for
# line), so a change of the Worker's format shows here: the full reply, and
# the same without the derived keys (geo, wx_fx, wx_hours, days_fx, the
# changes, the series, feature, numfmt; since 0.5.0 wx_soon, air, futbol),
# as a minimal Worker sends it.
# Two builds of tools/hostcheck/board_check.c: the host's cc at -O2 with
# -Wall -Wextra -Werror, and the same with AddressSanitizer and
# UndefinedBehaviorSanitizer, which also runs 20,000 mutated replies of
# each. Also reader_check.c, the reader's parsers (news_parse,
# article_parse) and helpers, against the Worker's /news and /article forms
# and the preview's fixtures, in both builds (the sanitizer one with 20,000
# mutated replies of each). Each prints PASS or FAIL per group; the exit
# status is the number of failures.
#   zsh tools/hostcheck/run.zsh
H=${0:A:h}
B=$H/build
S=${H:h:h}/source
W=${H:h:h}/worker/test/board.test.js
mkdir -p $B
CC=${CC:-cc}
F=(-std=c11 -Wall -Wextra -Werror -I$S)
print -r -- "host: $($CC --version 2>&1 | head -1)"
fails=0
# Compile the production logger queue (main.c) against a writer
# that holds its borrowed buffer while a second caller waits to write.
${PYTHON:-python3} -I - ${H:h:h}/source/main.c $B/log_impl.h <<'PY' || { print "FAIL extracting the logger from main.c"; exit 1 }
import sys
s = open(sys.argv[1], encoding='utf-8').read()
a = s.index('#define LOGQ 16384')
b = s.index('/* The three slowest frames', a)
open(sys.argv[2], 'w').write(s[a:b])
PY
if $CC -std=c11 -Wall -Wextra -Werror -pthread -I$B $H/log_check.c -o $B/log_check; then
    $B/log_check || (( fails += $? ))
else
    print "FAIL logger host check: does not build"; (( ++fails ))
fi
# The reply: the array of the first assert.equal(t, [...].join('\n')) in the Worker test.
${PYTHON:-python3} -I - $W $B/worker_reply.txt $B/worker_reply_new.txt <<'PY' || { print "FAIL the Worker test's reply: not found in $W"; exit 1 }
import re, sys
src = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r"assert\.equal\(t, \[(.*?)\]\.join\('\\n'\)\);", src, re.S)
if not m:
    sys.exit(1)
items = re.findall(r"'((?:[^'\\]|\\.)*)'", m.group(1))
if len(items) < 3 or items[0] != 'couch-board 1' or items[-2:] != ['end', '']:
    sys.exit(1)
NEW = ('geo=', 'wx_fx=', 'wx_hours=', 'days_fx=', 'rate_chg=', 'extra_chg=', 'spark=', 'feature=', 'numfmt=', 'wx_soon=', 'air=', 'futbol=')
if not any(x.startswith(NEW) for x in items):
    print("the Worker test's reply has no derived key")
    sys.exit(1)
old = [x for x in items if not x.startswith(NEW)]
text = '\n'.join(old)
open(sys.argv[2], 'w', encoding='utf-8', newline='').write(text)
print("the Worker test's reply without the derived keys: %d lines, %d bytes" % (len(old) - 1, len(text.encode())))
text2 = '\n'.join(items)
open(sys.argv[3], 'w', encoding='utf-8', newline='').write(text2)
print("the Worker test's reply: %d lines, %d bytes" % (len(items) - 1, len(text2.encode())))
PY
R=($B/worker_reply.txt $B/worker_reply_new.txt)
if $CC -O2 $F $H/board_check.c $S/parse.c $S/astro.c -lm -o $B/board_check; then
    $B/board_check $R || (( fails += $? ))
else
    print "FAIL board_check: does not build"; (( ++fails ))
fi
print -r -- "-- the same with AddressSanitizer and UndefinedBehaviorSanitizer, and the mutated replies (the PASS lines not repeated):"
if $CC -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all $F $H/board_check.c $S/parse.c $S/astro.c -lm -o $B/board_check_asan; then
    $B/board_check_asan $R fuzz > $B/asan.out 2>&1
    rc=$?
    command grep -v -e '^PASS' -e 'cuts of' $B/asan.out
    (( $(command grep -c '^PASS 20,000 mutated' $B/asan.out) == 2 )) && print -r -- "PASS 20,000 mutated replies of each reply under the sanitizers"
    (( fails += rc ))
else
    print "FAIL board_check_asan: does not build"; (( ++fails ))
fi
# The reader (0.3.1): the Worker's /news and /article forms (written here, as worker/test/reader.test.js
# makes them), and the preview's replies (tools/preview/fixtures), decoded.
FSRC=${H:h}/preview/fixtures
${PYTHON:-python3} -I - $FSRC $B <<'PY' || { print "FAIL the preview's fixtures: not found"; exit 1 }
import codecs, os, sys
fix, out = sys.argv[1], sys.argv[2]
link = 'https://news.example.org/world/leaders-meet-in-z%C3%BCrich'
head = 'couch-article 1\nnow=1792164000\nid=1a2b3c4d\nsource=Zürich Post\ncategory=world\ntitle=Leaders meet in Zürich to agree on a new plan for river shipping\ndate=Fri 16 Oct 17:10|600\nlink=%s\n' % link
files = {
    'r_news.txt': 'couch-news 1\nnow=1792164000\nitem=1a2b3c4d|Zürich Post|world|600|Leaders meet in Zürich to agree on a new plan for river shipping|'
                  'A short summary of the story, and the café\'s climate plan.\nnews_age=0\nnews_state=ok\nend\n',
    'r_art_ok.txt': head + 'state=ok\np=First paragraph: café, naïve.\np=Second paragraph with spaces.\nend\n',
    'r_art_summary.txt': head + 'state=summary|news.example.org: HTTP 403\np=A short summary of the story.\nend\n',
    'r_art_cut.txt': 'couch-article 1\nnow=1792000600\nid=abcdef01\nsource=L\ncategory=-\ntitle=T whole\ndate=Wed 14 Oct 17:46|600\nlink=https://x.example/a%7Cb\nstate=cut\np=one\np=two\nend\n',
    'r_art_long.txt': 'couch-article 1\nnow=1\nid=abcdef01\nsource=-\ncategory=-\ntitle=T whole\ndate=-|-\nstate=summary|x: HTTP 403\nend\n',
    # 0.5.0: /futbol as worker/test/board.test.js makes it from its made-up teams
    'r_futbol.txt': 'couch-futbol 1\nnow=1792164000\nday=Today, Fri 16 Oct\nleague=Spree League|Germany\n'
                    'game=Final|FC Kiezblau|Union Mitte|2|1|Spree TV|end\ngame=18:30|Havel Rovers|SV Tegelsee|-|-|Spree TV, Kanal 9|pre\n'
                    'game=20:30|Borussia Wedding|Dynamo Pankow|-|-|-|pre\nleague=Cup of the North|Europe\n'
                    'game=Pens 4-3|Nordhafen|Ostkreuz United|1|1|Kanal 9|end\ngame=Postponed|Grünau 04|Müggel SC|-|-|-|off\n'
                    'futbol_age=120\nfutbol_state=ok\nend\n',
}
for n, t in files.items():
    open(os.path.join(out, n), 'w', encoding='utf-8', newline='').write(t)
for n, d in (('news', 'r_live_news.txt'), ('article_451cc556', 'r_live_article.txt')):
    b = codecs.escape_decode(open(os.path.join(fix, n + '.esc'), encoding='ascii').read().encode('ascii'))[0]
    open(os.path.join(out, d), 'wb').write(b)
print("the reader's texts: /news 1 item, /article 4 texts, /futbol 5 games; the preview's /news and one /article")
PY
RR=($B/r_news.txt $B/r_art_ok.txt $B/r_art_summary.txt $B/r_art_cut.txt $B/r_art_long.txt $B/r_live_news.txt $B/r_live_article.txt $B/r_futbol.txt)
print -r -- "-- the reader's parsers (reader_check.c):"
if $CC -O2 $F $H/reader_check.c $S/parse.c -lm -o $B/reader_check; then
    $B/reader_check $RR || (( fails += $? ))
else
    print "FAIL reader_check: does not build"; (( ++fails ))
fi
if $CC -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all $F $H/reader_check.c $S/parse.c -lm -o $B/reader_check_asan; then
    $B/reader_check_asan $RR fuzz > $B/reader_asan.out 2>&1
    rc=$?
    command grep -v -e '^PASS' $B/reader_asan.out | command grep -v '^0 failed'
    (( $(command grep -c '^PASS 20,000 mutated' $B/reader_asan.out) == 3 )) && print -r -- "PASS the reader's and the futbol parsers under the sanitizers, and 20,000 mutated replies of each"
    (( fails += rc ))
else
    print "FAIL reader_check_asan: does not build"; (( ++fails ))
fi
print "$fails failed"
exit $fails
