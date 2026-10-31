#!/bin/zsh
# Preview scenarios: each runs the app in a fresh sandbox with a pad script.
#   zsh tools/preview/run.zsh [scenario]     (default: all; names in the case below)
#   AREA=narrow zsh tools/preview/run.zsh    (every scenario at the narrowest visible area, 1600 x 900,
#                                             snapshots named *_narrow)
#   NJOBS=4 zsh tools/preview/run.zsh        (all: each scenario in its own process, NJOBS at a time,
#                                             by default the cores; the outputs in the case's order)
#   PASSES=2 zsh tools/preview/run.zsh       (twice, the second into an emptied build/out2, and the
#                                             snapshots of the second pass that differ from the
#                                             first's by more than 8 levels: the Board thread runs in host time)
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
    # a pixel more than 8 levels apart counts (the sky's blur and the glass vary by 1 or 2 with the
    # host's timing); the box of those pixels says where; without Pillow, any byte
    ${PYTHON:-python3} -I - $P/build/out2 $OUT <<'PY' 2>/dev/null && exit 0
import glob, os, sys
from PIL import Image, ImageChops
for p in sorted(glob.glob(os.path.join(sys.argv[1], '*.ppm'))):
    q = os.path.join(sys.argv[2], os.path.basename(p))
    a, b = Image.open(p), Image.open(q) if os.path.exists(q) else None
    box = (ImageChops.difference(a, b).convert('L').point(lambda v: 255 if v > 8 else 0).getbbox() if b and a.size == b.size else (0, 0) + a.size)
    if box: print(f"differs between the passes: {os.path.basename(p)[:-4]} in {box}")
