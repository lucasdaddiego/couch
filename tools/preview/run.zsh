#!/bin/zsh
# Preview scenarios: each runs the app in a fresh sandbox with a pad script.
#   zsh tools/preview/run.zsh [scenario]     (default: all; names in the case below)
#   AREA=narrow zsh tools/preview/run.zsh    (every scenario at the narrowest visible area, 1600 x 900,
#                                             snapshots named *_narrow)
#   NJOBS=4 zsh tools/preview/run.zsh        (all: each scenario in its own process, NJOBS at a time,
#                                             by default the cores; the outputs in the case's order)
#   PASSES=2 zsh tools/preview/run.zsh       (twice, the second into an emptied build/out2, and the
#                                             snapshots of the second pass that differ from the
#                                             first's: the Board thread runs in host time)
# Snapshots go to build/out/*.ppm, and *.png when $PYTHON (or python3) has
# Pillow. The sandbox of each scenario stays in build/sbx/<name>: couch.txt,
# journal.txt, peer.log (the requests the peer got), spu.log (the SPU calls
# the twin got, pv_fx.c), the shots of auto.
# The reply files are written here at run time: the sources stay ASCII,
# the degree sign is the bytes $'\xc2\xb0'. The reader's replies (0.3.1:
# /news and /article, live replies of the Worker saved with curl on
# 2026-10-14, the summary and the cut forms made from two of them) are in
# fixtures/*.esc with every byte from 0x80 as \xNN and the backslash as
# \\, decoded into build/fixtures at each run.
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
[[ -n $PV_CHILD ]] || ${PYTHON:-python3} -I - ${P:h:h}/worker/test/board.test.js $REPLY <<'PY' || { print -u2 "the Worker test's reply: not found"; exit 1 }
import re, sys
src = open(sys.argv[1], encoding='utf-8').read()
m = re.search(r"assert\.equal\(t, \[(.*?)\]\.join\('\\n'\)\);", src, re.S)
items = re.findall(r"'((?:[^'\\]|\\.)*)'", m.group(1)) if m else []
if len(items) < 3 or items[0] != 'couch-board 1' or items[-2:] != ['end', '']:
    sys.exit(1)
open(sys.argv[2], 'w', encoding='utf-8', newline='').write('\n'.join(items))
PY
FIX=$P/build/fixtures
mkdir -p $FIX/articles
[[ -n $PV_CHILD ]] || ${PYTHON:-python3} -I - $P/fixtures $FIX <<'PY' || { print -u2 "the reader's fixtures: not decoded"; exit 1 }
import codecs, glob, os, sys
src, out = sys.argv[1], sys.argv[2]
for p in sorted(glob.glob(os.path.join(src, '*.esc'))):
    b = codecs.escape_decode(open(p, encoding='ascii').read().encode('ascii'))[0]
    n = os.path.basename(p)[:-4]
    dst = os.path.join(out, 'articles', n[8:] + '.txt') if n.startswith('article_') else os.path.join(out, n + '.txt')
    open(dst, 'wb').write(b)
