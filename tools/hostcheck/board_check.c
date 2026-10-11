/* Couch's host check of source/parse.c (tools/hostcheck/run.zsh): the Board
 * parser against the Worker test's reply (argv[1], the expected text of
 * couch/worker/test/board.test.js's first test, which run.zsh extracts),
 * that reply cut at every byte, with CR LF, as version 2, with long fields,
 * at 4 KB, with too many lines, the states and the missing fields; the
 * fold; the HTTP head, the URL, the clock, the ages, auto.txt. With argv[2]
 * "fuzz", also 20,000 mutated replies (a sanitizer build: no crash, every
 * field ends inside its buffer). Prints PASS or FAIL per group; the exit
 * status is the number of failed checks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parse.h"

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

/* Every text field of b ends inside its buffer. */
static int bounded(void)
{
#define IN(f) (memchr((f), 0, sizeof(f)) != NULL)
    int ok = IN(b.city) && IN(b.temp) && IN(b.feels) && IN(b.humidity) && IN(b.words) && IN(b.wind) && IN(b.usd_feed.state) &&
             IN(b.usd_feed.reason) && IN(b.wx_feed.state) && IN(b.wx_feed.reason) && b.n_usd <= BOARD_USD && b.n_day <= BOARD_DAYS;
    for (int i = 0; i < b.n_usd; i++) ok = ok && IN(b.usd[i].label) && IN(b.usd[i].buy) && IN(b.usd[i].sell) && IN(b.usd[i].when);
    for (int i = 0; i < b.n_day; i++)
        ok = ok && IN(b.day[i].label) && IN(b.day[i].words) && IN(b.day[i].max) && IN(b.day[i].min) && IN(b.day[i].rain);
    return ok;
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
    CHECK(b.version == 1 && b.now == 1791614218L && b.has_offset && b.offset == -10800);
    STR(b.city, "Buenos Aires");
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
    CHECK(b.has_wx && !b.is_day);
    STR(b.temp, "11\xc2\xb0");
    STR(b.feels, "10\xc2\xb0");
    STR(b.humidity, "92%");
    STR(b.words, "Overcast");
    STR(b.wind, "9 km/h");
    CHECK(b.n_day == 3);
    STR(b.day[0].label, "Today");
    STR(b.day[0].words, "Rain showers");
    STR(b.day[0].max, "16\xc2\xb0");
    STR(b.day[0].min, "11\xc2\xb0");
    STR(b.day[0].rain, "96%");
    STR(b.day[1].label, "Sun");
    STR(b.day[2].label, "Mon");
    STR(b.day[2].words, "Drizzle");
    STR(b.day[2].rain, "22%");
    STR(b.wx_feed.state, "ok");
    CHECK(b.wx_feed.age == 310);
    CHECK(board_has_end(w, wn));
    group("the Worker test's reply, every field", f0);

    f0 = fails;
    CHECK(board_parse(w, wn, 1, &b) == BP_OK);
    STR(b.temp, "11");
    STR(b.day[0].max, "16");
    STR(b.city, "Buenos Aires");
    STR(b.usd[5].label, "Tarjeta");
    group("the same reply folded to ASCII (text_utf8 failed)", f0);

    f0 = fails;
    char *crlf = malloc((size_t)wn * 2 + 1);
    int k = 0;
    for (int i = 0; i < wn; i++) {
        if (w[i] == '\n') crlf[k++] = '\r';
        crlf[k++] = w[i];
    }
    crlf[k] = 0;
    CHECK(board_parse(crlf, k, 0, &b) == BP_OK && b.n_usd == 6 && b.n_day == 3 && board_has_end(crlf, k));
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
    CHECK(!b.is_day);
    STR(b.usd[1].label, "-");
    group("missing and empty fields read as \"-\"", f0);

    f0 = fails;
    CHECK(parse("couch-board 1\ncity= \t A\x01" "B \x7f \nnow=abc\noffset=60000\nend\n", 0) == BP_OK);
    STR(b.city, "AB");
    CHECK(b.now == 0 && !b.has_offset);
    CHECK(parse("couch-board 1\noffset=-43200\nnow= 1791614218 \nend\n", 0) == BP_OK && b.has_offset && b.offset == -43200 &&
          b.now == 1791614218L);
    group("control bytes dropped, spaces trimmed, numbers checked", f0);
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
static void fuzz(const char *w, int wn)
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
    group("20,000 mutated replies: every field ends inside its buffer", f0);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: board_check <the Worker test's reply> [fuzz]\n");
        return 1;
    }
    int wn;
    char *w = read_file(argv[1], &wn);
    worker_reply(w, wn);
    rules();
    fold();
    http();
    clock_words_check();
    if (argc > 2 && !strcmp(argv[2], "fuzz")) fuzz(w, wn);
    free(w);
    printf("%d failed\n", fails);
    return fails > 125 ? 125 : fails;
}
