#!/bin/zsh
# Preview scenarios: each runs the app in a fresh sandbox with a pad script.
#   zsh tools/preview/run.zsh [scenario]     (default: all; names in the case below)
#   AREA=narrow zsh tools/preview/run.zsh    (every scenario at the narrowest visible area, 1600 x 900,
#                                             snapshots named *_narrow)
#   PASSES=2 zsh tools/preview/run.zsh       (twice, the second into an emptied build/out2, and the
#                                             snapshots of the second pass that differ from the
#                                             first's: the Board thread runs in host time)
# Snapshots go to build/out/*.ppm, and *.png when $PYTHON (or python3) has
# Pillow. The sandbox of each scenario stays in build/sbx/<name>: couch.txt,
# journal.txt, peer.log (the requests the peer got), the shots of auto.
# The reply files are written here at run time: the sources stay ASCII,
# the degree sign is the bytes $'\xc2\xb0'.
P=${0:A:h}
OUT=${OUT:-$P/build/out}
DIR=dev_hdd0/tmp/couch
[[ -x $P/build/pv && -x $P/build/pv_utf8_off ]] || { print -u2 "run build.zsh first"; exit 1 }
if [[ ${PASSES:-1} == 2 ]]; then
    PASSES=1 zsh $0 "$@" || exit $?
    rm -rf $P/build/out2                     # the second pass's snapshots only: no old or *_narrow files
    PASSES=1 OUT=$P/build/out2 zsh $0 "$@" > /dev/null || exit $?
    for f in $P/build/out2/*.ppm(N); do cmp -s $f $OUT/${f:t} || print "differs between the passes: ${f:t:r}"; done
    exit 0
fi
mkdir -p $OUT
SFX= AREA_DEF='67 38 1853 1042'                          # the default visible area (93 %)
[[ $AREA == narrow ]] && { SFX=_narrow; AREA_DEF='160 90 1760 990' }
D=$'\xc2\xb0' DASH=$'\xe2\x80\x93' NL=$'\n'           # the degree sign, an en dash (U+2013), a line end
TZ_DONE=$'done tz_params rc=0x00000000 ok=1\n'        # the clock settings read before: no prompt
URL_LAN=http://192.168.0.3:8787/board                 # a dotted address: the fake peer needs no DNS

# The Worker test's first reply (couch/worker/test/board.test.js: Junin on 2026-10-13, every
# upstream, line for line), extracted once per run as the host check does: body_ok, and the
# others derived from it line by line, so this file stays ASCII.
REPLY=$P/build/worker_reply.txt
mkdir -p $P/build
${PYTHON:-python3} -I - ${P:h:h}/worker/test/board.test.js $REPLY <<'PY' || { print -u2 "the Worker test's reply: not found"; exit 1 }
import re, sys
src = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r"assert\.equal\(t, \[(.*?)\]\.join\('\\n'\)\);", src, re.S)
items = re.findall(r"'((?:[^'\\]|\\.)*)'", m.group(1)) if m else []
if len(items) < 3 or items[0] != 'couch-board 1' or items[-2:] != ['end', '']:
    sys.exit(1)
open(sys.argv[2], 'w', encoding='utf-8', newline='').write('\n'.join(items))
PY
body_ok() { print -rn -- "$(< $REPLY)"$'\n'; }
each() {   # the reply with each line through the function $1 (it prints the line, another, or nothing)
    local l
    for l in "${(@f)$(< $REPLY)}"; do $1 "$l"; done
}
body_stale() { each stale_line; }
stale_line() { case $1 in usd_age=*) print -r -- "usd_age=4000";; usd_state=*) print -r -- "usd_state=stale|dolarapi.com: HTTP 503";; *) print -r -- "$1";; esac; }
body_none() {
    print -rn -- "couch-board 1
now=1791913620
offset=-10800
city=Buenos Aires
usd_age=0
usd_state=none|dolarapi.com: HTTP 503
wx_moon=Waxing crescent moon
wx_age=0
wx_state=none|open-meteo: no answer in 4 s
news_age=0
news_state=none|clarin.com: HTTP 403
end
"
}
body_smn() { each smn_line; }    # the now line from SMN's station (the Worker test's SMN reply)
smn_line() { case $1 in wx_now=*) print -r -- "wx_now=10$D|10$D|84%|Cloudy|3 km/h|night|SMN 23:00";; *) print -r -- "$1";; esac; }
body_accents() { each accents_line; }   # a city with an n tilde (U+00F1) and a reason with an en dash, for the fold
accents_line() {
    case $1 in
    (city=*) print -r -- "city=Ca"$'\xc3\xb1'"uelas, Provincia de Buenos Aires";;
    (usd_age=*) print -r -- "usd_age=900";;
    (usd_state=*) print -r -- "usd_state=stale|dolarapi.com: HTTP 503 $DASH maintenance";;
    (*) print -r -- "$1";;
    esac
}
body_extras() { each extras_line; }   # the money extras none or stale, no rain, no news (the feeds failed)
extras_line() {
    case $1 in
    (money=Riesgo*) print -r -- "${1%%|*}|-|-|none|argentinadatos.com: HTTP 503";;
    (money=Plazo*) print -r -- "${1%|ok}|stale|argentinadatos.com: no answer in 4 s";;
    (wx_rain=*) print -r -- "wx_rain=dry|No rain in the next 12 h";;
    (news=*) ;;
    (news_age=*) print -r -- "news_age=0";;
    (news_state=*) print -r -- "news_state=none|clarin.com: HTTP 403";;
    (*) print -r -- "$1";;
    esac
}
body_v1() {    # a Worker of 0.1.0's time (the reply of 2026-10-10): no money, no sky, no news
    print -rn -- "couch-board 1
now=1791614218
offset=-10800
city=Buenos Aires
usd=Oficial|1.485,00|1.535,00|Fri 15:00
usd=Blue|1.520,00|1.540,00|Fri 18:00
usd=MEP|1.535,90|1.542,90|Fri 18:00
usd=CCL|1.605,50|1.606,60|Fri 18:00
usd=Cripto|1.601,81|1.606,88|Fri 18:00
usd=Tarjeta|1.930,50|1.995,50|Fri 15:00
usd_age=42
usd_state=ok
wx_now=11$D|10$D|92%|Overcast|9 km/h|night|Open-Meteo
day=Today|Rain showers|16$D|11$D|96%
day=Sun|Overcast|16$D|9$D|2%
day=Mon|Drizzle|16$D|10$D|22%
wx_age=310
wx_state=ok
end
"
}

scn() {   # name script [VAR=value ...]; JOURNAL, BODY, URL, AUTO, BIN, SAFE from the caller
    local name=$1$SFX e s=()
    for e in ${=2}; do [[ $e == *SNAP=* ]] && e=$e$SFX; s+=$e; done
    shift 2
    local sbx=$P/build/sbx/$name
    rm -rf $sbx
    mkdir -p $sbx/$DIR
    print -r -- "${SAFE:-$AREA_DEF}" > $sbx/$DIR/safe_area.txt
    print -rn -- "${JOURNAL-$TZ_DONE}" > $sbx/$DIR/journal.txt
    [[ ${URL-$URL_LAN} != none ]] && print -r -- "${URL-$URL_LAN}" > $sbx/$DIR/board_url.txt
    [[ -n $AUTO ]] && print -r -- "$AUTO" > $sbx/$DIR/auto.txt
    [[ -n $NOFOLDER ]] && { rm -rf $sbx/$DIR; : > $sbx/$DIR }   # the app folder is a file: no folder, no journal
    body_${BODY:-ok} > $sbx/body.txt
    env PV_ROOT=$sbx PV_OUT=$OUT PV_BOARD=$sbx/body.txt PV_SCRIPT="${(j: :)s}" "$@" $P/build/${BIN:-pv} 2>&1 | command grep -v '^snap' | head -5
    print -- "$name: exit ${pipestatus[1]}"
}

ends() {   # name: the start's and the exit's lines in couch.txt, and the netctl and exit calls the peer saw (its "pv: " lines)
    local d=$P/build/sbx/$1$SFX
    local lines=$(command grep -h -e '^quit ' -e '^net_start' -e '^board thread' -e '^net_end' -e '^exit at' $d/$DIR/couch.txt 2>/dev/null)
    local calls=$(command grep -h '^pv: ' $d/peer.log 2>/dev/null | cut -c5-)
    print -r -- "  couch.txt: ${(j:; :)${(f)lines}}"
    print -r -- "  net calls: ${(j:; :)${(f)calls}}"
}

case ${1:-all} in
board_ok|all)      # the reply of the Worker test: the clock, both cards; the Info screen; back
    scn board_ok "120:SNAP=board_ok 122:SELECT 126:SNAP=info 128:CIRCLE 132:SNAP=board_back 133:EXIT"
    print -r -- "  the request: $(command grep -m1 '^GET' $P/build/sbx/board_ok$SFX/peer.log | tr -d '\r')"
    ends board_ok ;|
board_stale|all)   # the dollar upstream failed, the Worker kept its old rates: the card's foot in YELLOW
    BODY=stale scn board_stale "120:SNAP=board_stale 121:EXIT" ;|
board_none|all)    # both upstreams failed with no old data
    BODY=none scn board_none "120:SNAP=board_none 121:EXIT" ;|
pages|all)         # L1 and R1: the Board, the News, the Info page, R1 wraps to the Board, L1 back to the Info page
    scn pages "120:SNAP=pages_board 122:R1 126:SNAP=pages_news 128:R1 132:SNAP=pages_info 134:R1 138:SNAP=pages_back 140:L1 144:SNAP=pages_l1 145:EXIT" ;|
board_extras|all)  # the money extras none and stale, a dry forecast, the feeds failed: the News page says why
    BODY=extras scn board_extras "120:SNAP=board_extras 122:R1 126:SNAP=news_none 127:EXIT" ;|
board_v1|all)      # a Worker of 0.1.0's time: no money, no sky, no news lines (the new Couch draws what came)
    BODY=v1 scn board_v1 "120:SNAP=board_v1 122:R1 126:SNAP=news_v1 127:EXIT" ;|
net_slow|all)      # the answer takes 4 s: the header says the internet is slow
    scn net_slow "400:SNAP=net_slow 402:SELECT 406:SNAP=net_slow_info 407:EXIT" PV_BOARD_FAIL=slow ;|
net_down|all)      # a good reply, then SQUARE after 10 s and a refused connect: no internet since 09:00
    scn net_down "120:SNAP=net_down_first 700:SQUARE 820:SNAP=net_down 822:SELECT 826:SNAP=net_down_info 827:EXIT" PV_BOARD_FAIL=ok,refused ;|
ambient|all)       # 10 min with no button: dimmed (the Board); a minute on, the News moved 6 px; then the Board moved
                   # again; START wakes the screen and does not exit; the fetches go on every 5 min
    scn ambient "120:SNAP=ambient_before 36120:SNAP=ambient 39720:SNAP=ambient_news 43320:SNAP=ambient_board2 43340:START 43360:SNAP=ambient_awake 43370:EXIT" PV_MAXFRAMES=44000
    print -r -- "  requests: $(command grep -c '^GET' $P/build/sbx/ambient$SFX/peer.log); couch.txt: $(command grep -h '^ambient' $P/build/sbx/ambient$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
board_smn|all)     # Junin, the now line from SMN: the weather card's foot names both sources; the same folded to ASCII
    BODY=smn scn board_smn "120:SNAP=board_smn 121:EXIT"
    BIN=pv_utf8_off BODY=smn scn board_smn_utf8_off "120:SNAP=board_smn_utf8_off 121:EXIT" ;|
board_301|all)     # the host redirects plain HTTP (Always Use HTTPS back on)
    scn board_301 "120:SNAP=board_301 122:SELECT 126:SNAP=board_301_info 127:EXIT" PV_BOARD_FAIL=301 ;|
board_stale301|all) # a good reply, then SQUARE after 10 s and a 301: the stale line keeps the data's time, the Info screen the Location
    scn board_stale301 "120:SNAP=board_stale301_first 700:SQUARE 820:SNAP=board_stale301 822:SELECT 826:SNAP=board_stale301_info 827:EXIT" PV_BOARD_FAIL=ok,301 ;|
board_cut|all)     # a good reply, then SQUARE after 10 s and a cut one: the old Board stays
    scn board_cut "120:SNAP=board_cut_first 700:SQUARE 820:SNAP=board_cut 822:SELECT 826:SNAP=board_cut_info 827:EXIT" PV_BOARD_FAIL=ok,cut ;|
board_v2|all)      # a reply of format version 2
    scn board_v2 "120:SNAP=board_v2 121:EXIT" PV_BOARD_FAIL=v2 ;|
board_silent|all)  # the peer accepts and never answers (SO_RCVTIMEO)
    scn board_silent "120:SNAP=board_silent 121:EXIT" PV_BOARD_FAIL=silent ;|
board_refused|all) # nothing listens on the port
    scn board_refused "120:SNAP=board_refused 121:EXIT" PV_BOARD_FAIL=refused ;|
board_dns|all)     # no board_url.txt: the default host, and the preview has no DNS
    URL=none scn board_dns "120:SNAP=board_dns 121:EXIT" ;|
frozen|all)        # the network start froze before: the Board says so; SELECT, SQUARE twice, CIRCLE: the start runs and the Board fills
    JOURNAL=$'start net_start\n'$TZ_DONE scn frozen "60:SNAP=frozen 62:SELECT 66:SNAP=frozen_info 70:SQUARE 74:SQUARE 78:SNAP=frozen_cleared 80:CIRCLE 200:SNAP=frozen_after 201:EXIT" ;|
tz_skip|all)       # a first start: the clock's prompt, CIRCLE skips it, the Board's offset sets the clock
    JOURNAL= scn tz_skip "10:SNAP=prompt 12:CIRCLE 130:SNAP=tz_skip 132:SELECT 136:SNAP=tz_skip_info 137:EXIT" ;|
tz_12h|all)        # CROSS at the prompt; TIME_FORMAT 0 (a 12-hour clock), UTC+5:30
    JOURNAL= scn tz_12h "12:CROSS 130:SNAP=tz_12h 132:SELECT 136:SNAP=tz_12h_info 137:EXIT" PV_TIMEFMT=0 PV_TZ=330 ;|
utf8_off|all)      # text_utf8 failed (the preview's second build): the fold to ASCII
    BIN=pv_utf8_off BODY=accents scn utf8_off "120:SNAP=utf8_off 122:SELECT 126:SNAP=utf8_off_info 127:EXIT" ;|
accents|all)       # the same reply with UTF-8 text
    BODY=accents scn accents "120:SNAP=accents 121:EXIT" ;|
refresh|all)       # SQUARE 3 s after the first fetch: the refresh waits for the 10 s mark, then a second request
    scn refresh "200:SQUARE 210:SNAP=refresh_wait 900:SNAP=refresh_done 901:EXIT"
    print -r -- "  requests: $(command grep -c '^GET' $P/build/sbx/refresh$SFX/peer.log)" ;|
net_fail|all)      # netInitialize fails: CIRCLE and SQUARE try again (netCtlInit once), the exit ends netctl only
    scn net_fail "120:SNAP=net_fail 122:SELECT 126:SNAP=net_fail_info 128:CIRCLE 132:SQUARE 136:EXIT" PV_NET_FAIL=1
    ends net_fail ;|
netctl_fail|all)   # netCtlInit fails: the Board runs, the address is not read, the exit has no netCtlTerm
    scn netctl_fail "120:EXIT" PV_NETCTL_FAIL=1
    ends netctl_fail ;|
hang|all)          # START while the fetch waits in recv: the exit shuts the socket down, joins the thread, ends the network
    scn hang "60:START 400:EXIT" PV_BOARD_FAIL=hang
    ends hang ;|
quit_net|all)      # Quit Game during the network start: no Board thread, no fetch, the network ends at the exit
    scn quit_net "400:EXIT" PV_QUIT=net
    ends quit_net ;|
noip|all)          # no address and a refused connect: the Board names the missing address
    scn noip "120:SNAP=noip 121:EXIT" PV_NOIP=1 PV_BOARD_FAIL=refused ;|
nofolder|all)      # the app folder cannot be written: no journal (no counter, no clock prompt), the default URL
    NOFOLDER=1 JOURNAL= scn nofolder "120:SNAP=nofolder 122:SELECT 126:SNAP=nofolder_info 127:EXIT" ;|
auto|all)          # auto.txt 60: no prompt (the round is the consent), the shots at 30 and 45 s, the exit at 60 s
    JOURNAL= AUTO=60 scn auto "1810:SNAP=auto_board 2710:SNAP=auto_info 3300:SNAP=auto_back 4000:EXIT"
    print -r -- "  the sandbox: $(cd $P/build/sbx/auto$SFX/$DIR && print -r -- *(N)); auto.txt now: $(< $P/build/sbx/auto$SFX/$DIR/auto.txt)" ;|
tv|all)            # Lucas's visible area, 0 2 1920 1078 (not in the narrow pass: it sets its own area)
    [[ $AREA != narrow ]] && SAFE='0 2 1920 1078' scn tv "120:SNAP=tv_board 122:SELECT 126:SNAP=tv_info 127:EXIT" ;|
esac
py=${PYTHON:-python3}
$py -I - $OUT <<'PY' 2>/dev/null || print "no Pillow: the snapshots stay .ppm"
import glob, os, sys
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], '*.ppm')):
    Image.open(p).save(p[:-4] + '.png')
PY