PY
body_ok() { print -rn -- "$(< $REPLY)"$'\n'; }
news_none() {    # /news when every feed failed
    print -rn -- "couch-news 1
now=1792022941
news_age=0
news_state=none|clarin.com: HTTP 403
end
"
}
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
body_rain() { each rain_line; }   # the design's rainy day: WMO 61, 88 % cover, 1.2 mm, wind 20 km/h from the south-east
rain_line() { case $1 in wx_fx=*) print -r -- "wx_fx=61|88|1.2|135|20|30";; *) print -r -- "$1";; esac; }
body_snow() { each snow_line; }   # moderate snow, 90 % cover, wind 10 km/h from the west
snow_line() { case $1 in wx_fx=*) print -r -- "wx_fx=73|90|0.4|270|10|15";; *) print -r -- "$1";; esac; }
# The Board page of 0.3.0 (stage 9): the mockups' scenes. The night ones put the place at longitude 165 (and
# offset and PV_TZ at +11 h): the preview's clock is fixed at 12:00 UTC, which is 23:00 there.
body_old() { each old_line; }     # a Worker of 0.2.0's time: the reply without the keys of 0.3.0 (the host check's first reply)
old_line() { [[ $1 == (geo|wx_fx|wx_hours|days_fx|usd_chg|money_chg|spark)=* ]] || print -r -- "$1"; }
body_dayrain() { each dayrain_line; }   # the mockup's rainy day: WMO 61, 88 %, 20 km/h from the SE; a chip of each kind
dayrain_line() {
    case $1 in
    (wx_now=*) print -r -- "wx_now=19$D|17$D|88%|Rain|20 km/h|day|Open-Meteo";;
    (wx_fx=*) print -r -- "wx_fx=61|88|1.2|135|20|34";;
    (days_fx=*) print -r -- "days_fx=61|3|2";;
    (day=Thu*) print -r -- "day=Thu|Partly cloudy|22$D|12$D|18%";;
    (usd_chg=Oficial*) print -r -- "usd_chg=Oficial|up|+0,3%|+5,00|12 Oct";;
    (usd_chg=MEP*) print -r -- "usd_chg=MEP|down|-0,2%|-3,10|12 Oct";;
    (usd_chg=CCL*) print -r -- "usd_chg=CCL|up|+0,1%|+1,60|12 Oct";;
    (usd_chg=Cripto*) print -r -- "usd_chg=Cripto|down|-0,4%|-6,40|12 Oct";;
    (money_chg=*) print -r -- "${1%%|*}|up|+0,2%|+1|9 Oct";;
    (*) print -r -- "$1";;
    esac
}
body_dayrain_before() { each dayrain_before_line; }   # the reply before it: Oficial 5 pesos lower, MEP 2,10 higher
dayrain_before_line() {
    case $1 in
    (usd=Oficial*) print -r -- "usd=Oficial|1.480,00|1.530,00|Fri 15:00";;
    (usd=MEP*) print -r -- "usd=MEP|1.538,00|1.545,00|Fri 18:00";;
    (*) dayrain_line "$1";;
    esac
}
night_line() {   # $2: the wx_now words and wx_fx of the night
    case $1 in
    (offset=*) print -r -- "offset=39600";;
    (geo=*) print -r -- "geo=-34.58|165.0000";;
    (wx_now=*) print -r -- "wx_now=11$D|10$D|71%|$2|6 km/h|night|Open-Meteo";;
    (wx_fx=*) print -r -- "wx_fx=$3";;
    (wx_rain=*) print -r -- "wx_rain=dry|No rain in the next 12 h";;
    (wx_hours=*) print -r -- "wx_hours=0|11 10 10 9 9 8 8 8 8 9 11 13|0 0 0 0 0 0 0 0 0 0 0 0|0 0 0 0 0 0 0 1 1 1 1 1";;
    (days_fx=*) print -r -- "days_fx=61|2|0";;
    (day=Thu*) print -r -- "day=Thu|Clear|22$D|12$D|0%";;
    (*) print -r -- "$1";;
    esac
}
body_night() { local l; for l in "${(@f)$(< $REPLY)}"; do night_line "$l" Clear "0|3|0|200|6|11"; done; }
body_ocnight() { local l; for l in "${(@f)$(< $REPLY)}"; do night_line "$l" Overcast "3|100|0|200|8|15"; done; }
body_storm() { each storm_line; }   # a thunderstorm by day: WMO 95, 100 %, 8.5 mm, 35 km/h from the NW
storm_line() {
    case $1 in
    (wx_now=*) print -r -- "wx_now=18$D|18$D|94%|Thunderstorm|35 km/h|day|Open-Meteo";;
    (wx_fx=*) print -r -- "wx_fx=95|100|8.5|300|35|62";;
    (wx_rain=*) print -r -- "wx_rain=rain|Rain now (90%)";;
    (days_fx=*) print -r -- "days_fx=95|61|3";;
    (*) print -r -- "$1";;
    esac
}
body_snowday() { each snowday_line; }   # snow by day, below 0: the minus sign of the digits
snowday_line() {
    case $1 in
    (wx_now=*) print -r -- "wx_now=-1$D|-6$D|90%|Snow|10 km/h|day|Open-Meteo";;
    (wx_fx=*) print -r -- "wx_fx=73|90|0.4|270|10|15";;
    (wx_rain=*) print -r -- "wx_rain=rain|Snow now (80%)";;
    (day=Today*) print -r -- "day=Today|Snow|1$D|-3$D|80%";;
    (days_fx=*) print -r -- "days_fx=73|71|3";;
    (wx_hours=*) print -r -- "wx_hours=15|-1 -1 -2 -2 -3 -3 -3 -2 -1 0 1 1|80 80 70 60 50 40 30 20 10 10 5 5|73 73 73 71 71 3 3 3 3 2 2 1";;
    (*) print -r -- "$1";;
    esac
}
body_newsmix() { NEWS_I=0; each newsmix_line; }   # the rainy day with mixed sources: Ambito (economy) and BBC (world) too
newsmix_line() {
    case $1 in
    (news=*)
        (( NEWS_I++ ))
        case $NEWS_I in
        (2) print -r -- "news="$'\xc3\x81'"mbito|540|El d"$'\xc3\xb3'"lar blue cerr"$'\xc3\xb3'" estable y el riesgo pa"$'\xc3\xad'"s baj"$'\xc3\xb3'" a 589 puntos";;
        (3) print -r -- "news="$'\xc3\x81'"mbito|1980|Inflaci"$'\xc3\xb3'"n: las consultoras esperan un dato por debajo del 2% para octubre";;
        (6) print -r -- "news=BBC|3600|Storm warnings issued across the Atlantic coast as a deep low moves in";;
        (*) dayrain_line "$1";;
        esac;;
    (*) dayrain_line "$1";;
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

scn() {   # name script [VAR=value ...]; JOURNAL, BODY, NEWS, URL, AUTO, BIN, SAFE from the caller
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
    local b2=()
    [[ -n $BODY2 ]] && { body_$BODY2 > $sbx/body2.txt; b2=(PV_BOARD2=$sbx/body2.txt) }   # the second and later replies
    local rd=(PV_ARTICLES=$FIX/articles)                 # the reader: /news from the fixture (NEWS=none, 404), the articles
    case ${NEWS:-list} in
    (list) rd+=(PV_NEWS=$FIX/news.txt);;
    (404) ;;
    (*) news_$NEWS > $sbx/news.txt; rd+=(PV_NEWS=$sbx/news.txt);;
    esac
    env PV_ROOT=$sbx PV_OUT=$OUT PV_BOARD=$sbx/body.txt $b2 $rd PV_SCRIPT="${(j: :)s}" "$@" $P/build/${BIN:-pv} 2>&1 | command grep -v '^snap' | head -5
    print -- "$name: exit ${pipestatus[1]}"
}

fxends() {   # name: the effects' lines in couch.txt, the fx_ lines of the journal, the SPU calls the twin got
    local d=$P/build/sbx/$1$SFX
    local lines=$(command grep -h -e '^effects' -e '^board thread' -e '^net_end' $d/$DIR/couch.txt 2>/dev/null)
    local j=$(command grep -h ' fx_' $d/$DIR/journal.txt 2>/dev/null)
    local calls=$(command grep -h '^spu: ' $d/spu.log 2>/dev/null | cut -c6-)
    print -r -- "  couch.txt: ${(j:; :)${(f)lines}}"
    print -r -- "  journal: ${(j:; :)${(f)j}}"
    print -r -- "  spu calls: ${${calls:+${(j:; :)${(f)calls}}}:-none}"
}

ends() {   # name: the start's and the exit's lines in couch.txt, and the netctl and exit calls the peer saw (its "pv: " lines)
    local d=$P/build/sbx/$1$SFX
    local lines=$(command grep -h -e '^quit ' -e '^net_start' -e '^board thread' -e '^net_end' -e '^exit at' $d/$DIR/couch.txt 2>/dev/null)
    local calls=$(command grep -h '^pv: ' $d/peer.log 2>/dev/null | cut -c5-)
    print -r -- "  couch.txt: ${(j:; :)${(f)lines}}"
    print -r -- "  net calls: ${(j:; :)${(f)calls}}"
}

# all: one process per scenario (PV_CHILD: the replies and fixtures above are the parent's), NJOBS at a time
if [[ ${1:-all} == all && -z $PV_CHILD ]]; then
    names=(${(f)"$(command grep -oE '^[a-z0-9_]+[|]all[)]' $0 | cut -d'|' -f1)"})
    order=($names) names=(${(M)names:#*ambient*} ${names:#*ambient*})   # the long ones (8,000 to 11,000 frames) first: the wall time is the longest
    logs=$P/build/logs$SFX
    rm -rf $logs && mkdir -p $logs
    print -l -- $names | xargs -P ${NJOBS:-$(sysctl -n hw.ncpu)} -I{} \
        env PV_CHILD=1 OUT=$OUT zsh -c 'zsh "$0" "$1" > "$2/$1.log" 2>&1' ${0:A} {} $logs
    for n in $order; do cat $logs/$n.log; done
    set -- none                              # the case below: no branch
fi
case ${1:-all} in
board_ok|all)      # the reply of the Worker test: the clock, both cards; the Info screen; back
    scn board_ok "120:SNAP=board_ok 122:SELECT 150:SNAP=info 152:CIRCLE 180:SNAP=board_back 181:EXIT"
    print -r -- "  the request: $(command grep -m1 '^GET' $P/build/sbx/board_ok$SFX/peer.log | tr -d '\r')"
    ends board_ok ;|
board_stale|all)   # the dollar upstream failed, the Worker kept its old rates: the card's foot in YELLOW
    BODY=stale scn board_stale "120:SNAP=board_stale 121:EXIT" ;|
board_none|all)    # both upstreams failed with no old data
    BODY=none scn board_none "120:SNAP=board_none 121:EXIT" ;|
