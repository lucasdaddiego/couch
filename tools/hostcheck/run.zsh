#!/bin/zsh
# Couch's host checks (README: "The host checks"): source/parse.c, the
# Board parser and the pure helpers, against the Worker test's reply. The
# reply comes from couch/worker/test/board.test.js (the expected text of its
# first test, line for line), so a change of the Worker's format shows here.
# Two builds of tools/hostcheck/board_check.c: the host's cc at -O2 with
# -Wall -Wextra -Werror, and the same with AddressSanitizer and
# UndefinedBehaviorSanitizer, which also runs 20,000 mutated replies; then
# old_check.c with Couch 0.1.0's parse.c (git show bcf4243, read only)
# against the same reply: an old Couch must keep its Board. Each prints PASS
# or FAIL per group; the exit status is the number of failures.
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
${PYTHON:-python3} -I - $W $B/worker_reply.txt <<'PY' || { print "FAIL the Worker test's reply: not found in $W"; exit 1 }
import re, sys
src = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r"assert\.equal\(t, \[(.*?)\]\.join\('\\n'\)\);", src, re.S)
if not m:
    sys.exit(1)
items = re.findall(r"'((?:[^'\\]|\\.)*)'", m.group(1))
if len(items) < 3 or items[0] != 'couch-board 1' or items[-2:] != ['end', '']:
    sys.exit(1)
text = '\n'.join(items)
open(sys.argv[2], 'w', encoding='utf-8', newline='').write(text)
print("the Worker test's reply: %d lines, %d bytes" % (len(items) - 1, len(text.encode())))
PY
if $CC -O2 $F $H/board_check.c $S/parse.c -o $B/board_check; then
    $B/board_check $B/worker_reply.txt || (( fails += $? ))
else
    print "FAIL board_check: does not build"; (( ++fails ))
fi
print -r -- "-- the same with AddressSanitizer and UndefinedBehaviorSanitizer, and the mutated replies (the PASS lines not repeated):"
if $CC -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all $F $H/board_check.c $S/parse.c -o $B/board_check_asan; then
    $B/board_check_asan $B/worker_reply.txt fuzz > $B/asan.out 2>&1
    rc=$?
    command grep -v -e '^PASS' -e 'cuts of' $B/asan.out
    command grep -q '^PASS 20,000 mutated' $B/asan.out && print -r -- "PASS 20,000 mutated replies under the sanitizers"
    (( fails += rc ))
else
    print "FAIL board_check_asan: does not build"; (( ++fails ))
fi
print -r -- "-- Couch 0.1.0's parser (source/parse.c at bcf4243, from git) against the same reply:"
mkdir -p $B/old
if git -C ${H:h:h} show bcf4243:couch/source/parse.c > $B/old/parse.c 2>/dev/null &&
   git -C ${H:h:h} show bcf4243:couch/source/parse.h > $B/old/parse.h 2>/dev/null &&
   $CC -O2 -std=c11 -Wall -Wextra -Werror -I$B/old $H/old_check.c $B/old/parse.c -o $B/old_check; then
    $B/old_check $B/worker_reply.txt || (( fails += $? ))
else
    print "FAIL old_check: 0.1.0's parse.c not in git, or it does not build"; (( ++fails ))
fi
print "$fails failed"
exit $fails
