/* Couch's host check of source/parse.c and source/astro.c
 * (tools/hostcheck/run.zsh): the Board parser against the Worker test's
 * reply (argv[1], the expected text of couch/worker/test/board.test.js's
 * first test, which run.zsh extracts), that reply cut at every byte, with
 * CR LF, as version 2, with long fields, at 4 KB, with too many lines, the
 * states and the missing fields; the same reply with the keys of 0.3.0
 * (argv[2]: geo, wx_fx, wx_hours, days_fx, usd_chg, money_chg, spark) and
 * their rules; the fold; the HTTP head, the URL, the clock, the ages,
 * auto.txt; the numbers, the sky's colours, the weather codes, the wind;
 * the sun against Open-Meteo's sunrises and a table of NOAA's full
 * algorithm, the moon against the 2026 eclipses. With argv[3] "fuzz", also
 * 20,000 mutated replies of each (a sanitizer build: no crash, every field
 * ends inside its buffer, every number in its range). Prints PASS or FAIL
 * per group; the exit status is the number of failed checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "parse.h"
#include "astro.h"

static int fails;
#define CHECK(c)                                                              \
    do {                                                                      \
        if (!(c)) {                                                           \
            fails++;                                                          \
            printf("  FAIL line %d: %s\n", __LINE__, #c);                     \
        }                                                                     \
    } while (0)
#define STR(a, b) CHECK(!strcmp((a), (b)))

static void group(const char *name, int f0) { printf("%s %s\n", fails == f0 ? "PASS" : "FAIL", name); }

static board_data b;

static int parse(const char *s, int fold) { return board_parse(s, (int)strlen(s), fold, &b); }

/* Every text field of b ends inside its buffer, every count and number of
 * the keys of 0.3.0 is in its range or missing. */
static int bounded(void)
{
#define IN(f) (memchr((f), 0, sizeof(f)) != NULL)
#define RNG(v, lo, hi, none) ((v) == (none) || ((v) >= (lo) && (v) <= (hi)))
    int ok = IN(b.city) && IN(b.temp) && IN(b.feels) && IN(b.humidity) && IN(b.words) && IN(b.wind) && IN(b.wx_src) && IN(b.usd_feed.state) &&
             IN(b.usd_feed.reason) && IN(b.wx_feed.state) && IN(b.wx_feed.reason) && b.n_usd <= BOARD_USD && b.n_day <= BOARD_DAYS &&
             IN(b.rain) && IN(b.sunrise) && IN(b.sunset) && IN(b.moon) && IN(b.news_feed.state) && IN(b.news_feed.reason) &&
             b.n_money <= BOARD_MONEY && b.n_news <= BOARD_NEWS;
    for (int i = 0; i < b.n_usd; i++) ok = ok && IN(b.usd[i].label) && IN(b.usd[i].buy) && IN(b.usd[i].sell) && IN(b.usd[i].when);
    for (int i = 0; i < b.n_day; i++)
        ok = ok && IN(b.day[i].label) && IN(b.day[i].words) && IN(b.day[i].max) && IN(b.day[i].min) && IN(b.day[i].rain);
    for (int i = 0; i < b.n_money; i++)
        ok = ok && IN(b.money[i].label) && IN(b.money[i].value) && IN(b.money[i].when) && IN(b.money[i].state) && IN(b.money[i].reason);
    for (int i = 0; i < b.n_news; i++) ok = ok && IN(b.news[i].source) && IN(b.news[i].title) && b.news[i].title[0];
    ok = ok && (!b.has_geo || (b.lat >= -90 && b.lat <= 90 && b.lon >= -180 && b.lon <= 180));
    ok = ok && RNG(b.fx_code, 0, 99, -1) && RNG(b.fx_cover, 0, 100, -1) && RNG(b.fx_dir, 0, 359, -1) && RNG(b.fx_speed, 0, 500, -1) &&
         RNG(b.fx_gusts, 0, 500, -1) && (b.fx_mm == -1 || (b.fx_mm >= 0 && b.fx_mm <= 1000)) && RNG(b.hours_start, 0, 23, -1);
    ok = ok && b.n_hours >= 0 && b.n_hours <= HOURS_MAX && b.n_day_code >= 0 && b.n_day_code <= BOARD_DAYS;
    for (int i = 0; i < HOURS_MAX; i++)
        ok = ok && RNG(b.hour_temp[i], -90, 70, TEMP_NONE) && RNG(b.hour_pop[i], 0, 100, -1) && RNG(b.hour_code[i], 0, 99, -1);
    for (int i = 0; i < BOARD_DAYS; i++) ok = ok && RNG(b.day_code[i], 0, 99, -1);
    ok = ok && b.n_usd_chg >= 0 && b.n_usd_chg <= BOARD_USD && b.n_money_chg >= 0 && b.n_money_chg <= BOARD_MONEY && b.n_spark >= 0 &&
         b.n_spark <= BOARD_SPARKS;
    for (int i = 0; i < b.n_usd_chg + b.n_money_chg; i++) {
        const board_chg *g = i < b.n_usd_chg ? &b.usd_chg[i] : &b.money_chg[i - b.n_usd_chg];
        ok = ok && IN(g->label) && IN(g->pct) && IN(g->abs) && IN(g->when) && g->dir >= CHG_NONE && g->dir <= CHG_FLAT;
    }
    for (int i = 0; i < b.n_spark; i++) {
        ok = ok && IN(b.spark[i].label) && b.spark[i].n >= 0 && b.spark[i].n <= SPARK_MAX && b.spark[i].start_day >= -1;
        for (int j = 0; j < SPARK_MAX; j++) ok = ok && RNG(b.spark[i].v[j], 0, 100000000, -1);
    }
    return ok;
#undef RNG
#undef IN
}

static char *read_file(const char *p, int *n)
{
    FILE *f = fopen(p, "rb");
    char *s = malloc(65536);
    if (!f || !s) {
        printf("FAIL cannot read %s\n", p);
        exit(1);
    }
    *n = (int)fread(s, 1, 65535, f);
    s[*n] = 0;
    fclose(f);
    return s;
}

static void worker_reply(const char *w, int wn)
{
    int f0 = fails;
    CHECK(board_parse(w, wn, 0, &b) == BP_OK);
    CHECK(b.version == 1 && b.now == 1791913620L && b.has_offset && b.offset == -10800);
    STR(b.city, "Jun\xc3\xadn");
    CHECK(b.n_usd == 6);
    static const char *const want[6][4] = {
        {"Oficial", "1.485,00", "1.535,00", "Fri 15:00"}, {"Blue", "1.520,00", "1.540,00", "Fri 18:00"},
        {"MEP", "1.535,90", "1.542,90", "Fri 18:00"},     {"CCL", "1.605,50", "1.606,60", "Fri 18:00"},
        {"Cripto", "1.601,81", "1.606,88", "Fri 18:00"},  {"Tarjeta", "1.930,50", "1.995,50", "Fri 15:00"}};
    for (int i = 0; i < 6 && i < b.n_usd; i++) {
        STR(b.usd[i].label, want[i][0]);
        STR(b.usd[i].buy, want[i][1]);
        STR(b.usd[i].sell, want[i][2]);
        STR(b.usd[i].when, want[i][3]);
    }
    STR(b.usd_feed.state, "ok");
    CHECK(b.usd_feed.age == 42 && !b.usd_feed.reason[0]);
    CHECK(b.has_wx && b.is_day);
    STR(b.temp, "19\xc2\xb0");
    STR(b.feels, "17\xc2\xb0");
    STR(b.humidity, "61%");
    STR(b.words, "Overcast");
    STR(b.wind, "10 km/h");
    STR(b.wx_src, "Open-Meteo");
    CHECK(b.n_day == 3);
    STR(b.day[0].label, "Today");
    STR(b.day[0].words, "Drizzle");
    STR(b.day[0].max, "19\xc2\xb0");
    STR(b.day[0].min, "9\xc2\xb0");
    STR(b.day[0].rain, "75%");
    STR(b.day[1].label, "Wed");
    STR(b.day[1].max, "20\xc2\xb0");
    STR(b.day[2].label, "Thu");
    STR(b.day[2].words, "Drizzle");
    STR(b.day[2].rain, "48%");
    STR(b.wx_feed.state, "ok");
    CHECK(b.wx_feed.age == 310);
    CHECK(b.n_money == 3);
    STR(b.money[0].label, "Riesgo pa\xc3\xads");
    STR(b.money[0].value, "589");
    STR(b.money[0].when, "12 Oct");
    STR(b.money[0].state, "ok");
    STR(b.money[1].label, "Inflaci\xc3\xb3n mensual");
    STR(b.money[1].value, "1,7%");
    STR(b.money[1].when, "Aug 2026");
    STR(b.money[2].label, "Plazo fijo BNA (TNA)");
    STR(b.money[2].value, "19,25%");
    STR(b.money[2].when, "14:47");
    CHECK(!b.money[2].reason[0]);
    CHECK(b.has_rain && b.rain_wet);
    STR(b.rain, "Rain now (75%)");
    CHECK(b.has_sun);
    STR(b.sunrise, "06:24");
    STR(b.sunset, "19:16");
    STR(b.moon, "Waxing crescent moon");
    CHECK(b.n_news == 8);
    STR(b.news[0].source, "Infobae");                 /* Infobae's items are newer than the reply's now: age 0 */
    CHECK(b.news[0].age == 0 && b.news[3].age == 0 && b.news[4].age == 1335 && b.news[7].age == 1611);
    STR(b.news[3].title, "Galbiati: \"Hemos perdido lucidez, pero hemos aprendido una lecci\xc3\xb3n\"");
    STR(b.news[7].source, "Clar\xc3\xadn");
    STR(b.news[7].title, "La embajadora de EEUU en Grecia dimite ante esc\xc3\xa1ndalo de presunta corrupci\xc3\xb3n");
    STR(b.news_feed.state, "ok");
    CHECK(b.news_feed.age == 300);
    CHECK(bounded());
    CHECK(board_has_end(w, wn));
    group("the Worker test's reply, every field", f0);

    f0 = fails;
    CHECK(board_parse(w, wn, 1, &b) == BP_OK);
    STR(b.temp, "19");
    STR(b.day[0].max, "19");
    STR(b.city, "Junin");
    STR(b.usd[5].label, "Tarjeta");
    STR(b.money[0].label, "Riesgo pais");
    STR(b.news[3].title, "Galbiati: \"Hemos perdido lucidez, pero hemos aprendido una leccion\"");
    STR(b.news[6].title, "El ruido por Ramon Diaz y la respuesta de River: el plantel respaldo a Ponzio, ?que dicen los dirigentes?");   /* the inverted question mark is a "?" */
    STR(b.news[7].title, "La embajadora de EEUU en Grecia dimite ante escandalo de presunta corrupcion");
    group("the same reply folded to ASCII (text_utf8 failed)", f0);

    f0 = fails;
    char *crlf = malloc((size_t)wn * 2 + 1);
    int k = 0;
    for (int i = 0; i < wn; i++) {
        if (w[i] == '\n') crlf[k++] = '\r';
        crlf[k++] = w[i];
    }
    crlf[k] = 0;
    CHECK(board_parse(crlf, k, 0, &b) == BP_OK && b.n_usd == 6 && b.n_day == 3 && b.n_money == 3 && b.n_news == 8 && board_has_end(crlf, k));
    STR(b.news[7].source, "Clar\xc3\xadn");
    STR(b.moon, "Waxing crescent moon");
    STR(b.usd[0].when, "Fri 15:00");
    STR(b.wx_feed.state, "ok");
    free(crlf);
    group("CR LF line ends", f0);

    f0 = fails;
    int bad_rc = 0, bad_end = 0, bad_bounds = 0;
    for (int n = 0; n < wn; n++) {                   /* every cut: only the one before the last LF has the end line */
        int rc = board_parse(w, n, 0, &b);
        if (n == wn - 1 ? rc != BP_OK : rc == BP_OK) bad_rc++;
        if (board_has_end(w, n)) bad_end++;
        if (!bounded()) bad_bounds++;
    }
    CHECK(bad_rc == 0);
    CHECK(bad_end == 0);
    CHECK(bad_bounds == 0);
    printf("  %d cuts of %d bytes\n", wn, wn);
    group("the reply cut at every byte: BP_CUT (or not a Board) until the end line", f0);

    f0 = fails;
    char *v2 = strdup(w);
    v2[12] = '2';
    CHECK(board_parse(v2, wn, 0, &b) == BP_VERSION && b.version == 2);
    free(v2);
    CHECK(parse("couch-board 10\nend\n", 0) == BP_VERSION && b.version == 10);
    CHECK(parse("couch-board x\nend\n", 0) == BP_NOT_BOARD);
    CHECK(parse("couch-board\nend\n", 0) == BP_NOT_BOARD);
    CHECK(parse("<!DOCTYPE html>\n<html>\n", 0) == BP_NOT_BOARD);
    CHECK(parse("", 0) == BP_NOT_BOARD);
    CHECK(parse("couch-board 1\n", 0) == BP_CUT);
    CHECK(parse("couch-board 1\nend", 0) == BP_OK);   /* a last line without LF counts */
    group("the version line: another version, not a Board, no end", f0);
}