pages|all)         # L1 and R1 cycle the Board and the News (0.3.1): R1 to the News, R1 wraps to the Board, L1 to the News;
                   # SELECT opens the Info page, R1 there does nothing, CIRCLE goes back to the Board
    scn pages "120:SNAP=pages_board 122:R1 128:SNAP=pages_slide 150:SNAP=pages_news 152:R1 180:SNAP=pages_back 182:L1 210:SNAP=pages_l1 212:SELECT 240:SNAP=pages_info 242:R1 270:SNAP=pages_info_r1 272:CIRCLE 300:SNAP=pages_circle 301:EXIT" ;|
board_extras|all)  # the money extras none and stale, a dry forecast, the feeds failed: the News page says why
    NEWS=none BODY=extras scn board_extras "120:SNAP=board_extras 122:R1 150:SNAP=news_none 151:EXIT" ;|
board_v1|all)      # a Worker of 0.1.0's time: no money, no sky, no news lines, no /news (the new Couch draws what came)
    NEWS=404 BODY=v1 scn board_v1 "120:SNAP=board_v1 122:R1 150:SNAP=news_v1 151:EXIT" ;|
net_slow|all)      # the answer takes 4 s: the header says the internet is slow
    scn net_slow "400:SNAP=net_slow 402:SELECT 430:SNAP=net_slow_info 431:EXIT" PV_BOARD_FAIL=slow ;|
