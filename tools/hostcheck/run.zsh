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
# an old Couch must keep its Board. Each prints PASS or FAIL per group; the
# exit status is the number of failures.
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