PY
    for f in $P/build/out2/*.ppm(N); do cmp -s $f $OUT/${f:t} || print "differs between the passes: ${f:t:r}"; done
    exit 0
fi
mkdir -p $OUT
SFX= AREA_DEF='67 38 1853 1042'                          # the default visible area (93 %)
[[ $AREA == narrow ]] && { SFX=_narrow; AREA_DEF='160 90 1760 990' }
D=$'\xc2\xb0' DASH=$'\xe2\x80\x93' NL=$'\n'           # the degree sign, an en dash (U+2013), a line end
TZ_DONE=$'done tz_params rc=0x00000000 ok=1\ndone fan_policy rc=0x00000000 ok=1\n'   # the clock settings and the fan read before: no prompt
TZ_ONLY=$'done tz_params rc=0x00000000 ok=1\n'        # the clock settings only: the Info page's fan prompt (0.5.0)
URL_LAN=http://192.168.0.3:8787/board                 # a dotted address: the fake peer needs no DNS

# The Worker test's first reply (worker/test/board.test.js: Berlin on 2026-10-16, every
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
FSRC=$P/fixtures                         # the reader's replies: made-up text (fixtures/README.md)
[[ -n $PV_CHILD ]] || ${PYTHON:-python3} -I - $FSRC $FIX <<'PY' || { print -u2 "the reader's fixtures: not decoded"; exit 1 }
import codecs, glob, os, sys
src, out = sys.argv[1], sys.argv[2]
for p in sorted(glob.glob(os.path.join(src, '*.esc'))):
    b = codecs.escape_decode(open(p, encoding='ascii').read().encode('ascii'))[0]
    n = os.path.basename(p)[:-4]
    dst = os.path.join(out, 'articles', n[8:] + '.txt') if n.startswith('article_') else os.path.join(out, n + '.txt')
    open(dst, 'wb').write(b)
PY
body_ok() { print -rn -- "$(< $REPLY)"$'\n'; }
# 0.5.0: the rain of the next 2 h, the air and the futbol line (the private Worker's key; made-up teams)
body_soon() { each soon_line; }
soon_line() {
    case $1 in
    (wx_rain=*) print -r -- "wx_rain=rain|Rain from 19:00 (60%)"; print -r -- "wx_soon=Rain from 18:45";;
    (air=*|futbol=*) ;;
    (wx_state=*) print -r -- "$1"; print -r -- "air=42|3"; print -r -- "futbol=2|2 live";;
    (*) print -r -- "$1";;
    esac
}
body_soon2() { each soon2_line; }   # raining now until 19:30, the air poor and the UV high, the next game's words
soon2_line() {
    case $1 in
    (wx_fx=*) print -r -- "wx_fx=61|88|1.2|135|20|30";;
    (wx_rain=*) print -r -- "wx_rain=rain|Rain now (80%)"; print -r -- "wx_soon=Rain until 19:30";;
    (air=*|futbol=*) ;;
    (wx_state=*) print -r -- "$1"; print -r -- "air=163|9"; print -r -- "futbol=0|Next: Northbridge${DASH}Valley Athletic 21:00";;
    (*) print -r -- "$1";;
    esac
}
# The Futbol page's replies (0.5.0, GET /futbol?d=0 and d=1): made-up leagues and teams.
futbol_today() {
    print -rn -- "couch-futbol 1
now=1792164000
day=Today, Fri 16 Oct
league=Northern League|Freedonia
game=67'|Riverside United|Harbour City|2|1|Sport 1|live
game=HT|Old Town Rovers|Lakeside|0|0|Sport 2, Stream Plus|live
game=Final|Hillcrest|Ironworks FC|1|3|-|end
game=21:00|Northbridge|Valley Athletic|-|-|Sport 1|pre
game=23:15|Coast Wanderers|Millbrook Town|-|-|-|pre
league=Second Division|Freedonia
game=Final|Pine Grove|Eastfield|2|2|-|end
game=Final|Saint Clement Athletic Club|Port Esperanza Sporting|0|1|Sport 3|end
game=19:30|Westport|Granite Rock|-|-|Sport 3|pre
game=20:00|Cedar Falls|Brookhaven|-|-|-|pre
league=Continental Cup|International
game=Postponed|Atlas Club|Sunrise FC|-|-|-|off
game=Pens 4-3|Blue Harbour|Red Mill|1|1|Stream Plus|end
game=88'|Golden Coast|Silver Lake|3|2|Sport 1, Sport 1 HD, Stream Plus, Radio Freedonia|live
league=Women's League|Freedonia
game=Final|Lakeside Women|Harbour City Women|4|0|Sport 2|end
game=18:00|Riverside Ladies|Ironworks Ladies|-|-|Sport 2|pre
league=Reserve League|Sylvania
game=Final|North Point|South Point|1|0|-|end
game=Final|East Gate|West Gate|2|3|-|end
game=17:00|Old Mill|New Mill|-|-|-|pre
game=Cancelled|High Moor|Low Moor|-|-|-|off
more=2|11
futbol_age=30
futbol_state=ok
end
"
}
econ_ok() {      # 0.6.0: the Economía page's reply, the sources' readings of 2026-10-29 (the catalogue of the Worker's economia.js)
    print -rn -- "couch-economia 1
now=1793297934
ind=inflacion|Inflación mensual|Precios|%|1.9|2026-09-30|1.7|2026-08-31|BCRA
ind=inflacion_interanual|Inflación interanual|Precios|%|33.4|2026-09-30|33.5|2026-08-31|BCRA
ind=rem|Inflación esperada 12 meses (REM)|Precios|%|21.1|2026-09-30|21|2026-08-31|BCRA
ind=uva|UVA|Precios|ARS|2174.89|2026-10-29|2173.56|2026-10-28|BCRA
ind=cer|CER|Precios|index|861.75|2026-10-29|861.2|2026-10-28|BCRA
ind=icl|ICL (alquileres)|Precios|index|37.15|2026-10-29|37.13|2026-10-28|BCRA
ind=linea_pobreza|Línea de pobreza (INDEC)|Precios|ARS|1605497.3487|2026-08-01|1564715.8995|2026-07-01|INDEC
ind=mayorista|Dólar mayorista (A3500)|Dólar|ARS|1538.4739|2026-10-28|1538.741|2026-10-27|BCRA
ind=minorista|Dólar minorista (BCRA)|Dólar|ARS|1560.12|2026-10-28|1560|2026-10-27|BCRA
ind=blue|Dólar blue|Dólar|ARS|1565|2026-10-29|-|-|DolarAPI
ind=brecha|Brecha blue / oficial|Dólar|%|0.3205|2026-10-29|-|-|DolarAPI
ind=badlar|BADLAR bancos privados|Tasas y BCRA|%|23.5|2026-10-27|22.1875|2026-10-26|BCRA
ind=tamar|TAMAR bancos privados|Tasas y BCRA|%|24.5625|2026-10-27|24.125|2026-10-26|BCRA
ind=plazo_fijo|Plazo fijo 30 días|Tasas y BCRA|%|22.6|2026-10-27|20.92|2026-10-26|BCRA
ind=reservas|Reservas internacionales|Tasas y BCRA|USDM|50482|2026-10-26|50208|2026-10-23|BCRA
ind=base_monetaria|Base monetaria|Tasas y BCRA|ARSM|46804518|2026-10-26|46813000|2026-10-23|BCRA
ind=riesgo_pais|Riesgo país|Tasas y BCRA|pts|763|2026-10-28|-|-|ArgentinaDatos
ind=emae|EMAE desestacionalizado|Actividad|index|149.6479|2026-07-01|154.1173|2026-06-01|INDEC
ind=desocupacion|Desocupación (EPH, trimestral)|Actividad|%|7.9|2026-04-01|7.8|2026-01-01|INDEC
hist=inflacion|2.4,2.7,2.4,2.2,2,2.4,2.9,2.5,2.1,1.9,2.1,1.7,1.9
econ_age=7
econ_state=ok
end
"
}
econ_stale() {   # the entry old, a source failed, two lines missing
    econ_ok | sed -e '/^ind=tamar/d' -e '/^ind=emae/d' -e 's/^econ_age=7$/econ_age=9000/' -e 's/^econ_state=ok$/econ_state=stale|api.bcra.gob.ar: no answer in 4 s/' |
        awk '/^econ_age=/ { print "econ_missing=44,143.3_NO_PR_2004_A_31" } { print }'
}
econ_none() {    # every source failed: no indicator
    print -rn -- "couch-economia 1
now=1793297934
econ_age=0
econ_state=none|api.bcra.gob.ar: HTTP 503
end
"
}
futbol_tomorrow() {
    print -rn -- "couch-futbol 1
now=1792164000
day=Tomorrow, Sat 17 Oct
league=Northern League|Freedonia
game=15:00|Harbour City|Hillcrest|-|-|Sport 1|pre
game=17:30|Lakeside|Riverside United|-|-|Sport 2|pre
game=20:00|Ironworks FC|Old Town Rovers|-|-|Sport 1, Stream Plus|pre
league=Second Division|Freedonia
game=16:00|Eastfield|Westport|-|-|-|pre
futbol_age=0
futbol_state=ok
end
"
}
futbol_empty() {
    print -rn -- "couch-futbol 1
now=1792164000
day=Today, Fri 16 Oct
futbol_age=0
futbol_state=ok
end
"
}
futbol_none() {
    print -rn -- "couch-futbol 1
now=1792164000
day=Today, Fri 16 Oct
futbol_age=0
futbol_state=none|the source: HTTP 503
end
"
}
news_none() {    # /news when every feed failed
    print -rn -- "couch-news 1
now=1792164000
news_age=0
news_state=none|feeds.bbci.co.uk: HTTP 403
end
"
}
each() {   # the reply with each line through the function $1 (it prints the line, another, or nothing)
    local l
    for l in "${(@f)$(< $REPLY)}"; do $1 "$l"; done
}
body_stale() { each stale_line; }
stale_line() { case $1 in rate_age=*) print -r -- "rate_age=90000";; rate_state=*) print -r -- "rate_state=stale|frankfurter.dev: HTTP 503";; *) print -r -- "$1";; esac; }
body_none() {
    print -rn -- "couch-board 1
now=1792164000
offset=7200
city=Berlin
rate_age=0
rate_state=none|frankfurter.dev: HTTP 503
wx_age=0
wx_state=none|open-meteo: no answer in 4 s
news_age=0
news_state=none|feeds.bbci.co.uk: HTTP 403
end
"
}
body_accents() { each accents_line; }   # a city with an n tilde (U+00F1) and a reason with an en dash, for the fold
accents_line() {
    case $1 in
    (city=*) print -r -- "city=A Coru"$'\xc3\xb1'"a, Galicia";;
    (rate_age=*) print -r -- "rate_age=90000";;
    (rate_state=*) print -r -- "rate_state=stale|frankfurter.dev: HTTP 503 $DASH maintenance";;
    (*) print -r -- "$1";;
    esac
}
body_nonews() { each nonews_line; }   # no rain, no news (the feeds failed)
nonews_line() {
    case $1 in
    (wx_rain=*) print -r -- "wx_rain=dry|No rain in the next 12 h";;
    (news=*) ;;
    (news_age=*) print -r -- "news_age=0";;
    (news_state=*) print -r -- "news_state=none|feeds.bbci.co.uk: HTTP 403";;
    (*) print -r -- "$1";;
    esac
}
body_south() { each south_line; }   # Berlin's latitude mirrored south of the equator (no real place)
south_line() { case $1 in geo=*) print -r -- "geo=-52.52|13.4";; *) print -r -- "$1";; esac; }
body_rain() { each rain_line; }   # a rainy day: WMO 61, 88 % cover, 1.2 mm, wind 20 km/h from the south-east
rain_line() { case $1 in wx_fx=*) print -r -- "wx_fx=61|88|1.2|135|20|30";; *) print -r -- "$1";; esac; }
body_snow() { each snow_line; }   # moderate snow, 90 % cover, wind 10 km/h from the west
snow_line() { case $1 in wx_fx=*) print -r -- "wx_fx=73|90|0.4|270|10|15";; *) print -r -- "$1";; esac; }
# The night scenes put the place at longitude 165 (and offset and PV_TZ at +11 h): the preview's
# clock is fixed at 12:00 UTC, which is 23:00 there.
body_old() { each old_line; }     # a minimal Worker: the reply without the derived keys (the host check's first reply)
old_line() { [[ $1 == (geo|wx_fx|wx_hours|days_fx|rate_chg|spark|feature|numfmt|wx_soon|air|futbol)=* ]] || print -r -- "$1"; }
body_dayrain() { each dayrain_line; }   # a rainy day: WMO 61, 88 %, 20 km/h from the SE; a chip of each kind
dayrain_line() {
    case $1 in
    (wx_now=*) print -r -- "wx_now=19$D|17$D|88%|Rain|20 km/h|day";;
    (wx_fx=*) print -r -- "wx_fx=61|88|1.2|135|20|34";;
    (days_fx=*) print -r -- "days_fx=61|3|2";;
    (day=Sun*) print -r -- "day=Sun|Partly cloudy|22$D|12$D|18%";;
    (rate_chg=USD*) print -r -- "rate_chg=USD|up|+0,3%|+0,0027|15 Oct";;
    (rate_chg=GBP*) print -r -- "rate_chg=GBP|down|-0,2%|-0,0024|15 Oct";;
    (rate_chg=CHF*) print -r -- "rate_chg=CHF|flat|0,0%|0,0000|15 Oct";;
    (*) print -r -- "$1";;
    esac
}
body_dayrain_before() { each dayrain_before_line; }   # the reply before it: USD 0,0040 lower, GBP 0,0025 higher
dayrain_before_line() {
    case $1 in
    (rate=USD*) print -r -- "rate=USD|-|0,8847|16 Oct";;
    (rate=GBP*) print -r -- "rate=GBP|-|1,1875|16 Oct";;
    (*) dayrain_line "$1";;
    esac
}
night_line() {   # $2: the wx_now words and wx_fx of the night
    case $1 in
    (offset=*) print -r -- "offset=39600";;
    (geo=*) print -r -- "geo=52.52|165";;
    (wx_now=*) print -r -- "wx_now=11$D|10$D|71%|$2|6 km/h|night";;
    (wx_fx=*) print -r -- "wx_fx=$3";;
    (wx_rain=*) print -r -- "wx_rain=dry|No rain in the next 12 h";;
    (wx_hours=*) print -r -- "wx_hours=0|11 10 10 9 9 8 8 8 8 9 11 13|0 0 0 0 0 0 0 0 0 0 0 0|0 0 0 0 0 0 0 1 1 1 1 1";;
    (days_fx=*) print -r -- "days_fx=61|2|0";;
    (day=Sun*) print -r -- "day=Sun|Clear|22$D|12$D|0%";;
    (*) print -r -- "$1";;
    esac
}
hol_line() { [[ $1 == holiday=* ]] && { print -r -- "holiday=2026-10-12|Founders' Day|4"; return }; print -r -- "$1"; }   # the next holiday in 4 days
body_hol() { each hol_line; }   # the hourly card from 18:00, the reply's next hour (0.5.0's review: by the reply's clock and zone)
body_nighthol() { local l; for l in "${(@f)$(body_night)}"; do hol_line "$l"; done; }
body_night() { local l; for l in "${(@f)$(< $REPLY)}"; do night_line "$l" Clear "0|3|0|200|6|11"; done; }
body_ocnight() { local l; for l in "${(@f)$(< $REPLY)}"; do night_line "$l" Overcast "3|100|0|200|8|15"; done; }
body_storm() { each storm_line; }   # a thunderstorm by day: WMO 95, 100 %, 8.5 mm, 35 km/h from the NW
storm_line() {
    case $1 in
    (wx_now=*) print -r -- "wx_now=18$D|18$D|94%|Thunderstorm|35 km/h|day";;
    (wx_fx=*) print -r -- "wx_fx=95|100|8.5|300|35|62";;
    (wx_rain=*) print -r -- "wx_rain=rain|Rain now (90%)";;
    (days_fx=*) print -r -- "days_fx=95|61|3";;
    (*) print -r -- "$1";;
    esac
}
body_snowday() { each snowday_line; }   # snow by day, below 0: the minus sign of the digits
snowday_line() {
    case $1 in
    (wx_now=*) print -r -- "wx_now=-1$D|-6$D|90%|Snow|10 km/h|day";;
    (wx_fx=*) print -r -- "wx_fx=73|90|0.4|270|10|15";;
    (wx_rain=*) print -r -- "wx_rain=rain|Snow now (80%)";;
    (day=Today*) print -r -- "day=Today|Snow|1$D|-3$D|80%";;
    (days_fx=*) print -r -- "days_fx=73|71|3";;
    (wx_hours=*) print -r -- "wx_hours=15|-1 -1 -2 -2 -3 -3 -3 -2 -1 0 1 1|80 80 70 60 50 40 30 20 10 10 5 5|73 73 73 71 71 3 3 3 3 2 2 1";;
    (*) print -r -- "$1";;
    esac
}
body_newsmix() { NEWS_I=0; each newsmix_line; }   # the rainy day with mixed sources: their pills' colours
newsmix_line() {
    case $1 in
    (news=*)
        (( NEWS_I++ ))
        case $NEWS_I in
        (2) print -r -- "news=Courier|540|Markets close steady as the central bank holds its rate";;
        (3) print -r -- "news=Herald|1980|City council approves a plan for new bike lanes downtown";;
        (6) print -r -- "news=Wire|3600|Storm warnings issued across the Atlantic coast as a deep low moves in";;
        (*) dayrain_line "$1";;
        esac;;
    (*) dayrain_line "$1";;
    esac
}
body_v1() {    # a minimal Worker: rates (with buy prices), the weather now and the days; no sky, no news
    print -rn -- "couch-board 1
now=1792164000
offset=7200
city=Berlin
rate=USD|0,8850|0,8920|16 Oct
rate=GBP|1,1800|1,1900|16 Oct
rate_age=42
rate_state=ok
wx_now=11$D|10$D|92%|Overcast|9 km/h|night
day=Today|Rain showers|16$D|11$D|96%
day=Sat|Overcast|16$D|9$D|2%
day=Sun|Drizzle|16$D|10$D|22%
wx_age=310
wx_state=ok
end
"
}

scn() {   # name script [VAR=value ...]; JOURNAL, BODY, NEWS, URL, AUTO, BIN, SAFE, LAST from the caller
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
    [[ -n $NIGHT ]] && print -r -- "$NIGHT" > $sbx/$DIR/night.txt   # 0.5.0: the night's hours
    [[ -n $NOFOLDER ]] && { rm -rf $sbx/$DIR; : > $sbx/$DIR }   # the app folder is a file: no folder, no journal
    body_${BODY:-ok} > $sbx/body.txt
    [[ -n $LAST ]] && { print -r -- "couch-last 1|$(( 1791460800 - LAST ))|${LASTURL:-${URL-$URL_LAN}}"; body_ok } > $sbx/$DIR/board_last.txt   # the last reply, LAST s old
    local b2=()
    [[ -n $BODY2 ]] && { body_$BODY2 > $sbx/body2.txt; b2=(PV_BOARD2=$sbx/body2.txt) }   # the second and later replies
    local rd=(PV_ARTICLES=$FIX/articles PV_IMAGES=$P/fixtures/images)   # the reader: /news from the fixture (NEWS=none, 404), the articles, the photos (0.6.0) articles
    if [[ ${FUT:-day} != 404 ]]; then                   # the Futbol page: d0 and d1 (FUT=empty, none: d0 so; 404: no /futbol)
        mkdir -p $sbx/futbol
        case ${FUT:-day} in
        (day) futbol_today > $sbx/futbol/d0.txt;;
        (*) futbol_$FUT > $sbx/futbol/d0.txt;;
        esac
        futbol_tomorrow > $sbx/futbol/d1.txt
        rd+=(PV_FUTBOL=$sbx/futbol)
    fi
    if [[ ${ECON:-ok} != 404 ]]; then                   # the Economía page (0.6.0): ECON=ok (default), stale, none; 404: no /economia
        econ_${ECON:-ok} > $sbx/econ.txt
        rd+=(PV_ECON=$sbx/econ.txt)
    fi
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
pages|all)         # L1 and R1 cycle the Board, the News, the Futbol and the Economía page (0.6.0): R1 to the News, R1 to the Futbol page,
                   # R1 to the Economía page, R1 wraps to the Board, L1 to the Economía page; SELECT opens the Info page, R1 there does
                   # nothing, CIRCLE goes back
    scn pages "120:SNAP=pages_board 122:R1 128:SNAP=pages_slide 150:SNAP=pages_news 152:R1 180:SNAP=pages_futbol 182:R1 210:SNAP=pages_econ 212:R1 240:SNAP=pages_back 242:L1 270:SNAP=pages_l1 272:SELECT 300:SNAP=pages_info 302:R1 330:SNAP=pages_info_r1 332:CIRCLE 360:SNAP=pages_circle 361:EXIT" ;|
board_nonews|all)  # a dry forecast, the news feeds failed (0.5.1: no extras, which no Worker sends): no ticker, the News page says why
    NEWS=none BODY=nonews scn board_nonews "120:SNAP=board_nonews 122:R1 150:SNAP=news_none 151:EXIT" ;|
board_v1|all)      # a Worker of 0.1.0's time: no money, no sky, no news lines, no /news (the new Couch draws what came)
    NEWS=404 BODY=v1 scn board_v1 "120:SNAP=board_v1 122:R1 150:SNAP=news_v1 151:EXIT" ;|
net_slow|all)      # the connect takes 2 s: the header says the internet is slow (0.5.0's review: by the connect's time); a slow
                   # Worker (its answer 4 s after a fast connect) is no slow internet
    scn net_slow "400:SNAP=net_slow 402:SELECT 430:SNAP=net_slow_info 431:EXIT" PV_BOARD_FAIL=slownet
    scn net_slowworker "400:SNAP=net_slowworker 401:EXIT" PV_BOARD_FAIL=slow ;|
net_down|all)      # a good reply, then SQUARE after 10 s and a refused connect: no internet since 09:00
    scn net_down "120:SNAP=net_down_first 700:SQUARE 820:SNAP=net_down 822:SELECT 850:SNAP=net_down_info 851:EXIT" PV_BOARD_FAIL=ok,refused ;|
ambient|all)       # 10 min with no button (1 min in the preview's build): the waiting screen; after 2 min of it the block
                   # fades out (ambient_fade, half way), comes back at its next place (ambient_moved); START wakes the
                   # screen (the Board) and does not exit; the fetches go on (every 5 min: 3 min of frames, one)
    BODY=hol scn ambient "120:SNAP=ambient_before 3720:SNAP=ambient 7320:SNAP=ambient_news 10800:SNAP=ambient_fade 10920:SNAP=ambient_moved 10940:START 10960:SNAP=ambient_awake 10970:EXIT" PV_MAXFRAMES=11000
    print -r -- "  requests: $(command grep -c '^GET' $P/build/sbx/ambient$SFX/peer.log); couch.txt: $(command grep -h '^ambient' $P/build/sbx/ambient$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
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
board_dns|all)     # a host name in board_url.txt, and the preview has no DNS
    URL=http://board.example.com/board scn board_dns "120:SNAP=board_dns 121:EXIT" ;|
frozen|all)        # the network start froze before: the Board says so; SELECT, SQUARE twice, CIRCLE: the start runs and the Board fills
    JOURNAL=$'start net_start\n'$TZ_DONE scn frozen "100:SNAP=frozen 102:SELECT 130:SNAP=frozen_info 132:SQUARE 136:SQUARE 140:SNAP=frozen_cleared 142:CIRCLE 260:SNAP=frozen_after 261:EXIT" ;|
tz_skip|all)       # a first start: the clock's prompt, CIRCLE skips it, the Board's offset sets the clock
    JOURNAL= scn tz_skip "30:SNAP=prompt 32:CIRCLE 150:SNAP=tz_skip 152:SELECT 180:SNAP=tz_skip_info 181:EXIT" ;|
tz_12h|all)        # CROSS at the prompt; TIME_FORMAT 0 (a 12-hour clock), UTC+5:30
    JOURNAL= scn tz_12h "32:CROSS 150:SNAP=tz_12h 152:SELECT 180:SNAP=tz_12h_info 181:EXIT" PV_TIMEFMT=0 PV_TZ=330 ;|
tz_fail|all)       # 0.5.1's review: the three clock settings' reads fail: the Board's offset sets the clock; Info names the rc
    scn tz_fail "120:SNAP=tz_fail 122:SELECT 150:SNAP=tz_fail_info 151:EXIT" PV_TZ_FAIL=1 ;|
tz_summer|all)     # 0.5.1's review: SUMMERTIME on: the clock an hour on (UTC-2); Info's clock row says "with summer time"
    scn tz_summer "120:SNAP=tz_summer 122:SELECT 150:SNAP=tz_summer_info 151:EXIT" PV_SUMMER=1 ;|
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
fx_late_start|all) # 0.5.1's review: SPU 2 never puts its start report: fx_start's 2 s deadline, the terminate and the join, RSX only
    scn fx_late_start "200:SNAP=fx_late_start 201:EXIT" PV_FX_LATE=start
    fxends fx_late_start ;|
fx_late_aurora|all) # 0.5.1's review: the waiting screen at night, the aurora's first picture never comes: RSX only 2 s after it was
                   # sent, fx_aurora closed as failed; the waiting screen is then RSX only's dimmed page
    BODY=night scn fx_late_aurora "3900:SNAP=fx_late_aurora 3901:EXIT" PV_TZ=660 PV_FX_LATE=aurora PV_MAXFRAMES=4000
    fxends fx_late_aurora ;|
fx_import|all)     # 0.5.1's review: the image's import fails: no group, RSX only
    scn fx_import "120:EXIT" PV_SPU_FAIL=import
    fxends fx_import ;|
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
board_rain|all)    # the Board of 0.3.0 on the mockup's rainy day; SQUARE at 11.7 s brings USD 0,0040 up and GBP 0,0025
                   # down: they counted and pulsed, and keep a low glow (UP amber, DOWN teal) until the next reply
    BODY=dayrain_before BODY2=dayrain scn board_rain "120:SNAP=board_rain_first 700:SQUARE 1150:SNAP=board_rain 1151:EXIT" ;|
board_rain_tv|all) # the same at a TV's measured area (not in the narrow pass)
    [[ $AREA != narrow ]] && SAFE='0 2 1920 1078' BODY=dayrain_before BODY2=dayrain scn board_rain_tv "700:SQUARE 1150:SNAP=board_rain_tv 1151:EXIT" ;|
board_night|all)   # a clear night (23:00): stars, the moon's sphere, the dark arc with the moon on it, no rain
    BODY=night scn board_night "120:SNAP=board_night 121:EXIT" PV_TZ=660 ;|
board_ocnight|all) # an overcast night: the cover's grey scaled by the light, no brighter than the clear night
    BODY=ocnight scn board_ocnight "120:SNAP=board_ocnight 121:EXIT" PV_TZ=660 ;|
board_storm|all)   # a thunderstorm (WMO 95): heavy slanted rain; frame 360 is the first bolt (6 s) with its flash
    BODY=storm scn board_storm "120:SNAP=board_storm_rain 360:SNAP=board_storm 361:EXIT" ;|
board_snow|all)    # snow by day at -1 deg: the flakes, the hero's snowflakes, the minus sign
    BODY=snowday scn board_snow "120:SNAP=board_snow 121:EXIT" ;|
board_south|all)   # 0.5.1's review: a place south of the equator: the moon's sphere lit from the other side (the north's left is
                   # the south's right), also when its first picture, made before the first reply, had the north's view
    BODY=south scn board_south "120:SNAP=board_south 121:EXIT" ;|
board_old|all)     # a Worker of 0.2.0's time (no geo, wx_fx, wx_hours, days_fx, chips, spark): each value's "-" form,
                   # the sky from wx_now's day or night
    BODY=old scn board_old "120:SNAP=board_old 121:EXIT" ;|
board_fxoff|all)   # the effects off (fx_first froze before: RSX only): solid panels, the sprites, the PPU's rain; the glow too
    JOURNAL=$'start fx_first\n'$TZ_DONE BODY=dayrain_before BODY2=dayrain scn board_fxoff "120:SNAP=board_fxoff_first 700:SQUARE 1150:SNAP=board_fxoff 1151:EXIT" ;|
setup|all)         # 0.4.0: no board_url.txt: the Board's setup steps, the News page and Info say there is no URL, no request
    URL=none scn setup "120:SNAP=setup 122:R1 150:SNAP=setup_news 152:SELECT 180:SNAP=setup_info 181:EXIT"
    print -r -- "  requests: $(command grep -c '^GET' $P/build/sbx/setup$SFX/peer.log 2>/dev/null || print 0)" ;|
tv|all)            # a TV's measured visible area, 0 2 1920 1078 (not in the narrow pass: it sets its own area)
    [[ $AREA != narrow ]] && SAFE='0 2 1920 1078' scn tv "120:SNAP=tv_board 122:R1 150:SNAP=tv_news 152:SELECT 180:SNAP=tv_info 181:EXIT" ;|
ambient_night|all) # 0.3.2: the waiting screen at night (the aurora) with the next holiday; 20 s later the next three headlines
    BODY=nighthol scn ambient_night "3720:SNAP=ambient_night 4980:SNAP=ambient_night_next 4981:EXIT" PV_TZ=660 PV_MAXFRAMES=5000 ;|
holiday|all)       # 0.3.2: the header's second line names the next holiday (the clock is the console's own)
    BODY=hol scn holiday "120:SNAP=holiday 121:EXIT" ;|
ambient_rsx|all)   # the ambient mode in RSX-only mode (fx_first froze before): 0.2.0's dimmed page, 6 px moves, the page each minute
    JOURNAL=$'start fx_first\n'$TZ_DONE scn ambient_rsx "3720:SNAP=ambient_rsx 7320:SNAP=ambient_rsx_news 7330:EXIT" PV_MAXFRAMES=8000 ;|
start|all)         # the start: since 0.4.1 the background from the first frame after ui_init (frame 3), no counter
                   # in a short effects' job (frame 7), the start animation half way, then the Board; couch.txt's
                   # start times, ui_init's steps and boot.txt's marks
    scn start "3:SNAP=start_first 7:SNAP=start_fx 30:SNAP=start_mid 120:SNAP=start_end 121:EXIT"
    print -r -- "  couch.txt: $(command grep -h '^(start|ui_init)' -E $P/build/sbx/start$SFX/$DIR/couch.txt | tr '\n' ';')"
    print -r -- "  boot.txt: $(tr '\n' ' ' < $P/build/sbx/start$SFX/$DIR/boot.txt)" ;|
slow_net|all)      # 0.4.1: a network start of 3 s: the job screen's counter shows after 2 s, not before
    scn slow_net "40:SNAP=slow_net_1s 200:SNAP=slow_net 400:EXIT" PV_NET_SLOW=3000 ;|
last|all)          # 0.4.1: board_last.txt of 30 min ago and a 4 s answer: the first frames show the last reply with its
                   # age, then the fetch's reply; an old file (2 days) and one of another URL are not shown
    LAST=1800 scn last "30:SNAP=last_start 120:SNAP=last 400:SNAP=last_fetched 401:EXIT" PV_BOARD_FAIL=slow
    print -r -- "  couch.txt: $(command grep -h '^board_last' $P/build/sbx/last$SFX/$DIR/couch.txt)"
    LAST=172800 scn last_old "30:SNAP=last_old 31:EXIT" PV_BOARD_FAIL=slow
    print -r -- "  couch.txt: $(command grep -h '^board_last' $P/build/sbx/last_old$SFX/$DIR/couch.txt)"
    LAST=1800 LASTURL=http://other.example.com/board scn last_url "30:EXIT" PV_BOARD_FAIL=slow
    print -r -- "  couch.txt: $(command grep -h '^board_last' $P/build/sbx/last_url$SFX/$DIR/couch.txt)" ;|
last_day|all)      # 0.5.1's review: board_last.txt of 10 h ago (the Worker test's reply, 17:20 on Friday at the place) and every
                   # fetch refused: at 03:20 on Saturday the days start at Sat, the hero's bar is Sat's, the 12 h curve starts at
                   # 03, no air chips, the bar names the data's date; the waiting screen: Sat's high and low, the hours from 03
    LAST=36000 scn last_day "120:SNAP=last_day 3720:SNAP=last_day_ambient 3721:EXIT" PV_BOARD_FAIL=refused PV_MAXFRAMES=4000 ;|
skeleton|all)      # before the first reply (a 4 s answer): the skeleton's bars on the Board and on the News page
    scn skeleton "120:SNAP=skeleton 122:R1 150:SNAP=skeleton_news 151:EXIT" PV_BOARD_FAIL=slow ;|
news_rain|all)     # the News page on the rainy day of the mockups (the sources' pills, the categories, the ages): since 0.3.1 the
                   # list of /news (the fixture)
    BODY=newsmix scn news_rain "120:R1 150:SNAP=news_rain 151:EXIT" ;|
reader|all)        # the reader (0.3.1): R1, the News page with /news's 30 headlines, the featured card selected; DOWN four times and
                   # RIGHT: the list scrolls (3 frames after the last press, then at rest); SQUARE 10 s later asks the list again
    scn reader "120:R1 160:SNAP=news_list 162:DOWN 168:DOWN 174:DOWN 180:DOWN 186:RIGHT 189:SNAP=news_scrolling 240:SNAP=news_scrolled 900:SQUARE 960:SNAP=news_refresh 961:EXIT"
    print -r -- "  requests: $(command grep -c '^GET /news' $P/build/sbx/reader$SFX/peer.log) of /news, $(command grep -c '^GET /board' $P/build/sbx/reader$SFX/peer.log) of /board; couch.txt: $(command grep -h -e '^news' -e '^stext' $P/build/sbx/reader$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
reader_article|all) # CROSS on the second headline: the loading (a 4 s answer: the skeleton), the article with its QR code; DOWN,
                   # then DOWN held a second: it glides; CIRCLE: the list, its selection kept
    scn reader_article "120:R1 160:DOWN 166:CROSS 200:SNAP=article_loading 460:SNAP=article 462:DOWN 470-540:DOWN 600:SNAP=article_scrolled 602:CIRCLE 640:SNAP=article_back 641:EXIT" PV_ARTICLE_FAIL=slow
    print -r -- "  requests: $(command grep -h '^GET /article' $P/build/sbx/reader_article$SFX/peer.log | tr -d '\r' | tr '\n' ';') couch.txt: $(command grep -h '^article' $P/build/sbx/reader_article$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
reader_photo|all)  # 0.6.0: CROSS on the second headline (f80706d6, photo=1): the article with the place for its photo, then the photo
                   # (a 1200 x 675 JPEG, decoded to 960 x 540 on the Board thread); DOWN held: the photo scrolls with the text, faded at the band
    scn reader_photo "120:R1 160:DOWN 166:CROSS 200:SNAP=photo_loading 500:SNAP=photo 502-560:DOWN 620:SNAP=photo_scrolled 622:CIRCLE 660:EXIT"
    print -r -- "  requests: $(command grep -h -e '^GET /article' -e '^GET /image' $P/build/sbx/reader_photo$SFX/peer.log | tr -d '\r' | tr '\n' ';') couch.txt: $(command grep -h -e '^article' -e '^photo' $P/build/sbx/reader_photo$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
reader_states|all) # the article's other forms, each one line: the feed's summary (the publisher refused the page), a text cut at 12 KB, an
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
reader_title|all)  # 0.5.0's review: an article's title of 7 lines and more: its 6th line ends in "..."
    A2=$P/build/sbx/articles_title$SFX
    rm -rf $A2 && mkdir -p $A2 && cp $FIX/articles/*.txt $A2/
    TT="$(printf 'Leaders meet in the capital to agree on a new plan for river shipping and ports %.0s' 1 2 3 4 5)"
    sed -i '' "s|^title=.*|title=${TT% }|" $A2/f80706d6.txt
    scn reader_title "120:R1 160:DOWN 166:CROSS 260:SNAP=article_title 261:EXIT" PV_ARTICLES=$A2 ;|
board_soon|all)    # 0.5.0: the rain of the next 2 h in the hero's band, the air's chips, the futbol line in the header by turns
                   # with the holiday (8 s each: frame 600 is the futbol line); then raining until 19:30, a poor air, the next game
    BODY=soon scn board_soon "120:SNAP=board_soon 600:SNAP=board_soon_futbol 601:EXIT"
    BODY=soon2 scn board_soon2 "120:SNAP=board_soon2 600:SNAP=board_soon2_futbol 601:EXIT" ;|
futbol|all)        # 0.5.0: the Futbol page (R1 twice from the Board; L1 since 0.6.0 opens the Economía page): today's leagues with live games, the list scrolled, tomorrow (RIGHT),
                   # back to today (LEFT); a minute later the live list again (every 60 s while a game is live), before the
                   # waiting screen (60 s after the last press in the preview's build)
    scn futbol "120:R1 124:R1 160:SNAP=futbol_today 162:DOWN 166:DOWN 170:DOWN 174:DOWN 178:DOWN 182:DOWN 186:DOWN 190:DOWN 230:SNAP=futbol_scrolled 232:RIGHT 280:SNAP=futbol_tomorrow 282:LEFT 300:SNAP=futbol_back 3840:SNAP=futbol_live 3841:EXIT" PV_MAXFRAMES=4000
    print -r -- "  requests: $(command grep -h '^GET /futbol' $P/build/sbx/futbol$SFX/peer.log | tr -d '\r' | tr '\n' ';') couch.txt: $(command grep -h '^futbol' $P/build/sbx/futbol$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
econ|all)          # 0.6.0: the Economía page (L1 from the Board): the indicators by section with the inflation chart; a stale
                   # reply with two sources missing; the Worker's state none; no /economia (a Worker before the page: 404); SQUARE asks again
    scn econ "120:L1 170:SNAP=econ 171:EXIT"
    print -r -- "  requests: $(command grep -h '^GET /economia' $P/build/sbx/econ$SFX/peer.log | tr -d '\r' | tr '\n' ';') couch.txt: $(command grep -h '^econ' $P/build/sbx/econ$SFX/$DIR/couch.txt | tr '\n' ';')"
    ECON=stale scn econ_stale "120:L1 170:SNAP=econ_stale 171:EXIT"
    ECON=none scn econ_none "120:L1 170:SNAP=econ_none 171:EXIT"
    ECON=404 scn econ_404 "120:L1 170:SNAP=econ_404 171:EXIT"
    scn econ_square "120:L1 800:SQUARE 900:SNAP=econ_square 901:EXIT"
    print -r -- "  requests: $(command grep -h '^GET /economia' $P/build/sbx/econ_square$SFX/peer.log | tr -d '\r' | tr '\n' ';')"
    scn econ_slow "120:L1 170:SNAP=econ_skeleton 171:EXIT" PV_ECON_FAIL=slow ;|
futbol_states|all) # 0.5.0: no games today, the source failed (the Worker's state none), no /futbol (a Worker without the page: 404), the
                   # skeleton of a slow answer
    FUT=empty scn futbol_empty "120:R1 124:R1 160:SNAP=futbol_empty 161:EXIT"
    FUT=none scn futbol_none "120:R1 124:R1 160:SNAP=futbol_none 161:EXIT"
    FUT=404 scn futbol_404 "120:R1 124:R1 160:SNAP=futbol_404 161:EXIT"
    scn futbol_slow "120:R1 124:R1 160:SNAP=futbol_skeleton 161:EXIT" PV_FUTBOL_FAIL=slow
    # 0.5.0's review: SQUARE on the Futbol page asks only its list, not the Board
    scn futbol_square "120:R1 124:R1 800:SQUARE 900:SNAP=futbol_square 901:EXIT"
    print -r -- "  requests: $(command grep -c '^GET /futbol' $P/build/sbx/futbol_square$SFX/peer.log) of /futbol, $(command grep -c '^GET /board' $P/build/sbx/futbol_square$SFX/peer.log) of /board" ;|
info_console|all)  # 0.5.0: SELECT with no fan_policy in the journal: the fan's first-run prompt, CROSS; the Info page with the Hardware row;
                   # TRIANGLE edits the night, RIGHT to the start, UP twice; CROSS saves night.txt; then SELECT on a hot console
    JOURNAL=$TZ_ONLY scn info_console "120:SELECT 150:SNAP=info_fan_prompt 152:CROSS 180:SNAP=info_console 182:TRIANGLE 210:SNAP=info_night_edit 212:RIGHT 216:UP 220:UP 240:SNAP=info_night_changed 242:CROSS 270:SNAP=info_night_saved 271:EXIT"
    print -r -- "  night.txt: $(< $P/build/sbx/info_console$SFX/$DIR/night.txt); journal: $(command grep -h -e temps -e fan_policy $P/build/sbx/info_console$SFX/$DIR/journal.txt | tr '\n' ';') couch.txt: $(command grep -h -e '^console' -e '^fan_policy' -e '^night' $P/build/sbx/info_console$SFX/$DIR/couch.txt | tr '\n' ';')"
    JOURNAL=$TZ_ONLY scn info_fan_skip "120:SELECT 150:CIRCLE 180:SNAP=info_fan_skipped 181:EXIT" PV_TEMPS=74,71 ;|
info_rc|all)       # 0.5.1's review: the Info page's two reads refused (383, the temperatures; 409, the fan): their rcs, YELLOW
    scn info_rc "120:SELECT 150:SNAP=info_rc 151:EXIT" PV_383_RC=0x80010003 PV_409_RC=0x80010003 ;|
ambient_nightmode|all) # 0.5.0: night.txt 22-07 and 23:00 at the place: the waiting screen is the dim clock on black; 2 min later it
                   # fades out (half way) and comes back at its next place
    NIGHT=22-07 BODY=night scn ambient_nightmode "3720:SNAP=ambient_nightmode 10800:SNAP=ambient_nightmode_fade 10920:SNAP=ambient_nightmode_moved 10921:EXIT" PV_TZ=660 PV_MAXFRAMES=11000
    print -r -- "  couch.txt: $(command grep -h -e '^ambient' -e '^night' -e 'sky pictures' $P/build/sbx/ambient_nightmode$SFX/$DIR/couch.txt | tr '\n' ';')" ;|
esac
[[ -n $PV_CHILD ]] && exit 0                 # the PNGs: the parent's, once
py=${PYTHON:-python3}
$py -I - $OUT <<'PY' 2>/dev/null || print "no Pillow: the snapshots stay .ppm"
import glob, os, sys
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], '*.ppm')):
    Image.open(p).save(p[:-4] + '.png')
PY