net_down|all)      # a good reply, then SQUARE after 10 s and a refused connect: no internet since 09:00
    scn net_down "120:SNAP=net_down_first 700:SQUARE 820:SNAP=net_down 822:SELECT 850:SNAP=net_down_info 851:EXIT" PV_BOARD_FAIL=ok,refused ;|
ambient|all)       # 10 min with no button (1 min in the preview's build): the aurora with the clock, the date and the weather's line (0.3.0); a
                   # minute on, the block glided to its next place with Blue's price; then the newest headline;
                   # START wakes the screen (the Board) and does not exit; the fetches go on (every 5 min: 3 min of frames, one)
    scn ambient "120:SNAP=ambient_before 3720:SNAP=ambient 7320:SNAP=ambient_news 10920:SNAP=ambient_board2 10940:START 10960:SNAP=ambient_awake 10970:EXIT" PV_MAXFRAMES=11000
    print -r -- "  requests: $(command grep -c '^GET' $P/build/sbx/ambient$SFX/peer.log); couch.txt: $(command grep -h '^ambient' $P/build/sbx/ambient$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
board_smn|all)     # Junin, the now line from SMN: the weather card's foot names both sources; the same folded to ASCII
    BODY=smn scn board_smn "120:SNAP=board_smn 121:EXIT"
    BIN=pv_utf8_off BODY=smn scn board_smn_utf8_off "120:SNAP=board_smn_utf8_off 121:EXIT" ;|
