#!/bin/zsh
# The Worker on the Mac's LAN address, for the PS3 and for curl: a git archive of the last commit
# of worker/ (commit first), a node:22 image, npm ci, the tests, then wrangler dev on
# 0.0.0.0:8787 published on the Mac. Ctrl-C stops it; the image is removed at the end.
#   zsh worker/dev.zsh            (then: curl http://<the Mac's IP>:8787/board)
#   zsh worker/dev.zsh test       (only the tests)
set -e
R=${0:A:h:h}
ctx=$(mktemp -d)
trap 'docker rmi -f couch-worker:dev > /dev/null 2>&1; rm -rf "${ctx:?}"' EXIT
trap 'exit 130' INT TERM HUP    # zsh skips the EXIT trap on a signal: exit runs it
git -C $R archive -o $ctx/worker.tar HEAD:worker
# the news fixtures (publishers' text) are not in the repo: from COUCH_FIXTURES when there,
# else their tests are skipped (test/fixtures/README.md)
F=${COUCH_FIXTURES:-$HOME/code/ps3tools_private/apps/couch-fixtures}/worker
[[ -d $F ]] && tar -rf $ctx/worker.tar -C $F -s ',^,test/fixtures/,' $(cd $F && print -r -- *.(xml|html))
cp ${0:A:h}/Dockerfile $ctx/
docker build -q -t couch-worker:dev $ctx > /dev/null
docker run --rm couch-worker:dev npm test
[[ ${1:-} == test ]] && exit 0
print "the Worker: http://$(ipconfig getifaddr en0 2>/dev/null || ipconfig getifaddr en1):8787/board (Ctrl-C stops it)"
docker run --rm --init -p 8787:8787 couch-worker:dev
