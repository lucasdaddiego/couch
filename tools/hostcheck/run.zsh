#!/bin/zsh
# Couch's host checks (README: "The host checks"): source/parse.c, the
# Board parser and the pure helpers, and source/astro.c, the sun and the
# moon, against the Worker test's reply. The reply comes from
# couch/worker/test/board.test.js (the expected text of its first test,
# line for line), so a change of the Worker's format shows here. Since the
# Worker's stage 6 that reply holds the keys of 0.3.0 (design 5.1: geo,
# wx_fx, wx_hours, days_fx, usd_chg, money_chg, spark): it is the second
# reply as it is, and the first is the same without those lines (the reply
# of the Worker before them).
# Two builds of tools/hostcheck/board_check.c: the host's cc at -O2 with
# -Wall -Wextra -Werror, and the same with AddressSanitizer and
# UndefinedBehaviorSanitizer, which also runs 20,000 mutated replies of
# each; then old_check.c with the parse.c of Couch 0.1.0 (git show bcf4243)
# and of Couch 0.2.0 (git show f239876), read only, against both replies:
# an old Couch must keep its Board. Since 0.3.1 also reader_check.c, the
# reader's parsers (news_parse, article_parse) and helpers, against the
# Worker test's texts of couch/worker/test/reader.test.js and the preview's
# live fixtures, in both builds (the sanitizer one with 20,000 mutated
# replies of each). Each prints PASS or FAIL per group; the exit status is
# the number of failures.
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
NEW = ('geo=', 'wx_fx=', 'wx_hours=', 'days_fx=', 'usd_chg=', 'money_chg=', 'spark=')
if not any(x.startswith(NEW) for x in items):
    print("the Worker test's reply has no key of 0.3.0")
    sys.exit(1)
old = [x for x in items if not x.startswith(NEW)]
text = '\n'.join(old)
open(sys.argv[2], 'w', encoding='utf-8', newline='').write(text)
print("the Worker test's reply without the keys of 0.3.0: %d lines, %d bytes" % (len(old) - 1, len(text.encode())))
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
# The reader (0.3.1): the Worker test's /news item and /article texts (couch/worker/test/reader.test.js,
# with its NEWS_NOW and LINKS put in), and the live replies of the preview's fixtures, decoded.
RT=${H:h:h}/worker/test/reader.test.js
${PYTHON:-python3} -I - $RT ${H:h}/preview/fixtures $B <<'PY' || { print "FAIL the reader test's texts or the fixtures: not found"; exit 1 }
import codecs, os, re, sys
src = open(sys.argv[1], encoding='utf-8').read()
fix, out = sys.argv[2], sys.argv[3]
now = re.search(r"const NEWS_NOW = (\d+);", src).group(1)
links = dict(re.findall(r"^\s+(\w+): '([^']*)',?$", re.search(r"const LINKS = \{(.*?)\};", src, re.S).group(1), re.M))
def lit(t):
    q, body = t[0], t[1:-1]
    if q == '`':
        body = body.replace('${NEWS_NOW}', now)
        body = re.sub(r"\$\{LINKS\.(\w+)\}", lambda m: links[m.group(1)], body)
    return body.encode('utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8') if '\\' in body else body
tok = r"'(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\"|`(?:[^`\\]|\\.)*`"
arrays = [m.group(1) for m in re.finditer(r"assert\.equal\(t, \[(.*?)\]\.join\('\\n'\)\);", src, re.S)]
arts = ['\n'.join(lit(x) for x in re.findall(tok, a)) for a in arrays if 'couch-article 1' in a]
singles = [lit(m.group(0)) for m in re.finditer(r"'couch-article 1\\n(?:[^'\\]|\\.)*'", src)]
item = re.search(r"assert\.equal\(items\[0\], ('(?:[^'\\]|\\.)*') \+\s*('(?:[^'\\]|\\.)*')\);", src)
if len(arts) < 2 or len(singles) < 2 or not item:
    sys.exit(1)
news = 'couch-news 1\nnow=%s\n%s%s\nnews_age=0\nnews_state=ok\nend\n' % (now, lit(item.group(1)), lit(item.group(2)))
files = {'r_news.txt': news, 'r_art_ok.txt': arts[0], 'r_art_summary.txt': arts[1], 'r_art_cut.txt': singles[0], 'r_art_long.txt': singles[1]}
for n, t in files.items():
    open(os.path.join(out, n), 'w', encoding='utf-8', newline='').write(t)
for n, d in (('news', 'r_live_news.txt'), ('article_451cc556', 'r_live_article.txt')):
    b = codecs.escape_decode(open(os.path.join(fix, n + '.esc'), encoding='ascii').read().encode('ascii'))[0]
    open(os.path.join(out, d), 'wb').write(b)
print("the reader test's texts: /news 1 item, /article %d texts; the live /news and one live /article" % (len(arts[:2]) + len(singles[:2])))
PY
RR=($B/r_news.txt $B/r_art_ok.txt $B/r_art_summary.txt $B/r_art_cut.txt $B/r_art_long.txt $B/r_live_news.txt $B/r_live_article.txt)
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
    (( $(command grep -c '^PASS 20,000 mutated' $B/reader_asan.out) == 2 )) && print -r -- "PASS the reader's parsers under the sanitizers, and 20,000 mutated replies of each"
    (( fails += rc ))
else
    print "FAIL reader_check_asan: does not build"; (( ++fails ))
fi
# The old parsers: Couch 0.1.0 (bcf4243, as before) and 0.2.0 (f239876).
for rev ver in bcf4243 0.1.0 f239876 0.2.0; do
    print -r -- "-- Couch $ver's parser (source/parse.c at $rev, from git) against both replies:"
    mkdir -p $B/old_$ver
    if git -C ${H:h:h} show ${rev}:couch/source/parse.c > $B/old_$ver/parse.c 2>/dev/null &&
       git -C ${H:h:h} show ${rev}:couch/source/parse.h > $B/old_$ver/parse.h 2>/dev/null &&
       $CC -O2 -std=c11 -Wall -Wextra -Werror -I$B/old_$ver $H/old_check.c $B/old_$ver/parse.c -o $B/old_$ver/old_check; then
        for r in $R; do $B/old_$ver/old_check $r "$ver ($rev)" || (( fails += $? )); done
    else
        print "FAIL old_check: $ver's parse.c not in git, or it does not build"; (( ++fails ))
    fi
done
print "$fails failed"
exit $fails