board_301|all)     # the host redirects plain HTTP (Always Use HTTPS back on)
    scn board_301 "120:SNAP=board_301 122:SELECT 150:SNAP=board_301_info 151:EXIT" PV_BOARD_FAIL=301 ;|
board_stale301|all) # a good reply, then SQUARE after 10 s and a 301: the stale line keeps the data's time, the Info screen the Location
    scn board_stale301 "120:SNAP=board_stale301_first 700:SQUARE 820:SNAP=board_stale301 822:SELECT 850:SNAP=board_stale301_info 851:EXIT" PV_BOARD_FAIL=ok,301 ;|
board_cut|all)     # a good reply, then SQUARE after 10 s and a cut one: the old Board stays
    scn board_cut "120:SNAP=board_cut_first 700:SQUARE 820:SNAP=board_cut 822:SELECT 850:SNAP=board_cut_info 851:EXIT" PV_BOARD_FAIL=ok,cut ;|
board_v2|all)      # a reply of format version 2
    scn board_v2 "120:SNAP=board_v2 121:EXIT" PV_BOARD_FAIL=v2 ;|
board_silent|all)  # the peer accepts and never answers (SO_RCVTIMEO)
    scn board_silent "120:SNAP=board_silent 121:EXIT" PV_BOARD_FAIL=silent ;|
board_refused|all) # nothing listens on the port
    scn board_refused "120:SNAP=board_refused 121:EXIT" PV_BOARD_FAIL=refused ;|
board_dns|all)     # no board_url.txt: the default host, and the preview has no DNS
    URL=none scn board_dns "120:SNAP=board_dns 121:EXIT" ;|
frozen|all)        # the network start froze before: the Board says so; SELECT, SQUARE twice, CIRCLE: the start runs and the Board fills
    JOURNAL=$'start net_start\n'$TZ_DONE scn frozen "100:SNAP=frozen 102:SELECT 130:SNAP=frozen_info 132:SQUARE 136:SQUARE 140:SNAP=frozen_cleared 142:CIRCLE 260:SNAP=frozen_after 261:EXIT" ;|
tz_skip|all)       # a first start: the clock's prompt, CIRCLE skips it, the Board's offset sets the clock
    JOURNAL= scn tz_skip "30:SNAP=prompt 32:CIRCLE 150:SNAP=tz_skip 152:SELECT 180:SNAP=tz_skip_info 181:EXIT" ;|
tz_12h|all)        # CROSS at the prompt; TIME_FORMAT 0 (a 12-hour clock), UTC+5:30
    JOURNAL= scn tz_12h "32:CROSS 150:SNAP=tz_12h 152:SELECT 180:SNAP=tz_12h_info 181:EXIT" PV_TIMEFMT=0 PV_TZ=330 ;|
utf8_off|all)      # text_utf8 failed (the preview's second build): the fold to ASCII
    BIN=pv_utf8_off BODY=accents scn utf8_off "120:SNAP=utf8_off 122:SELECT 150:SNAP=utf8_off_info 151:EXIT" ;|
accents|all)       # the same reply with UTF-8 text
    BODY=accents scn accents "120:SNAP=accents 121:EXIT" ;|
refresh|all)       # SQUARE 3 s after the first fetch: the refresh waits for the 10 s mark, then a second request
    scn refresh "200:SQUARE 210:SNAP=refresh_wait 900:SNAP=refresh_done 901:EXIT"
    print -r -- "  requests: $(command grep -c '^GET' $P/build/sbx/refresh$SFX/peer.log)" ;|