static void rules(void)
{
    int f0 = fails;
    CHECK(parse("couch-board 1\nfoo=bar\njust text\n=x\ncity=X\nend\ncity=Y\nusd=Late|1|2|3\n", 0) == BP_OK);
    STR(b.city, "X");
    CHECK(b.n_usd == 0);
    CHECK(parse("couch-board 1\nusd=A|1|2|3\nday=D1|w|1|2|3\nusd=B|4|5|6\nday=D2|w|1|2|3\nend\n", 0) == BP_OK);
    CHECK(b.n_usd == 2 && b.n_day == 2);
    STR(b.usd[0].label, "A");
    STR(b.usd[1].label, "B");
    STR(b.day[1].label, "D2");
    group("unknown keys and lines with no '=' skipped, the order kept, nothing read after end", f0);

    f0 = fails;
    static char s[8192];
    int k = snprintf(s, sizeof s, "couch-board 1\n");
    for (int i = 0; i < 12; i++) k += snprintf(s + k, sizeof s - k, "usd=R%d|1|2|3\n", i);
    for (int i = 0; i < 5; i++) k += snprintf(s + k, sizeof s - k, "day=D%d|w|1|2|3\n", i);
    snprintf(s + k, sizeof s - k, "end\n");
    CHECK(parse(s, 0) == BP_OK && b.n_usd == BOARD_USD && b.n_day == BOARD_DAYS);
    STR(b.usd[7].label, "R7");
    STR(b.day[2].label, "D2");
    group("at most 8 rate lines and 3 day lines, the first ones", f0);

    f0 = fails;
    k = snprintf(s, sizeof s, "couch-board 1\nusd=");
    memset(s + k, 'L', 500);
    k += 500;
    k += snprintf(s + k, sizeof s - k, "|1.535,00|1.540,00|Fri 18:00\ncity=");
    for (int i = 0; i < 100; i++) k += snprintf(s + k, sizeof s - k, "\xc3\xa9");
    k += snprintf(s + k, sizeof s - k, "\nwx_now=");
    memset(s + k, '9', 300);
    k += 300;
    snprintf(s + k, sizeof s - k, "|x|y|z|w|day\nend\n");
    CHECK(parse(s, 0) == BP_OK && bounded());
    CHECK(strlen(b.usd[0].label) == sizeof b.usd[0].label - 1 && b.usd[0].label[0] == 'L');
    STR(b.usd[0].buy, "1.535,00");
    CHECK(strlen(b.city) == 126);                    /* 127 would split a 2-byte sequence: cut before it */
    CHECK(strlen(b.temp) == sizeof b.temp - 1 && b.is_day);
    CHECK(parse(s, 1) == BP_OK && bounded() && strlen(b.city) == 100 && b.city[0] == 'e' && b.city[99] == 'e');
    group("long fields: cut to their buffers on a code point boundary", f0);

    f0 = fails;
    k = snprintf(s, sizeof s, "couch-board 1\n");
    for (int i = 0; i < 8; i++) {
        k += snprintf(s + k, sizeof s - k, "usd=");
        memset(s + k, 'A' + i, 300);
        k += 300;
        k += snprintf(s + k, sizeof s - k, "|1|2|3\n");
    }
    k += snprintf(s + k, sizeof s - k, "pad=");
    int pad = BOARD_MAX - k - 1 - 4;                 /* the pad line's LF and "end\n" */
    memset(s + k, 'p', (size_t)pad);
    k += pad;
    k += snprintf(s + k, sizeof s - k, "\nend\n");
    CHECK(k == BOARD_MAX);
    CHECK(board_parse(s, k, 0, &b) == BP_OK && b.n_usd == 8 && b.usd[7].label[0] == 'H' && board_has_end(s, k));
    group("a body of exactly 4096 bytes", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\nusd_state=stale|dolarapi.com: HTTP 503\nusd_age=4000\nwx_state=none|open-meteo: no answer in 4 s\nend\n", 0) ==
          BP_OK);
    STR(b.usd_feed.state, "stale");
    STR(b.usd_feed.reason, "dolarapi.com: HTTP 503");
    CHECK(b.usd_feed.age == 4000);
    STR(b.wx_feed.state, "none");
    STR(b.wx_feed.reason, "open-meteo: no answer in 4 s");
    CHECK(b.wx_feed.age == -1);
    CHECK(parse("couch-board 1\nusd_state=weird|x\nwx_state=ok\nusd_age=-5\nwx_age=12x\nend\n", 0) == BP_OK);
    STR(b.usd_feed.state, "none");
    STR(b.usd_feed.reason, "weird|x");
    STR(b.wx_feed.state, "ok");
    CHECK(b.usd_feed.age == -1 && b.wx_feed.age == -1);
    CHECK(parse("couch-board 1\nend\n", 0) == BP_OK && !b.usd_feed.state[0] && b.usd_feed.age == -1);
    group("the states: ok, stale and none with their reasons, another word, bad ages, no state line", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\nusd=Solo\nday=Today|Rain\nwx_now=5\nusd=|||\nend\n", 0) == BP_OK);
    STR(b.usd[0].label, "Solo");
    STR(b.usd[0].buy, "-");
    STR(b.usd[0].when, "-");
    STR(b.day[0].words, "Rain");
    STR(b.day[0].max, "-");
    STR(b.day[0].rain, "-");
    STR(b.temp, "5");
    STR(b.wind, "-");
    CHECK(!b.is_day && !b.wx_src[0]);               /* no 7th field: no source, not "-" */
    STR(b.usd[1].label, "-");
    group("missing and empty fields read as \"-\"", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\ncity=Jun\xc3\xadn\nwx_now=10\xc2\xb0|10\xc2\xb0|84%|Cloudy|3 km/h|day|SMN 23:00\nend\n", 0) == BP_OK);
    CHECK(b.has_wx && b.is_day);
    STR(b.city, "Jun\xc3\xadn");
    STR(b.temp, "10\xc2\xb0");
    STR(b.words, "Cloudy");
    STR(b.wind, "3 km/h");
    STR(b.wx_src, "SMN 23:00");
    CHECK(parse("couch-board 1\ncity=Jun\xc3\xadn\nwx_now=10\xc2\xb0|10|84%|Cloudy|3 km/h|night|SMN 23:00\nend\n", 1) == BP_OK);
    STR(b.city, "Junin");                           /* the fold: U+00ED is i */
    STR(b.temp, "10");
    STR(b.wx_src, "SMN 23:00");
    /* an 8th field is dropped and the 7th ends at its '|': the rule that makes
     * a Couch of before 2026-10-12, which splits 6, read "day" from "day|SMN 23:00" */
    CHECK(parse("couch-board 1\nwx_now=10|10|84%|Cloudy|3 km/h|day|SMN 23:00|later\nend\n", 0) == BP_OK && b.is_day);
    STR(b.wx_src, "SMN 23:00");
    CHECK(parse("couch-board 1\nwx_now=11|10|92%|Overcast|9 km/h|night\nend\n", 0) == BP_OK && !b.is_day && !b.wx_src[0]);
    CHECK(parse("couch-board 1\nwx_now=11|10|92%|Overcast|9 km/h|day|\nend\n", 0) == BP_OK && b.is_day && !b.wx_src[0]);
    CHECK(parse("couch-board 1\nwx_now=11|10|92%|Overcast|9 km/h|day| \x01 \nend\n", 0) == BP_OK && !b.wx_src[0]);
    CHECK(parse("couch-board 1\nwx_now=11|10|92%|Overcast|9 km/h|day|SMN 23:00\nwx_now=11|10|92%|Overcast|9 km/h|day\nend\n", 0) == BP_OK &&
          !b.wx_src[0]);                            /* a repeated wx_now: the last one's source */
    CHECK(parse("couch-board 1\nwx_now=1|2|3|4|5|day|SMN-23:00-and-a-source-longer-than-its-buffer\nend\n", 0) == BP_OK && bounded() &&
          strlen(b.wx_src) == sizeof b.wx_src - 1);
    static char w24[128];                            /* 24 two-byte letters: an SMN state the Worker cut to 24 code points */
    int wk = snprintf(w24, sizeof w24, "couch-board 1\nwx_now=1|2|3|");
    for (int i = 0; i < 24; i++) wk += snprintf(w24 + wk, sizeof w24 - wk, "\xc3\xa9");
    snprintf(w24 + wk, sizeof w24 - wk, "|5|day|SMN 23:00\nend\n");
    CHECK(parse(w24, 0) == BP_OK && strlen(b.words) == 48 && b.is_day);
    STR(b.wx_src, "SMN 23:00");
    group("wx_now's 7th field: the source, absent or empty as \"\", an 8th field dropped; 24 accented letters of words", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\ncity= \t A\x01" "B \x7f \nnow=abc\noffset=60000\nend\n", 0) == BP_OK);
    STR(b.city, "AB");
    CHECK(b.now == 0 && !b.has_offset);
    CHECK(parse("couch-board 1\noffset=-43200\nnow= 1791614218 \nend\n", 0) == BP_OK && b.has_offset && b.offset == -43200 &&
          b.now == 1791614218L);
    group("control bytes dropped, spaces trimmed, numbers checked", f0);
}

/* The keys of 2026-10-13: the money extras, the sky, the news. */
static void extras(void)
{
    int f0 = fails;
    CHECK(parse("couch-board 1\nmoney=A|1|d|ok\nmoney=B|-|-|none|x.com: HTTP 503\nmoney=C|2|e|stale|x.com: no answer in 4 s\n"
                "money=D|3|f|weird\nmoney=E|4|g|ok\nend\n", 0) == BP_OK);
    CHECK(b.n_money == BOARD_MONEY);
    STR(b.money[0].state, "ok");
    CHECK(!b.money[0].reason[0]);
    STR(b.money[1].state, "none");
    STR(b.money[1].value, "-");
    STR(b.money[1].reason, "x.com: HTTP 503");
    STR(b.money[2].state, "stale");
    STR(b.money[2].when, "e");
    STR(b.money[2].reason, "x.com: no answer in 4 s");
    STR(b.money[3].state, "none");                  /* another word is none */
    CHECK(parse("couch-board 1\nmoney=Solo\nend\n", 0) == BP_OK && b.n_money == 1);
    STR(b.money[0].value, "-");
    STR(b.money[0].when, "-");
    STR(b.money[0].state, "none");
    group("money: label|value|date|state|reason, at most 4, another state is none", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\nwx_rain=dry|No rain in the next 12 h\nwx_sun=06:24|19:16\nwx_moon=Full moon\nend\n", 0) == BP_OK);
    CHECK(b.has_rain && !b.rain_wet && b.has_sun);
    STR(b.rain, "No rain in the next 12 h");
    STR(b.moon, "Full moon");
    CHECK(parse("couch-board 1\nwx_rain=rain\nwx_sun=06:24\nend\n", 0) == BP_OK);
    CHECK(b.has_rain && b.rain_wet && !b.has_sun);
    STR(b.rain, "-");
    CHECK(parse("couch-board 1\nend\n", 0) == BP_OK && !b.has_rain && !b.has_sun && !b.moon[0]);
    group("the sky: wx_rain rain or dry, wx_sun with both times, wx_moon; absent from an older Worker", f0);

    f0 = fails;
    static char s[8192];
    int k = snprintf(s, sizeof s, "couch-board 1\nnews=A|60|One\nnews=B|-|Two\nnews=C|5|\nnews=D|x|Four|extra\n");
    for (int i = 0; i < 10; i++) k += snprintf(s + k, sizeof s - k, "news=S|%d|T%d\n", i, i);
    snprintf(s + k, sizeof s - k, "news_age=120\nnews_state=stale|laverdadonline.com: HTTP 500\nend\n");
    CHECK(parse(s, 0) == BP_OK && b.n_news == BOARD_NEWS && bounded());
    STR(b.news[0].title, "One");
    CHECK(b.news[0].age == 60 && b.news[1].age == -1 && b.news[2].age == -1);
    STR(b.news[2].title, "Four");                   /* "C" had no title: not a headline */
    STR(b.news[7].title, "T4");
    STR(b.news_feed.state, "stale");
    STR(b.news_feed.reason, "laverdadonline.com: HTTP 500");
    CHECK(b.news_feed.age == 120);
    CHECK(parse("couch-board 1\nend\n", 0) == BP_OK && !b.n_news && !b.news_feed.state[0] && b.news_feed.age == -1);
    k = snprintf(s, sizeof s, "couch-board 1\nnews=S|1|");
    for (int i = 0; i < 200; i++) k += snprintf(s + k, sizeof s - k, "\xe2\x80\x9c");   /* 200 three-byte quotes */
    snprintf(s + k, sizeof s - k, "\nend\n");
    CHECK(parse(s, 0) == BP_OK && bounded() && strlen(b.news[0].title) == 333);   /* 111 whole code points in 335 bytes */
    group("news: source|age|title, at most 8, no title skipped, the age or -1, the state; a long title cut whole", f0);

    f0 = fails;
    CHECK(net_quality(0, 0) == NQ_DOWN && net_quality(0, 9000000) == NQ_DOWN);
    CHECK(net_quality(1, 750000) == NQ_OK && net_quality(1, NQ_SLOW_US - 1) == NQ_OK && net_quality(1, NQ_SLOW_US) == NQ_SLOW);
    int dx, dy, seen[3][3] = {{0}}, far = 0, jump = 0, px = 0, py = 0;
    for (long st = 0; st < AMBIENT_SHIFTS; st++) {
        ambient_shift(st, &dx, &dy);
        if (dx < -AMBIENT_PX || dx > AMBIENT_PX || dy < -AMBIENT_PX || dy > AMBIENT_PX) far++;
        else seen[dx / AMBIENT_PX + 1][dy / AMBIENT_PX + 1]++;
        if (st && abs(dx - px) + abs(dy - py) != AMBIENT_PX) jump++;   /* one 6-pixel move per step */
        px = dx, py = dy;
    }
    int all = 1;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) all = all && seen[i][j] == 1;
    CHECK(!far && !jump && all);
    ambient_shift(0, &dx, &dy);
    CHECK(!dx && !dy);
    ambient_shift(AMBIENT_SHIFTS, &dx, &dy);
    CHECK(!dx && !dy);
    ambient_shift(-1, &dx, &dy);                     /* a negative step stays in the table */
    CHECK(dx == AMBIENT_PX && dy == -AMBIENT_PX);
    CHECK(ambient_page(0, 1) == 0 && ambient_page(1, 1) == 1 && ambient_page(2, 1) == 0 && ambient_page(1, 0) == 0);
    CHECK(AMBIENT_SEC == 600 && AMBIENT_STEP_SEC == 60);
    group("net_quality (slow from 3 s), the ambient shift (each of 9 places once, 6 px a step), the ambient page", f0);
}

/* ---- the keys of 0.3.0 (design 5.1) ------------------------------------------------ */

/* Each usd buy and sell, money value, change pct and abs of b: num_parse
 * reads it and num_format writes the same text ("-" is not a number). */
static int round_trips(void)
{
    const char *t[64];
    int k = 0, bad = 0;
    for (int i = 0; i < b.n_usd; i++) t[k++] = b.usd[i].buy, t[k++] = b.usd[i].sell;
    for (int i = 0; i < b.n_money; i++) t[k++] = b.money[i].value;
    for (int i = 0; i < b.n_usd_chg; i++) t[k++] = b.usd_chg[i].pct, t[k++] = b.usd_chg[i].abs;
    for (int i = 0; i < b.n_money_chg; i++) t[k++] = b.money_chg[i].pct, t[k++] = b.money_chg[i].abs;
    for (int i = 0; i < k; i++) {
        board_num x;
        char o[32];
        if (!num_parse(t[i], &x)) {
            bad++;
            printf("  not a number: %s\n", t[i]);
            continue;
        }
        num_format(o, sizeof o, &x);
        if (strcmp(o, t[i])) bad++, printf("  %s came back as %s\n", t[i], o);
    }
    return k && !bad;
}

/* No key of 0.3.0 came. */
static int none_new(void)
{
    int ok = !b.has_geo && !b.has_fx && b.fx_code == -1 && b.fx_cover == -1 && b.fx_mm == -1 && b.fx_dir == -1 && b.fx_speed == -1 &&
             b.fx_gusts == -1 && !b.n_hours && b.hours_start == -1 && !b.n_day_code && !b.n_usd_chg && !b.n_money_chg && !b.n_spark;
    for (int i = 0; i < BOARD_DAYS; i++) ok = ok && b.day_code[i] == -1;
    return ok && !board_chg_find(&b, 0, "Blue") && !board_chg_find(&b, 1, "Riesgo pa\xc3\xads") && !board_spark_find(&b, "Blue");
}

static void new_reply(const char *w, int wn, const char *w2, int w2n)
{
    int f0 = fails;
    CHECK(w2n <= BOARD_MAX);
    CHECK(board_parse(w2, w2n, 0, &b) == BP_OK && board_has_end(w2, w2n));
    CHECK(b.n_usd == 6 && b.n_day == 3 && b.n_money == 3 && b.n_news == 8 && b.has_wx && b.has_sun);   /* the old fields stay */
    STR(b.usd[1].sell, "1.540,00");
    STR(b.money[0].value, "589");
    STR(b.moon, "Waxing crescent moon");
    CHECK(b.has_geo && fabs(b.lat + 34.58) < 1e-9 && fabs(b.lon + 60.94) < 1e-9);
    CHECK(b.has_fx && b.fx_code == 3 && b.fx_cover == 88 && fabs(b.fx_mm - 0.4) < 1e-9 && b.fx_dir == 135 && b.fx_speed == 10 && b.fx_gusts == 22);
    CHECK(b.hours_start == 15 && b.n_hours == 12);
    CHECK(b.hour_temp[0] == 19 && b.hour_temp[3] == 21 && b.hour_temp[11] == 12);
    CHECK(b.hour_pop[0] == 10 && b.hour_pop[3] == 70 && b.hour_pop[11] == 0);
    CHECK(b.hour_code[0] == 3 && b.hour_code[2] == 61 && b.hour_code[11] == 0);
    CHECK(b.n_day_code == 3 && b.day_code[0] == 53 && b.day_code[1] == 3 && b.day_code[2] == 51);
    CHECK(b.n_usd_chg == 6 && b.n_money_chg == 1);
    int found = 0;
    for (int i = 0; i < b.n_usd; i++) found += board_chg_find(&b, 0, b.usd[i].label) != NULL;
    CHECK(found == 6);
    const board_chg *g = board_chg_find(&b, 0, "Blue");
    CHECK(g && g->dir == CHG_FLAT);
    g = board_chg_find(&b, 0, "MEP");
    CHECK(g && g->dir == CHG_UP && !strcmp(g->pct, "+0,3%") && !strcmp(g->abs, "+4,60") && !strcmp(g->when, "12 Oct"));
    g = board_chg_find(&b, 0, "CCL");
    CHECK(g && g->dir == CHG_DOWN && !strcmp(g->pct, "-0,2%") && !strcmp(g->abs, "-3,20"));
    g = board_chg_find(&b, 1, b.money[0].label);
    CHECK(g && g->dir == CHG_UP && !strcmp(g->pct, "+0,2%") && !strcmp(g->abs, "+1") && !strcmp(g->when, "9 Oct"));
    CHECK(!board_chg_find(&b, 1, b.money[1].label) && !board_chg_find(&b, 0, b.money[0].label));   /* no line: no chip */
    const board_spark *sp = board_spark_find(&b, "Blue");
    int lo = 0, hi = 0;
    CHECK(sp && sp->n == 30 && sp->v[0] == 1545 && sp->v[16] == 1565 && sp->v[29] == 1540 && sp->start_day == days_from_civil(2026, 9, 14));
    CHECK(sp && spark_range(sp, &lo, &hi) == 30 && lo == 1540 && hi == 1565);
    CHECK(!board_spark_find(&b, "Oficial"));
    CHECK(round_trips());
    CHECK(wmo_kind(b.fx_code) == WK_CLOUD && wx_icon(wmo_kind(b.day_code[0]), 1) == WI_CLOUD_RAIN && !wmo_wet(b.fx_code));
    char ww[32];
    wind_words(ww, sizeof ww, b.fx_dir, b.fx_gusts);
    STR(ww, "SE, gusts 22");
    CHECK(bounded());
    printf("  %d bytes, %d more than the Worker test's reply\n", w2n, w2n - wn);
    group("the reply with the keys of 0.3.0: every new field, the old ones as before, the chips and the series by label", f0);

    f0 = fails;
    CHECK(board_parse(w2, w2n, 1, &b) == BP_OK);
    STR(b.money[0].label, "Riesgo pais");
    g = board_chg_find(&b, 1, b.money[0].label);
    CHECK(g && g->dir == CHG_UP && !strcmp(g->label, "Riesgo pais"));
    CHECK(board_spark_find(&b, "Blue") && b.n_hours == 12 && b.has_geo);
    group("the same folded to ASCII: the money_chg label still matches its money line", f0);

    f0 = fails;
    char *crlf = malloc((size_t)w2n * 2 + 1);
    int k = 0;
    for (int i = 0; i < w2n; i++) {
        if (w2[i] == '\n') crlf[k++] = '\r';
        crlf[k++] = w2[i];
    }
    crlf[k] = 0;
    CHECK(board_parse(crlf, k, 0, &b) == BP_OK && b.n_usd_chg == 6 && b.n_spark == 1 && b.spark[0].n == 30 && b.n_hours == 12 &&
          b.hour_code[11] == 0 && b.n_day_code == 3 && b.day_code[2] == 51 && b.fx_gusts == 22 && b.has_geo);
    CHECK(board_spark_find(&b, "Blue") && board_spark_find(&b, "Blue")->v[29] == 1540);
    STR(b.money_chg[0].when, "9 Oct");
    free(crlf);
    group("CR LF line ends with the keys of 0.3.0", f0);

    f0 = fails;
    int bad_rc = 0, bad_end = 0, bad_bounds = 0;
    for (int n = 0; n < w2n; n++) {
        int rc = board_parse(w2, n, 0, &b);
        if (n == w2n - 1 ? rc != BP_OK : rc == BP_OK) bad_rc++;
        if (board_has_end(w2, n)) bad_end++;
        if (!bounded()) bad_bounds++;
    }
    CHECK(bad_rc == 0 && bad_end == 0 && bad_bounds == 0);
    printf("  %d cuts\n", w2n);
    group("the reply with the keys of 0.3.0 cut at every byte: BP_CUT until the end line, every field bounded", f0);

    f0 = fails;
    CHECK(board_parse(w, wn, 0, &b) == BP_OK && none_new() && b.n_usd == 6);
    CHECK(parse("couch-board 1\nnow=1791614218\noffset=-10800\ncity=Buenos Aires\nusd=Oficial|1.485,00|1.535,00|Fri 15:00\n"
                "usd_age=42\nusd_state=ok\nwx_now=11\xc2\xb0|10\xc2\xb0|92%|Overcast|9 km/h|night|Open-Meteo\n"
                "day=Today|Rain showers|16\xc2\xb0|11\xc2\xb0|96%\nwx_age=310\nwx_state=ok\nend\n", 0) == BP_OK);
    CHECK(none_new() && b.n_usd == 1 && b.n_day == 1 && !b.is_day && round_trips());
    group("the replies of today's Worker and of 0.1.0's time: no key of 0.3.0, every new field missing", f0);
}

static void new_rules(void)
{
    int f0 = fails;
    CHECK(parse("couch-board 1\ngeo=-34.58|-60.94\nend\n", 0) == BP_OK && b.has_geo);
    static const char *const bad_geo[] = {"geo=-91|0",  "geo=0|181",    "geo=x|1",     "geo=-34.5",  "geo=1.1234567|2", "geo=1|x|3",
                                          "geo=-|-",    "geo=1e3|2",    "geo=0x1|2",   "geo=|",      "geo=1234567890|1", "geo=.|1"};
    for (unsigned i = 0; i < sizeof bad_geo / sizeof bad_geo[0]; i++) {
        char s[96];
        snprintf(s, sizeof s, "couch-board 1\ngeo=1|2\n%s\nend\n", bad_geo[i]);
        CHECK(parse(s, 0) == BP_OK && !b.has_geo && b.lat == 0 && b.lon == 0);   /* a bad line replaces a good one */
    }
    CHECK(parse("couch-board 1\ngeo= 90 | -180 \nend\n", 0) == BP_OK && b.has_geo && b.lat == 90 && b.lon == -180);
    CHECK(parse("couch-board 1\ngeo=-34.5|-60.25|35\nend\n", 0) == BP_OK && b.has_geo && b.lat == -34.5 && b.lon == -60.25);   /* a 3rd field dropped */
    group("geo: lat|lon in the C form, in range, else no place; a 3rd field dropped; a repeated line replaces", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\nwx_fx=-|-|-|-|-|-\nend\n", 0) == BP_OK && b.has_fx && b.fx_code == -1 && b.fx_cover == -1 && b.fx_mm == -1 &&
          b.fx_dir == -1 && b.fx_speed == -1 && b.fx_gusts == -1);
    CHECK(parse("couch-board 1\nwx_fx=3.5|88.4|0|360|10.6|x\nend\n", 0) == BP_OK && b.fx_code == -1 && b.fx_cover == 88 && b.fx_mm == 0 &&
          b.fx_dir == 0 && b.fx_speed == 11 && b.fx_gusts == -1);
    CHECK(parse("couch-board 1\nwx_fx=0000000003|000000088\nend\n", 0) == BP_OK && b.fx_code == -1 && b.fx_cover == 88);   /* 10 digits, 9 */
    CHECK(parse("couch-board 1\nwx_fx=61\nend\n", 0) == BP_OK && b.fx_code == 61 && b.fx_cover == -1 && b.fx_gusts == -1);
    CHECK(parse("couch-board 1\nwx_fx=100|101|1001|361|501|-1\nend\n", 0) == BP_OK && b.fx_code == -1 && b.fx_cover == -1 && b.fx_mm == -1 &&
          b.fx_dir == -1 && b.fx_speed == -1 && b.fx_gusts == -1);
    CHECK(parse("couch-board 1\nwx_fx=95|100|12.5|359.4|80|120|extra\nwx_fx=0\nend\n", 0) == BP_OK && b.fx_code == 0 && b.fx_cover == -1);
    CHECK(parse("couch-board 1\nwx_fx=95|100|12.5|359.4|80|120|extra\nend\n", 0) == BP_OK && b.fx_code == 95 && b.fx_cover == 100 &&
          fabs(b.fx_mm - 12.5) < 1e-9 && b.fx_dir == 359 && b.fx_speed == 80 && b.fx_gusts == 120);
    group("wx_fx: code|cover|mm|dir|speed|gusts, a code whole, the rest rounded, out of range or \"-\" missing (-1)", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\nwx_hours=23|5  -3 - x|10|61 x\nend\n", 0) == BP_OK && b.hours_start == 23 && b.n_hours == 4);
    CHECK(b.hour_temp[0] == 5 && b.hour_temp[1] == -3 && b.hour_temp[2] == TEMP_NONE && b.hour_temp[3] == TEMP_NONE && b.hour_temp[11] == TEMP_NONE);
    CHECK(b.hour_pop[0] == 10 && b.hour_pop[1] == -1 && b.hour_code[0] == 61 && b.hour_code[1] == -1);
    CHECK(parse("couch-board 1\nwx_hours=24|-90 -91 70 71 -0.4 19.5\nend\n", 0) == BP_OK && b.hours_start == -1 && b.n_hours == 6);
    CHECK(b.hour_temp[0] == -90 && b.hour_temp[1] == TEMP_NONE && b.hour_temp[2] == 70 && b.hour_temp[3] == TEMP_NONE && b.hour_temp[4] == 0 &&
          b.hour_temp[5] == 20 && b.hour_pop[0] == -1 && b.hour_code[0] == -1);
    static char s[8192];
    int k = snprintf(s, sizeof s, "couch-board 1\nwx_hours=0|");
    for (int i = 0; i < 14; i++) k += snprintf(s + k, sizeof s - k, "%d ", i);
    snprintf(s + k, sizeof s - k, "|1 2 3|4 5 6\nwx_hours=7|1\nend\n");
    CHECK(parse(s, 0) == BP_OK && b.hours_start == 7 && b.n_hours == 1 && b.hour_temp[0] == 1 && b.hour_temp[1] == TEMP_NONE &&
          b.hour_pop[0] == -1 && b.hour_code[2] == -1);   /* the second line replaces the first whole */
    CHECK(parse("couch-board 1\nwx_hours=0|0 1 2 3 4 5 6 7 8 9 10 11 12 13\nend\n", 0) == BP_OK && b.n_hours == HOURS_MAX && b.hour_temp[11] == 11);
    group("wx_hours: start|temps|chances|codes, at most 12 each, runs of spaces, a bad word missing, a repeated line replaces", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\ndays_fx=53|-|99|1\nend\n", 0) == BP_OK && b.n_day_code == 3 && b.day_code[0] == 53 && b.day_code[1] == -1 &&
          b.day_code[2] == 99);
    CHECK(parse("couch-board 1\ndays_fx=53|3|51\ndays_fx=1\nend\n", 0) == BP_OK && b.n_day_code == 1 && b.day_code[0] == 1 && b.day_code[1] == -1);
    CHECK(parse("couch-board 1\ndays_fx=5.0|100|-1\nend\n", 0) == BP_OK && b.day_code[0] == -1 && b.day_code[1] == -1 && b.day_code[2] == -1);
    group("days_fx: a WMO code per day line, at most 3, a bad code missing", f0);

    f0 = fails;
    k = snprintf(s, sizeof s, "couch-board 1\n");
    for (int i = 0; i < 10; i++) k += snprintf(s + k, sizeof s - k, "usd_chg=R%d|%s|+%d,0%%|+%d|12 Oct\n", i, i % 2 ? "up" : "down", i, i);
    for (int i = 0; i < 6; i++) k += snprintf(s + k, sizeof s - k, "money_chg=M%d|flat|0,0%%|0|9 Oct\n", i);
    snprintf(s + k, sizeof s - k, "usd_chg=R0|up|+9,9%%|+9|1 Oct\nend\n");
    CHECK(parse(s, 0) == BP_OK && b.n_usd_chg == BOARD_USD && b.n_money_chg == BOARD_MONEY && bounded());
    CHECK(board_chg_find(&b, 0, "R7") && !board_chg_find(&b, 0, "R8") && board_chg_find(&b, 1, "M3") && !board_chg_find(&b, 1, "M4"));
    CHECK(board_chg_find(&b, 0, "R0")->dir == CHG_DOWN && board_chg_find(&b, 1, "M0")->dir == CHG_FLAT);   /* the first match counts */
    CHECK(parse("couch-board 1\nusd_chg=A|UP\nusd_chg=B|sideways|1|2|3\nusd_chg=C\nusd_chg=D|flat|\nend\n", 0) == BP_OK && b.n_usd_chg == 4);
    CHECK(b.usd_chg[0].dir == CHG_NONE && b.usd_chg[1].dir == CHG_NONE && b.usd_chg[2].dir == CHG_NONE && b.usd_chg[3].dir == CHG_FLAT);
    STR(b.usd_chg[0].pct, "-");
    STR(b.usd_chg[2].when, "-");
    STR(b.usd_chg[3].pct, "-");
    CHECK(!board_chg_find(&b, 0, "E") && !board_chg_find(&b, 1, "A"));
    CHECK(parse("couch-board 1\nusd=D\xc3\xb3lar de una casa con un nombre largo|1|2|3\nusd_chg=D\xc3\xb3lar de una casa con un nombre largo|up|+1%|+1|1 Oct\n"
                "spark=D\xc3\xb3lar de una casa con un nombre largo|2026-09-14|1\nend\n", 0) == BP_OK);
    CHECK(strlen(b.usd[0].label) < 24 && board_chg_find(&b, 0, b.usd[0].label) && board_spark_find(&b, b.usd[0].label));   /* cut alike */
    group("usd_chg and money_chg: label|dir|pct|abs|date, at most 8 and 4, up/down/flat else no chip, missing fields \"-\"", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\nspark=Blue|2026-02-30|1 - 3 1.6 x -5\nend\n", 0) == BP_OK && b.n_spark == 1);
    CHECK(b.spark[0].start_day == -1 && b.spark[0].n == 6 && b.spark[0].v[0] == 1 && b.spark[0].v[1] == -1 && b.spark[0].v[2] == 3 &&
          b.spark[0].v[3] == 2 && b.spark[0].v[4] == -1 && b.spark[0].v[5] == -1 && b.spark[0].v[6] == -1);
    int lo = 0, hi = 0;
    CHECK(spark_range(&b.spark[0], &lo, &hi) == 3 && lo == 1 && hi == 3);
    k = snprintf(s, sizeof s, "couch-board 1\nspark=A|2024-02-29|");
    for (int i = 0; i < 40; i++) k += snprintf(s + k, sizeof s - k, "%d ", 1000 + i);
    snprintf(s + k, sizeof s - k, "\nspark=B|2023-02-29|- -\nspark=C|x|1\nspark=D\nend\n");
    CHECK(parse(s, 0) == BP_OK && b.n_spark == BOARD_SPARKS && bounded());
    CHECK(b.spark[0].n == SPARK_MAX && b.spark[0].v[SPARK_MAX - 1] == 1000 + SPARK_MAX - 1 && b.spark[0].start_day == days_from_civil(2024, 2, 29));
    CHECK(b.spark[1].start_day == -1 && b.spark[1].n == 2 && spark_range(&b.spark[1], &lo, &hi) == 0 && !board_spark_find(&b, "C"));
    CHECK(parse("couch-board 1\nspark=D\nend\n", 0) == BP_OK && b.spark[0].n == 0 && b.spark[0].start_day == -1);
    civil_time ct;
    civil_from_unix(days_from_civil(2026, 9, 14) * 86400L, &ct);
    CHECK(ct.year == 2026 && ct.month == 9 && ct.day == 14 && days_from_civil(1970, 1, 1) == 0 && days_from_civil(2000, 3, 1) == 11017);
    group("spark: label|date|values, at most 2 lines of 31, \"-\" or a bad word missing, a bad date -1; spark_range", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\nspark=Blue|2026-09-14|1540\nusd_chg=Blue|up|+1%|+1|12 Oct\nwx_hours=1|2\ngeo=1|2\nwx_fx=3\ndays_fx=4\n"
                "usd=Blue|1|2|3\nend\n", 0) == BP_OK && b.n_usd == 1 && b.n_spark == 1 && b.n_usd_chg == 1 && b.has_geo && b.has_fx &&
          b.n_hours == 1 && b.day_code[0] == 4);
    CHECK(parse("couch-board 1\nend\ngeo=1|2\nspark=Blue|2026-09-14|1\n", 0) == BP_OK && none_new());
    group("the keys of 0.3.0 in any order, none after the end line", f0);
}

static void numbers(void)
{
    int f0 = fails;
    static const char *const rt[] = {"1.485,00", "1.540,00", "1.995,50", "589",  "1,7%", "19,25%", "+0,3%", "-0,2%",  "0,00",
                                     "+4,60",    "-3,20",    "+1",       "0,0%", "-0,0%", "+0,0%", "0",     "1.234.567,8901", "999.999.999.999"};
    char o[40];
    board_num x, y, z;
    for (unsigned i = 0; i < sizeof rt / sizeof rt[0]; i++) {
        CHECK(num_parse(rt[i], &x));
        num_format(o, sizeof o, &x);
        STR(o, rt[i]);
    }
    CHECK(num_parse("1.540,00", &x) && x.v == 154000 && x.dec == 2 && !x.pct && !x.plus && !x.neg);
    CHECK(num_parse("-3,20", &x) && x.v == -320 && x.dec == 2 && x.neg);
    CHECK(num_parse("19,25%", &x) && x.v == 1925 && x.pct);
    CHECK(num_parse("1540,00", &x) && x.v == 154000);
    num_format(o, sizeof o, &x);
    STR(o, "1.540,00");                              /* the Worker groups from 1.000 */
    static const char *const bad[] = {"-",  "",        "1,2,3", "1.23",  "1.2345", "12.34,5", "1234.567",   ",5",   "1,",   "1,23456", "1 000",
                                      "abc", "+-1",    "1%%",   "%",     "+",      "1.540,00 ", "1234567890123", "1.234.567.890.123", ".540"};
    for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) CHECK(!num_parse(bad[i], &x));
    group("num_parse and num_format: every value form of the Worker, back to the same text; not numbers", f0);

    f0 = fails;
    num_parse("1.540,00", &x);
    num_parse("1.565,00", &y);
#define MIX(a, bb, t, want)                \
    do {                                   \
        num_mix((a), (bb), (t), &z);       \
        num_format(o, sizeof o, &z);       \
        STR(o, (want));                    \
    } while (0)
    MIX(&x, &y, 0, "1.540,00");
    MIX(&x, &y, 0.5, "1.552,50");
    MIX(&x, &y, 1, "1.565,00");
    MIX(&x, &y, 2, "1.565,00");
    MIX(&x, &y, -1, "1.540,00");
    MIX(&x, &y, NAN, "1.565,00");
    num_parse("589", &x);
    num_parse("601,5", &y);
    MIX(&x, &y, 0.5, "595,3");
    num_parse("1,7%", &x);
    num_parse("2,25%", &y);
    MIX(&x, &y, 0, "1,70%");
    num_parse("-0,3%", &x);
    num_parse("+0,2%", &y);
    MIX(&x, &y, 0, "-0,3%");
    MIX(&x, &y, 0.6, "+0,0%");
    MIX(&x, &y, 1, "+0,2%");
    num_parse("1.540,55", &x);
    num_parse("1.600", &y);
    MIX(&x, &y, 0, "1.541");
    num_parse("-0,0%", &y);
    MIX(&y, &y, 1, "-0,0%");
#undef MIX
    group("num_mix: the counting value, the end value's decimals, '%' and sign", f0);

    f0 = fails;
    static const struct { const char *s; int m; } hm[] = {{"06:24", 384}, {"6:24", 384}, {"19:16", 1156}, {"00:00", 0},  {"23:59", 1439},
                                                         {"24:00", -1},  {"06:60", -1}, {"06:2", -1},    {"-", -1},     {"", -1},
                                                         {"006:24", -1}, {"06:24 ", -1}, {":24", -1},    {"6h24", -1}};
    for (unsigned i = 0; i < sizeof hm / sizeof hm[0]; i++) CHECK(hm_parse(hm[i].s) == hm[i].m);
    group("hm_parse", f0);
}

static void sky(void)
{
    int f0 = fails;
    static const struct { double e; unsigned top, hor; } st[] = {{-12, 0x050a18, 0x0d1a33}, {-9, 0x0a1530, 0x1f2d55}, {-3, 0x1b2550, 0xc8644a},
                                                                 {3, 0x3a5fa0, 0xf2a65a},   {13, 0x2f6fc0, 0x8fc3ec}, {20, 0x2a66c4, 0x9fd0f0},
                                                                 {-40, 0x050a18, 0x0d1a33}, {NAN, 0x050a18, 0x0d1a33}, {90, 0x2a66c4, 0x9fd0f0},
                                                                 {0, 0x2b4278, 0xdd8552}};
    unsigned t, h, t2, h2;
    for (unsigned i = 0; i < sizeof st / sizeof st[0]; i++) {
        sky_colors(st[i].e, -1, 0, &t, &h);
        CHECK(t == st[i].top && h == st[i].hor);
        sky_colors(st[i].e, 0, 0, &t2, &h2);
        CHECK(t2 == t && h2 == h);
    }
    sky_colors(-20, 100, 0, &t, &h);
    CHECK(t == 0x434e61);                            /* night toward 5d6b80 by 0.7 */
    sky_colors(-20, 0, 1, &t, &h);
    CHECK(t == 0x040812);                            /* night darkened by 25 % */
    sky_colors(30, 100, 1, &t, &h);
    sky_colors(30, 100, 0, &t2, &h2);
    CHECK(t < t2 && h < h2);
    int mono = 1;
    unsigned pt = 0;
    for (double e = -12; e <= 20; e += 0.25) {         /* the top's blue never falls on the way up */
        sky_colors(e, -1, 0, &t, &h);
        if ((t & 0xff) < (pt & 0xff)) mono = 0;
        pt = t;
    }
    CHECK(mono);
    group("sky_colors: the stops, clamped, between, the cover's grey, the rain's 25 %", f0);

    f0 = fails;
    int bad = 0;
    for (int c = -1; c <= 99; c++) {
        int want = c < 0 ? WK_NONE : c <= 1 ? WK_CLEAR : c == 2 ? WK_PARTLY : c == 3 ? WK_CLOUD : (c == 45 || c == 48) ? WK_FOG
                 : ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) ? WK_RAIN : ((c >= 71 && c <= 77) || c == 85 || c == 86) ? WK_SNOW
                 : c >= 95 ? WK_STORM : WK_CLOUD;
        if (wmo_kind(c) != want) bad++;
        if (wmo_wet(c) != (want == WK_RAIN || want == WK_STORM)) bad++;
    }
    CHECK(!bad);
    CHECK(wx_icon(WK_CLEAR, 1) == WI_SUN && wx_icon(WK_CLEAR, 0) == WI_MOON_STARS && wx_icon(WK_PARTLY, 1) == WI_CLOUD_SUN &&
          wx_icon(WK_PARTLY, 0) == WI_CLOUD_MOON && wx_icon(WK_CLOUD, 0) == WI_CLOUD && wx_icon(WK_FOG, 1) == WI_CLOUD_FOG &&
          wx_icon(WK_RAIN, 0) == WI_CLOUD_RAIN && wx_icon(WK_SNOW, 1) == WI_CLOUD_SNOW && wx_icon(WK_STORM, 0) == WI_CLOUD_LIGHTNING &&
          wx_icon(WK_NONE, 1) == WI_NONE && wmo_kind(100) == WK_CLOUD);
    group("wmo_kind, wmo_wet and wx_icon: the design's table for every code", f0);

    f0 = fails;
    static const struct { int d; const char *w; } cp[] = {{0, "N"},  {22, "N"},   {23, "NE"}, {67, "NE"}, {68, "E"},  {135, "SE"},
                                                         {180, "S"}, {225, "SW"}, {270, "W"}, {315, "NW"}, {337, "NW"}, {338, "N"},
                                                         {359, "N"}, {720, "N"},  {-1, ""}};
    for (unsigned i = 0; i < sizeof cp / sizeof cp[0]; i++) STR(wind_compass(cp[i].d), cp[i].w);
    char w[32];
    wind_words(w, sizeof w, 135, 22);
    STR(w, "SE, gusts 22");
    wind_words(w, sizeof w, 135, -1);
    STR(w, "SE");
    wind_words(w, sizeof w, -1, 30);
    STR(w, "Gusts 30");
    wind_words(w, sizeof w, -1, -1);
    STR(w, "");
    group("wind_compass and wind_words", f0);
}

/* the separation in degrees of two places in the sky */
static double sep(double e1, double a1, double e2, double a2)
{
    const double r = 3.14159265358979323846 / 180;
    double c = sin(e1 * r) * sin(e2 * r) + cos(e1 * r) * cos(e2 * r) * cos((a1 - a2) * r);
    return acos(c > 1 ? 1 : c < -1 ? -1 : c) / r;
}

static double adiff(double a, double b2)
{
    double d = fmod(a - b2, 360.0);
    if (d < 0) d += 360.0;
    return d > 180 ? 360 - d : d;
}

static void sun_check(void)
{
    int f0 = fails;
    /* Open-Meteo's sunrises and sunsets at Junin (worker/test/fixtures/meteo_junin.json, lat -34.55, lon
     * -60.94, UTC-3, whole minutes): the sun's centre is at -0.833 deg (refraction and the half disc). */
    static const long ev[8] = {1791883440L, 1791929760L, 1791969720L, 1792016220L, 1792056060L, 1792102680L, 1792142400L, 1792189080L};
    double worst = 0;
    for (int i = 0; i < 8; i++) {
        double e, a;
        sun_pos(ev[i], -34.55, -60.94, &e, &a);
        if (fabs(e + 0.833) > worst) worst = fabs(e + 0.833);
        CHECK(fabs(e + 0.833) < 0.5);
        CHECK(i % 2 ? a > 254 && a < 265 : a > 95 && a < 106);   /* October: the sun rises south of east */
    }
    printf("  the sunrises and sunsets: at most %.2f deg from -0.833\n", worst);
    /* NOAA's full algorithm (after Meeus), computed apart, at Junin (the Worker's LAT and LON): the times in
     * UTC, the elevation, the azimuth. The short formula is up to 0.4 deg off it (0.38 at the equinox). */
    static const struct { long t; double e, a; } pos[] = {
        {1791913620L, 52.18, 307.85},   /* 2026-10-13 14:47 local, the Worker test's now */
        {1791892800L, 31.07, 77.24},    /* 2026-10-13 09:00 */
        {1797868800L, 78.85, 2.28},     /* 2026-12-21 13:00, the solstice: high and north */
        {1782057000L, 31.86, 4.22},     /* 2026-06-21 12:50 */
        {1774021500L, 54.86, 11.41},    /* 2026-03-20 12:45, the equinox */
        {1791943800L, -41.22, 213.74},  /* 2026-10-13 23:10 */
        {1798837200L, 26.18, 259.10}};  /* 2027-01-01 18:00 */
    worst = 0;
    double worst_a = 0;
    for (unsigned i = 0; i < sizeof pos / sizeof pos[0]; i++) {
        double e, a;
        sun_pos(pos[i].t, -34.58, -60.94, &e, &a);
        double da = adiff(a, pos[i].a) * cos(pos[i].e * 3.14159265358979323846 / 180);   /* an azimuth step on the sky */
        CHECK(fabs(e - pos[i].e) < 0.5 && da < 0.5);
        if (fabs(e - pos[i].e) > worst) worst = fabs(e - pos[i].e);
        if (da > worst_a) worst_a = da;
    }
    printf("  the table: at most %.2f deg in elevation, %.2f deg along the horizon\n", worst, worst_a);
    group("sun_pos: Open-Meteo's sunrises and sunsets at Junin, a table of NOAA's full algorithm", f0);

    f0 = fails;
    double fr;
    long nx;
    CHECK(sun_arc(1791913620L, -10800, 384, 1156, &fr, &nx) == 1 && fabs(fr - 503.0 / 772) < 1e-9 && nx == 269 * 60);   /* 14:47 */
    CHECK(sun_arc(1791943800L, -10800, 384, 1156, &fr, &nx) == 0 && fabs(fr - 234.0 / 668) < 1e-9 && nx == 434 * 60);   /* 23:10 */
    CHECK(sun_arc(1791871200L, -10800, 384, 1156, &fr, &nx) == 0 && fabs(fr - 464.0 / 668) < 1e-9 && nx == 204 * 60);   /* 03:00 */
    CHECK(sun_arc(1791883440L, -10800, 384, 1156, &fr, &nx) == 1 && fr == 0 && nx == 772 * 60);                          /* the rise */
    CHECK(sun_arc(1791929760L, -10800, 384, 1156, &fr, &nx) == 0 && fr == 0 && nx == 668 * 60);                          /* the set */
    CHECK(sun_arc(1791913620L, -10800, -1, 1156, &fr, &nx) == -1 && sun_arc(1791913620L, -10800, 600, 600, &fr, &nx) == -1 &&
          sun_arc(1791913620L, -10800, 384, 1440, &fr, &nx) == -1 && fr == 0 && nx == 0);
    CHECK(sun_arc(-100, 0, 384, 1156, &fr, &nx) == 0);   /* before 1970: the local day still wraps */
    char s[32];
    age_words(s, sizeof s, 269 * 60);
    STR(s, "4 h 29 min");
    group("sun_arc: the day's and the night's fraction, the time to the set or the rise, no data", f0);
}

static void moon_check(const char *w, int wn)
{
    int f0 = fails;
    CHECK(fabs(moon_age(MOON_NEW_2000)) < 1e-9 && fabs(moon_age(MOON_NEW_2000 + 86400) - 1) < 1e-9);
    CHECK(fabs(moon_age(MOON_NEW_2000 - 86400) - (MOON_SYNODIC - 1)) < 1e-6);
    CHECK(moon_phase(0) == 0 && moon_phase(MOON_SYNODIC / 2) == 4 && moon_phase(MOON_SYNODIC - 0.1) == 0 && moon_phase(1.84) == 0 &&
          moon_phase(1.86) == 1);
    CHECK(fabs(moon_lit(0)) < 1e-12 && fabs(moon_lit(MOON_SYNODIC / 2) - 1) < 1e-12 && fabs(moon_lit(MOON_SYNODIC / 4) - 0.5) < 1e-12);
    /* the Worker's words for the reply's now: the same formula */
    CHECK(board_parse(w, wn, 0, &b) == BP_OK);
    double age = moon_age(b.now);
    int p = moon_phase(age);
    char word[40];
    snprintf(word, sizeof word, "%s%s", moon_phase_name[p], strstr(moon_phase_name[p], "moon") ? "" : " moon");
    STR(word, b.moon);
    CHECK(p == 1 && fabs(age - 2.3563) < 1e-3 && fabs(moon_lit(age) - 0.0615) < 1e-3);
    int full;
    long t = moon_next(1791913620L, &full);
    CHECK(full && labs(t - 1792985754L) <= 1);
    char s[32];
    moon_next_words(s, sizeof s, 1791913620L, -10800);
    STR(s, "Full moon 26 Oct");
    moon_next_words(s, sizeof s, 1792987920L, -10800);
    STR(s, "New moon 9 Nov");
    group("moon_age, moon_phase, moon_lit, moon_next: the Worker's formula and words", f0);

    f0 = fails;
    /* The 2026 new and full moons, UTC (the four eclipses, and October's two). At a new moon the moon's ecliptic
     * longitude is the sun's, at a full moon the sun's + 180; at an eclipse the moon is near the ecliptic. */
    static const struct { long t; int full, eclipse; } ph[] = {
        {1771329660L, 0, 1},   /* 2026-02-17 12:01, the annular eclipse */
        {1772537580L, 1, 1},   /* 2026-03-03 11:33, the total lunar eclipse */
        {1786556760L, 0, 1},   /* 2026-08-12 17:46, the total eclipse */
        {1787890380L, 1, 1},   /* 2026-08-28 04:13, the partial lunar eclipse */
        {1791647400L, 0, 0},   /* 2026-10-10 15:50 */
        {1792987920L, 1, 0}};  /* 2026-10-26 04:12 */
    const double r = 3.14159265358979323846 / 180;
    double wl = 0, wa = 0, ws = 0;
    for (unsigned i = 0; i < sizeof ph / sizeof ph[0]; i++) {
        double l, bt, par, n = ((double)ph[i].t - 946728000.0) / 86400.0;
        double g = (357.528 + 0.9856003 * n) * r, sl = 280.460 + 0.9856474 * n + 1.915 * sin(g) + 0.020 * sin(2 * g);   /* the sun's */
        moon_ecl(ph[i].t, &l, &bt, &par);
        double dl = adiff(l, sl + (ph[i].full ? 180 : 0));
        CHECK(dl < 0.5);
        CHECK(!ph[i].eclipse || fabs(bt) < 1.2);
        CHECK(par > 0.89 && par < 1.02);
        double a = moon_age(ph[i].t), da = ph[i].full ? fabs(a - MOON_SYNODIC / 2) : (a < MOON_SYNODIC / 2 ? a : MOON_SYNODIC - a);
        CHECK(da < 0.75);                            /* the mean month is within 0.75 day of the true one */
        if (dl > wl) wl = dl;
        if (da > wa) wa = da;
        if (ph[i].eclipse) {                         /* seen from Junin: the moon on the sun, or on the point opposite */
            double se, sa, me, ma;
            sun_pos(ph[i].t, -34.58, -60.94, &se, &sa);
            moon_pos(ph[i].t, -34.58, -60.94, &me, &ma);
            double d = ph[i].full ? sep(me, ma, -se, sa + 180) : sep(me, ma, se, sa);
            CHECK(d < 2.0);
            if (d > ws) ws = d;
        }
    }
    printf("  at most %.2f deg from the sun's longitude (+ 180), %.2f day from the mean age, %.2f deg on the sky\n", wl, wa, ws);
    group("moon_ecl, moon_pos and moon_age at the 2026 eclipses and October's new and full moon", f0);
}

static void fold(void)
{
    int f0 = fails;
    char o[64];
#define F(in, size, fl, want)                                     \
    do {                                                          \
        text_field(o, (size), (in), (int)strlen(in), (fl));       \
        STR(o, (want));                                           \
    } while (0)
    F("\xc3\x91" "and\xc3\xba y C\xc3\xad" "a", 64, 1, "Nandu y Cia");
    F("11\xc2\xb0", 64, 1, "11");
    F("\xe2\x82\xac 5", 64, 1, "? 5");
    F("x\xff" "y", 64, 1, "x?y");
    F("x\xc3", 64, 1, "x?");                         /* a cut sequence */
    F("\xc0\xaf", 64, 1, "??");                      /* an overlong form */
    F("\xed\xa0\x80", 64, 1, "???");                 /* a surrogate */
    F("\xc2\xa0x", 64, 1, "x");                      /* a no-break space at the start */
    F("a\xc2\xa0" "b", 64, 1, "a b");
    F("2\xc3\x97" "3", 64, 1, "2?3");
    F("\xc3\x9f" "\xc3\xbf" "\xc3\x86", 64, 1, "syA");
    F("Ca\xc3\xb1uelas", 64, 0, "Ca\xc3\xb1uelas");   /* no fold: the bytes stay */
    F("a\xc3\xb1" "b", 4, 0, "a\xc3\xb1");
    F("a\xc3\xb1" "b", 3, 0, "a");                   /* the cut does not split the sequence */
    F("\xe2\x82\xac", 3, 0, "");
    F("  x  ", 64, 0, "x");
    F("", 64, 0, "");
#undef F
    group("text_field: the fold to ASCII, the code point cut", f0);
}

static void http(void)
{
    int f0 = fails, st, ch;
    char loc[32];
    const char *ok = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 479\r\nConnection: close\r\n\r\ncouch-board 1\n";
    int hl = http_head(ok, (int)strlen(ok), &st, loc, sizeof loc, &ch);
    CHECK(hl == (int)(strstr(ok, "couch-board") - ok) && st == 200 && !ch && !loc[0]);
    const char *r = "HTTP/1.1 301 Moved Permanently\r\nlocation: https://couch.daddiego.com.ar/board\r\n\r\n";
    CHECK(http_head(r, (int)strlen(r), &st, loc, sizeof loc, &ch) == (int)strlen(r) && st == 301);
    STR(loc, "https://couch.daddiego.com.ar/b");  /* cut to the buffer: 31 bytes and the NUL */
    const char *c = "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n";
    CHECK(http_head(c, (int)strlen(c), &st, loc, sizeof loc, &ch) > 0 && ch);
    CHECK(http_head("HTTP/1.1 200 OK\r\nContent", 25, &st, loc, sizeof loc, &ch) == 0);
    CHECK(http_head("HTT", 3, &st, loc, sizeof loc, &ch) == 0);
    CHECK(http_head("<html>\n\n", 8, &st, loc, sizeof loc, &ch) == -1);
    CHECK(http_head("HTTP/1.1 abc\r\n\r\n", 16, &st, loc, sizeof loc, &ch) == -1);
    const char *lf = "HTTP/1.0 503 Service Unavailable\nX: y\n\nbody";
    CHECK(http_head(lf, (int)strlen(lf), &st, loc, sizeof loc, &ch) == (int)(strstr(lf, "body") - lf) && st == 503);
    group("http_head: the status, Location, chunked, an incomplete head, not HTTP, bare LF", f0);

    f0 = fails;
    char host[96], path[160];
    int port;
    CHECK(url_split("http://couch.daddiego.com.ar/board", host, sizeof host, &port, path, sizeof path) == 0 && port == 80);
    STR(host, "couch.daddiego.com.ar");
    STR(path, "/board");
    CHECK(url_split("http://192.168.0.3:8787/board", host, sizeof host, &port, path, sizeof path) == 0 && port == 8787);
    STR(host, "192.168.0.3");
    CHECK(url_split("192.168.0.3:8787", host, sizeof host, &port, path, sizeof path) == 0 && port == 8787);
    STR(path, "/");
    CHECK(url_split("HTTP://X/Y", host, sizeof host, &port, path, sizeof path) == 0);
    CHECK(url_split("http://h/p?u=http://x", host, sizeof host, &port, path, sizeof path) == 0);
    STR(path, "/p?u=http://x");
    CHECK(url_split("https://couch.daddiego.com.ar/board", host, sizeof host, &port, path, sizeof path) == -2);
    CHECK(url_split("ftp://x/", host, sizeof host, &port, path, sizeof path) == -2);
    CHECK(url_split("http://", host, sizeof host, &port, path, sizeof path) == -1);
    CHECK(url_split("http://a:0/", host, sizeof host, &port, path, sizeof path) == -1);
    CHECK(url_split("http://a:65536/", host, sizeof host, &port, path, sizeof path) == -1);
    CHECK(url_split("http://a:80x/", host, sizeof host, &port, path, sizeof path) == -1);
    CHECK(url_split("http://a:-1/", host, sizeof host, &port, path, sizeof path) == -1);
    CHECK(url_split("http://a b/", host, sizeof host, &port, path, sizeof path) == -1);
    CHECK(url_split("http://a/b c", host, sizeof host, &port, path, sizeof path) == -1);
    char longhost[120];
    memset(longhost, 'h', 110);
    longhost[110] = 0;
    CHECK(url_split(longhost, host, sizeof host, &port, path, sizeof path) == -1);
    group("url_split: the default URL, a port, no path, another scheme, bad ports, spaces, a long host", f0);
}

static void clock_words_check(void)
{
    int f0 = fails;
    civil_time t;
    civil_from_unix(0, &t);
    CHECK(t.year == 1970 && t.month == 1 && t.day == 1 && t.wday == 4 && !t.hour && !t.min && !t.sec);
    civil_from_unix(951782400L, &t);
    CHECK(t.year == 2000 && t.month == 2 && t.day == 29 && t.wday == 2);
    civil_from_unix(1791614218L, &t);
    CHECK(t.year == 2026 && t.month == 10 && t.day == 10 && t.wday == 6 && t.hour == 6 && t.min == 36 && t.sec == 58);
    civil_from_unix(1791614218L - 10800, &t);
    CHECK(t.day == 10 && t.hour == 3 && t.min == 36);
    civil_from_unix(1791460801L, &t);
    CHECK(t.year == 2026 && t.month == 10 && t.day == 8 && t.wday == 4 && t.hour == 12 && t.sec == 1);
    civil_from_unix(-1, &t);
    CHECK(t.year == 1969 && t.month == 12 && t.day == 31 && t.wday == 3 && t.hour == 23 && t.sec == 59);
    civil_from_unix(2147483648L, &t);
    CHECK(t.year == 2038 && t.month == 1 && t.day == 19 && t.wday == 2 && t.hour == 3 && t.min == 14 && t.sec == 8);
    STR(weekday_name[6], "Saturday");
    STR(month_name[9], "October");
    char s[16];
    const char *ap;
    clock_hm(s, sizeof s, 0, 5, 0, &ap);
    CHECK(!strcmp(s, "12:05") && ap && !strcmp(ap, "AM"));
    clock_hm(s, sizeof s, 12, 0, 0, &ap);
    CHECK(!strcmp(s, "12:00") && !strcmp(ap, "PM"));
    clock_hm(s, sizeof s, 13, 7, 0, &ap);
    CHECK(!strcmp(s, "1:07") && !strcmp(ap, "PM"));
    clock_hm(s, sizeof s, 9, 5, 1, &ap);
    CHECK(!strcmp(s, "09:05") && !ap);
    utc_label(s, sizeof s, -10800);
    STR(s, "UTC-3");
    utc_label(s, sizeof s, 19800);
    STR(s, "UTC+5:30");
    utc_label(s, sizeof s, -12600);
    STR(s, "UTC-3:30");
    utc_label(s, sizeof s, 0);
    STR(s, "UTC");
    int off = 1;
    CHECK(tz_offset(-180, 0, &off) && off == -10800);
    CHECK(tz_offset(-180, 1, &off) && off == -7200);
    CHECK(tz_offset(840, 0, &off) && off == 50400);
    off = 7;
    CHECK(!tz_offset(-721, 0, &off) && !tz_offset(841, 0, &off) && off == 7);
    group("the clock: dates and weekdays, 12 and 24 hours, the UTC label, the TIMEZONE range", f0);

    f0 = fails;
    static const struct { long s; const char *w; } ages[] = {{42, "42 s"},     {59, "59 s"},       {60, "1 min"},   {300, "5 min"},
                                                             {3600, "1 h"},    {4000, "1 h 6 min"}, {86400, "1 d"}, {200000, "2 d 7 h"},
                                                             {-5, "0 s"}};
    for (unsigned i = 0; i < sizeof ages / sizeof ages[0]; i++) {
        age_words(s, sizeof s, ages[i].s);
        STR(s, ages[i].w);
    }
    char e[24];
    STR(net_err_words(61, e, sizeof e), "refused");
    STR(net_err_words(999, e, sizeof e), "error 999");
    group("age_words and net_err_words", f0);

    f0 = fails;
    static const struct { const char *t; int v; } au[] = {{"", -1},     {"0", -1},  {"00\n", -1}, {"\n", -1},    {" 150\n", 150},
                                                          {"sixty", 0}, {"5", 60},  {"9999", 600}, {"60 a", 0}, {"m 60", 0}};
    for (unsigned i = 0; i < sizeof au / sizeof au[0]; i++) CHECK(auto_parse(au[i].t, (int)strlen(au[i].t)) == au[i].v);
    CHECK(board_has_end("x\nend\n", 6) && board_has_end("x\nend\r\n", 7));
    CHECK(!board_has_end("x\nend", 5) && !board_has_end("end\n", 4) && !board_has_end("x\nended\n", 8));
    group("auto_parse and board_has_end", f0);
}

/* Mutated replies: random bytes changed, '|', '=' and LF put in, cuts. */
static void fuzz(const char *w, int wn, const char *name)
{
    int f0 = fails, bad = 0;
    char *s = malloc((size_t)wn + 64);
    unsigned x = 12345;
    for (int it = 0; it < 20000; it++) {
        memcpy(s, w, (size_t)wn);
        int n = wn;
        int edits = 1 + it % 8;
        for (int e = 0; e < edits; e++) {
            x = x * 1103515245u + 12345u;
            int pos = (int)((x >> 8) % (unsigned)n);
            static const char put[] = "|=\n\xc3\xb0\x00\xff\r e";
            s[pos] = (x >> 4) & 1 ? put[(x >> 16) % (sizeof put - 1)] : (char)(x >> 20);
        }
        if (it % 3 == 0) n = (int)((x >> 3) % (unsigned)n);
        board_parse(s, n, it & 1, &b);
        if (!bounded()) bad++;
        board_has_end(s, n);
    }
    free(s);
    CHECK(bad == 0);
    char g[160];
    snprintf(g, sizeof g, "20,000 mutated replies of %s: every field ends inside its buffer, every number in its range", name);
    group(g, f0);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: board_check <the Worker test's reply> <the same with the keys of 0.3.0> [fuzz]\n");
        return 1;
    }
    int wn, w2n;
    char *w = read_file(argv[1], &wn), *w2 = read_file(argv[2], &w2n);
    worker_reply(w, wn);
    rules();
    extras();
    new_reply(w, wn, w2, w2n);
    new_rules();
    numbers();
    sky();
    sun_check();
    moon_check(w, wn);
    fold();
    http();
    clock_words_check();
    if (argc > 3 && !strcmp(argv[3], "fuzz")) {
        fuzz(w, wn, "the Worker test's reply");
        fuzz(w2, w2n, "the reply with the keys of 0.3.0");
    }
    free(w);
    free(w2);
    printf("%d failed\n", fails);
    return fails > 125 ? 125 : fails;
}