net_fail|all)      # netInitialize fails: CIRCLE and SQUARE try again (netCtlInit once), the exit ends netctl only
    scn net_fail "120:SNAP=net_fail 122:SELECT 150:SNAP=net_fail_info 152:CIRCLE 180:SQUARE 184:EXIT" PV_NET_FAIL=1
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
    NOFOLDER=1 JOURNAL= scn nofolder "120:SNAP=nofolder 122:SELECT 150:SNAP=nofolder_info 151:EXIT" ;|
auto|all)          # auto.txt 60: no prompt (the round is the consent), the shots at 30 and 45 s, the exit at 60 s
    JOURNAL= AUTO=60 scn auto "1810:SNAP=auto_board 2710:SNAP=auto_info 3300:SNAP=auto_back 4000:EXIT"
    print -r -- "  the sandbox: $(cd $P/build/sbx/auto$SFX/$DIR && print -r -- *(N)); auto.txt now: $(< $P/build/sbx/auto$SFX/$DIR/auto.txt)" ;|
fx_rain|all)       # the effects on a rainy day: the sky, the clouds and the slanted rain behind the cards; the Info page's
                   # Effects row; the exit: the six exit reports, the join, the exit statuses, the destroy, the close
    BODY=rain scn fx_rain "120:SNAP=fx_rain 122:SELECT 150:SNAP=fx_rain_info 151:EXIT"
    fxends fx_rain ;|
fx_snow|all)       # snow: 300 flakes with the round texture
    BODY=snow scn fx_snow "120:SNAP=fx_snow 121:EXIT" ;|
fx_frozen|all)     # fx_first froze the console before: no SPU call at all, RSX only (four cloud sprites, the PPU's 300 drops);
                   # SELECT, SQUARE twice, CIRCLE: the journal is clear, the Board's opening starts the SPUs
    JOURNAL=$'start fx_first\n'$TZ_DONE BODY=rain scn fx_frozen "120:SNAP=fx_frozen 122:SELECT 150:SNAP=fx_frozen_info 152:SQUARE 154:SQUARE 158:SNAP=fx_frozen_cleared 160:CIRCLE 320:SNAP=fx_frozen_after 321:EXIT"
    fxends fx_frozen ;|
fx_late|all)       # SPU 5 never reports its 30th rain job (inside fx_first): the 50 ms deadline, the terminate and the join,
                   # fx_first closed as failed, RSX only for the session
    BODY=rain scn fx_late "120:SNAP=fx_late 122:SELECT 150:SNAP=fx_late_info 151:EXIT" PV_FX_LATE=rain PV_FX_LATE_AT=30
    fxends fx_late ;|
fx_late_sky|all)   # the sky's 10th picture never comes: RSX only 2 s after it was sent
    scn fx_late_sky "200:SNAP=fx_late_sky 201:EXIT" PV_FX_LATE=sky PV_FX_LATE_AT=10
    fxends fx_late_sky ;|
fx_kat|all)        # SPU 4's known-answer test is wrong: the group stops before its first job
    scn fx_kat "120:SNAP=fx_kat 121:EXIT" PV_FX_KAT=bad
    fxends fx_kat ;|
fx_fail|all)       # sysSpuThreadGroupStart fails: the destroy, the close, RSX only
    scn fx_fail "120:EXIT" PV_SPU_FAIL=173
    fxends fx_fail ;|
fx_exit|all)       # START in the first half second, the SPUs up and the fetch in recv: the Board thread ends first, then the
                   # SPUs (fx_first closed at the exit), then the network
    BODY=rain scn fx_exit "30:START 400:EXIT" PV_BOARD_FAIL=hang
    fxends fx_exit ;|
board_rain|all)    # the Board of 0.3.0 on the mockup's rainy day; SQUARE at 11.7 s brings Oficial 5 pesos up and MEP 2,10
                   # down: they counted and pulsed, and keep a low glow (UP amber, DOWN teal) until the next reply
    BODY=dayrain_before BODY2=dayrain scn board_rain "120:SNAP=board_rain_first 700:SQUARE 1150:SNAP=board_rain 1151:EXIT" ;|
board_rain_tv|all) # the same at Lucas's area (not in the narrow pass)
    [[ $AREA != narrow ]] && SAFE='0 2 1920 1078' BODY=dayrain_before BODY2=dayrain scn board_rain_tv "700:SQUARE 1150:SNAP=board_rain_tv 1151:EXIT" ;|
board_night|all)   # a clear night (23:00): stars, the moon's sphere, the dark arc with the moon on it, no rain
    BODY=night scn board_night "120:SNAP=board_night 121:EXIT" PV_TZ=660 ;|
board_ocnight|all) # an overcast night: the cover's grey scaled by the light, no brighter than the clear night
    BODY=ocnight scn board_ocnight "120:SNAP=board_ocnight 121:EXIT" PV_TZ=660 ;|
board_storm|all)   # a thunderstorm (WMO 95): heavy slanted rain; frame 360 is the first bolt (6 s) with its flash
    BODY=storm scn board_storm "120:SNAP=board_storm_rain 360:SNAP=board_storm 361:EXIT" ;|
board_snow|all)    # snow by day at -1 deg: the flakes, the hero's snowflakes, the minus sign
    BODY=snowday scn board_snow "120:SNAP=board_snow 121:EXIT" ;|
board_old|all)     # a Worker of 0.2.0's time (no geo, wx_fx, wx_hours, days_fx, chips, spark): each value's "-" form,
                   # the sky from wx_now's day or night
    BODY=old scn board_old "120:SNAP=board_old 121:EXIT" ;|
board_fxoff|all)   # the effects off (fx_first froze before: RSX only): solid panels, the sprites, the PPU's rain; the glow too
    JOURNAL=$'start fx_first\n'$TZ_DONE BODY=dayrain_before BODY2=dayrain scn board_fxoff "120:SNAP=board_fxoff_first 700:SQUARE 1150:SNAP=board_fxoff 1151:EXIT" ;|
tv|all)            # Lucas's visible area, 0 2 1920 1078 (not in the narrow pass: it sets its own area)
    [[ $AREA != narrow ]] && SAFE='0 2 1920 1078' scn tv "120:SNAP=tv_board 122:R1 150:SNAP=tv_news 152:SELECT 180:SNAP=tv_info 181:EXIT" ;|
ambient_rsx|all)   # the ambient mode in RSX-only mode (fx_first froze before): 0.2.0's dimmed page, 6 px moves, the page each minute
    JOURNAL=$'start fx_first\n'$TZ_DONE scn ambient_rsx "3720:SNAP=ambient_rsx 7320:SNAP=ambient_rsx_news 7330:EXIT" PV_MAXFRAMES=8000 ;|
start|all)         # the start (0.3.0): the loading screen from the first frame after ui_init (frame 3: the icons'
                   # step), during the effects' job (its counter), the start animation half way, then the Board;
                   # couch.txt's start times and boot.txt's marks
    scn start "3:SNAP=loading 7:SNAP=loading_fx 30:SNAP=start_mid 120:SNAP=start_end 121:EXIT"
    print -r -- "  couch.txt: $(command grep -h '^start' $P/build/sbx/start$SFX/$DIR/couch.txt | tr '\n' ';')"
    print -r -- "  boot.txt: $(tr '\n' ' ' < $P/build/sbx/start$SFX/$DIR/boot.txt)" ;|
skeleton|all)      # before the first reply (a 4 s answer): the skeleton's bars on the Board and on the News page
    scn skeleton "120:SNAP=skeleton 122:R1 150:SNAP=skeleton_news 151:EXIT" PV_BOARD_FAIL=slow ;|
news_rain|all)     # the News page on the rainy day of the mockups (the sources' pills, the categories, the ages): since 0.3.1 the
                   # list of /news (the fixture)
    BODY=newsmix scn news_rain "120:R1 150:SNAP=news_rain 151:EXIT" ;|
reader|all)        # the reader (0.3.1): R1, the News page with /news's 30 headlines, the featured card selected; DOWN four times and
                   # RIGHT: the list scrolls (3 frames after the last press, then at rest); SQUARE 10 s later asks the list again
    scn reader "120:R1 160:SNAP=news_list 162:DOWN 168:DOWN 174:DOWN 180:DOWN 186:RIGHT 189:SNAP=news_scrolling 240:SNAP=news_scrolled 900:SQUARE 960:SNAP=news_refresh 961:EXIT"
    print -r -- "  requests: $(command grep -c '^GET /news' $P/build/sbx/reader$SFX/peer.log) of /news, $(command grep -c '^GET /board' $P/build/sbx/reader$SFX/peer.log) of /board; couch.txt: $(command grep -h -e '^news' -e '^stext' $P/build/sbx/reader$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
reader_article|all) # CROSS on the second headline (Ambito): the loading (a 4 s answer: the skeleton), the article with its QR code; DOWN,
                   # then DOWN held a second: it glides; CIRCLE: the list, its selection kept
    scn reader_article "120:R1 160:DOWN 166:CROSS 200:SNAP=article_loading 460:SNAP=article 462:DOWN 470-540:DOWN 600:SNAP=article_scrolled 602:CIRCLE 640:SNAP=article_back 641:EXIT" PV_ARTICLE_FAIL=slow
    print -r -- "  requests: $(command grep -h '^GET /article' $P/build/sbx/reader_article$SFX/peer.log | tr -d '\r' | tr '\n' ';') couch.txt: $(command grep -h '^article' $P/build/sbx/reader_article$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
reader_states|all) # the article's other forms, each one line: the feed's summary (Clarin refused the page), a text cut at 12 KB, an
                   # id the Worker does not know, no answer (SO_RCVTIMEO); CIRCLE back each time; then BBC's article
    scn reader_states "120:R1 160:DOWN 164:DOWN 168:CROSS 210:SNAP=article_summary 212:CIRCLE 220:RIGHT 224:CROSS 270:SNAP=article_cut 272:CIRCLE 280:DOWN 284:LEFT 288:CROSS 330:SNAP=article_none 332:CIRCLE 340:RIGHT 344:CROSS 390:SNAP=article_error 392:CIRCLE 400:SNAP=article_error_back 402:CROSS 450:SNAP=article_bbc 451:EXIT" PV_ARTICLE_FAIL=ok,ok,ok,silent,ok
    print -r -- "  couch.txt: $(command grep -h '^article' $P/build/sbx/reader_states$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
reader_first|all)  # the list's first 4 s (a slow /news): the Board's 8 headlines fill the page; CROSS says they open once the list is in;
                   # then the list
    scn reader_first "120:R1 160:SNAP=news_first 162:CROSS 170:SNAP=news_first_cross 440:SNAP=news_first_full 441:EXIT" PV_NEWS_FAIL=slow ;|
reader_ambient|all) # an open article holds the ambient mode off for 20 min (10 without one; the preview's build: 2 and 1): at 92 s
                   # still the article, at 182 s the aurora; no list request while the article is open (not in the narrow pass)
    [[ $AREA != narrow ]] && scn reader_ambient "120:R1 160:DOWN 166:CROSS 5520:SNAP=reader_noambient 10920:SNAP=reader_ambient 10921:EXIT" PV_MAXFRAMES=11000
    [[ $AREA != narrow ]] && print -r -- "  requests: $(command grep -c '^GET /news' $P/build/sbx/reader_ambient$SFX/peer.log) of /news, $(command grep -c '^GET /board' $P/build/sbx/reader_ambient$SFX/peer.log) of /board; couch.txt: $(command grep -h '^ambient' $P/build/sbx/reader_ambient$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
esac
[[ -n $PV_CHILD ]] && exit 0                 # the PNGs: the parent's, once
py=${PYTHON:-python3}
$py -I - $OUT <<'PY' 2>/dev/null || print "no Pillow: the snapshots stay .ppm"
import glob, os, sys
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], '*.ppm')):
    Image.open(p).save(p[:-4] + '.png')
PY
