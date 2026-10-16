/* Couch's pure functions (parse.h). The C library only: the host check
 * builds this file with the host's cc, the preview and ppu-gcc as it is. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "parse.h"

/* ---- text ------------------------------------------------------------------ */

/* The UTF-8 sequence at s (n bytes left): its length and its code point, or 0
 * when the bytes are not a valid sequence (an overlong form, a surrogate, a
 * cut sequence). */
static int utf8_seq(const unsigned char *s, int n, unsigned *cp)
{
    unsigned c = s[0], min;
    int len;
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c >= 0xc2 && c <= 0xdf) len = 2, c &= 0x1f, min = 0x80;
    else if (c >= 0xe0 && c <= 0xef) len = 3, c &= 0x0f, min = 0x800;
    else if (c >= 0xf0 && c <= 0xf4) len = 4, c &= 0x07, min = 0x10000;
    else return 0;
    if (n < len) return 0;
    for (int i = 1; i < len; i++) {
        if ((s[i] & 0xc0) != 0x80) return 0;
        c = c << 6 | (s[i] & 0x3f);
    }
    if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return 0;
    *cp = c;
    return len;
}

/* The base letters of U+00C0 to U+00FF; '?' for the two signs among them
 * (U+00D7 and U+00F7). */
static const char latin1_base[] = "AAAAAAACEEEEIIIIDNOOOOO?OUUUUYTsaaaaaaaceeeeiiiidnooooo?ouuuuyty";

void text_field(char *dst, int size, const char *src, int n, int fold)
{
    const unsigned char *s = (const unsigned char *)src;
    int o = 0;
    if (size < 1) return;
    for (int i = 0; i < n;) {
        unsigned cp = 0;
        int len = utf8_seq(s + i, n - i, &cp);
        const char *out = (const char *)s + i;
        char one = 0;
        int olen;
        if (len == 0) {                              /* a byte that starts no valid sequence */
            len = 1;
            if (fold) one = '?';
            olen = 1;
        } else if (cp < 0x20 || cp == 0x7f) {        /* a control byte: dropped */
            i += len;
            continue;
        } else if (!fold || cp < 0x80) {
            olen = len;
        } else if (cp == 0xb0) {                     /* the degree sign: nothing */
            i += len;
            continue;
        } else {
            one = cp == 0xa0 ? ' ' : (cp >= 0xc0 && cp <= 0xff) ? latin1_base[cp - 0xc0] : '?';
            olen = 1;
        }
        if (one) out = &one;
        if (o == 0 && olen == 1 && *out == ' ') {    /* the spaces at the start */
            i += len;
            continue;
        }
        if (o + olen > size - 1) break;              /* the cut, on a code point boundary */
        memcpy(dst + o, out, (size_t)olen);
        o += olen;
        i += len;
    }
    while (o > 0 && dst[o - 1] == ' ') o--;          /* the spaces at the end */
    dst[o] = 0;
}

/* ---- the Board reply ----------------------------------------------------- */

/* The next line of the body from *p: its start and length without the line
 * end (LF or CR LF); 0 at the end of the body. A last line with no LF counts. */
static int next_line(const char **p, const char *end, const char **ls, int *ll)
{
    if (*p >= end) return 0;
    const char *s = *p, *e = memchr(s, '\n', (size_t)(end - s));
    *ls = s;
    *ll = (int)((e ? e : end) - s);
    if (*ll > 0 && s[*ll - 1] == '\r') (*ll)--;
    *p = e ? e + 1 : end;
    return 1;
}

/* A whole number of at most 15 digits, a sign allowed, spaces at the ends
 * allowed: 1 and *v, or 0. */
static int to_long(const char *s, int n, long *v)
{
    int i = 0, neg = 0, digits = 0;
    long x = 0;
    while (n > 0 && s[n - 1] == ' ') n--;
    while (i < n && s[i] == ' ') i++;
    if (i < n && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
    for (; i < n; i++, digits++) {
        if (s[i] < '0' || s[i] > '9' || digits == 15) return 0;
        x = x * 10 + (s[i] - '0');
    }
    if (!digits) return 0;
    *v = neg ? -x : x;
    return 1;
}

/* The '|' fields of a value: at most max, each as its start and length; the count. */
static int split(const char *v, int n, const char **f, int *fl, int max)
{
    const char *s = v, *end = v + n;
    int k = 0;
    while (k < max) {
        const char *bar = memchr(s, '|', (size_t)(end - s));
        f[k] = s;
        fl[k] = (int)((bar ? bar : end) - s);
        k++;
        if (!bar) break;
        s = bar + 1;
    }
    return k;
}

/* Field i of k into dst, or "-" when the line has fewer fields or it is empty. */
static void put(char *dst, int size, const char **f, const int *fl, int k, int i, int fold)
{
    if (i < k) text_field(dst, size, f[i], fl[i], fold);
    if (i >= k || !dst[0]) snprintf(dst, (size_t)size, "-");
}

static int key_is(const char *k, int kl, const char *name) { return (int)strlen(name) == kl && !memcmp(k, name, (size_t)kl); }

/* "ok", "stale|reason" or "none|reason"; another word is none, with the whole
 * value as its reason. */
static void feed_state(board_feed *f, const char *v, int n, int fold)
{
    const char *bar = memchr(v, '|', (size_t)n);
    char w[8];
    text_field(w, sizeof w, v, bar ? (int)(bar - v) : n, 1);
    if (!strcmp(w, "ok") || !strcmp(w, "stale") || !strcmp(w, "none")) {
        snprintf(f->state, sizeof f->state, "%s", w);
        f->reason[0] = 0;
        if (bar) text_field(f->reason, sizeof f->reason, bar + 1, n - (int)(bar + 1 - v), fold);
    } else {
        snprintf(f->state, sizeof f->state, "none");
        text_field(f->reason, sizeof f->reason, v, n, fold);
    }
}

static void feed_age(board_feed *f, const char *v, int n)
{
    long a;
    if (to_long(v, n, &a) && a >= 0) f->age = a;
}

/* ---- the keys of 0.3.0: numbers in the C form ----------------------------- */

/* A number in the C form: a sign allowed, at most 9 whole digits, a '.' and
 * at most 6 decimals, spaces at the ends allowed: 1 and *v, or 0. The digit
 * caps keep every value far inside a double's exact range and an int's. */
static int to_dec(const char *s, int n, double *v)
{
    int i = 0, neg = 0, wd = 0, fd = 0;
    double x = 0, scale = 1;
    while (n > 0 && s[n - 1] == ' ') n--;
    while (i < n && s[i] == ' ') i++;
    if (i < n && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) {
        if (++wd > 9) return 0;
        x = x * 10 + (s[i] - '0');
    }
    if (i < n && s[i] == '.') {
        for (i++; i < n && s[i] >= '0' && s[i] <= '9'; i++) {
            if (++fd > 6) return 0;
            scale /= 10;
            x += (s[i] - '0') * scale;
        }
    }
    if (i < n || wd + fd == 0) return 0;
    *v = neg ? -x : x;
    return 1;
}

/* A whole value from n bytes: the number rounded half away from zero when
 * whole is 0 (a whole one only when 1), or none when it is missing ("-"),
 * not a number, or outside lo..hi. */
static int num_c(const char *s, int n, int lo, int hi, int whole, int none)
{
    double d;
    if (!to_dec(s, n, &d) || (whole && memchr(s, '.', (size_t)n))) return none;
    if (d < lo - 0.5 || d >= hi + 0.5) return none;
    int r = d < 0 ? -(int)(-d + 0.5) : (int)(d + 0.5);
    return r < lo || r > hi ? none : r;
}

/* Field i of k as num_c, none when the line has fewer fields. */
static int field_c(const char **f, const int *fl, int k, int i, int lo, int hi, int whole, int none)
{
    return i < k ? num_c(f[i], fl[i], lo, hi, whole, none) : none;
}

/* The space-separated words of a field: at most max, runs of spaces as one
 * separator; the count. */
static int words(const char *v, int n, const char **w, int *wl, int max)
{
    int k = 0, i = 0;
    while (k < max) {
        while (i < n && v[i] == ' ') i++;
        if (i == n) break;
        int s = i;
        while (i < n && v[i] != ' ') i++;
        w[k] = v + s;
        wl[k++] = i - s;
    }
    return k;
}

/* "2026-09-14" as days since 1970-01-01, -1 for anything else. */
static long ymd_days(const char *s, int n)
{
    int v[3] = {0, 0, 0}, len[3] = {4, 2, 2}, i = 0;
    while (n > 0 && s[n - 1] == ' ') n--;
    while (i < n && s[i] == ' ') i++;
    if (n - i != 10) return -1;
    for (int p = 0; p < 3; p++) {
        if (p && s[i++] != '-') return -1;
        for (int d = 0; d < len[p]; d++, i++) {
            if (s[i] < '0' || s[i] > '9') return -1;
            v[p] = v[p] * 10 + (s[i] - '0');
        }
    }
    static const unsigned char mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (v[0] < 1970 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > mdays[v[1] - 1]) return -1;
    if (v[1] == 2 && v[2] == 29 && !((v[0] % 4 == 0 && v[0] % 100 != 0) || v[0] % 400 == 0)) return -1;
    return days_from_civil(v[0], v[1], v[2]);
}

/* label|dir|pct|abs|date; the label cut as its usd or money line's (lsize
 * bytes with the NUL), so that a long label still finds its line */
static void chg_line(board_chg *g, const char *val, int vl, int fold, int lsize)
{
    const char *f[5];
    int fl[5];
    char dir[8];
    int c = split(val, vl, f, fl, 5);
    put(g->label, lsize, f, fl, c, 0, fold);
    put(dir, sizeof dir, f, fl, c, 1, 1);
    g->dir = !strcmp(dir, "up") ? CHG_UP : !strcmp(dir, "down") ? CHG_DOWN : !strcmp(dir, "flat") ? CHG_FLAT : CHG_NONE;
    put(g->pct, sizeof g->pct, f, fl, c, 2, fold);
    put(g->abs, sizeof g->abs, f, fl, c, 3, fold);
    put(g->when, sizeof g->when, f, fl, c, 4, fold);
}

/* label|yyyy-mm-dd|v1 v2 ... */
static void spark_line(board_spark *s, const char *val, int vl, int fold)
{
    const char *f[3], *w[SPARK_MAX];
    int fl[3], wl[SPARK_MAX];
    int c = split(val, vl, f, fl, 3);
    put(s->label, sizeof s->label, f, fl, c, 0, fold);
    s->start_day = c > 1 ? ymd_days(f[1], fl[1]) : -1;
    s->n = c > 2 ? words(f[2], fl[2], w, wl, SPARK_MAX) : 0;
    for (int i = 0; i < SPARK_MAX; i++) s->v[i] = i < s->n ? num_c(w[i], wl[i], 0, 100000000, 0, -1) : -1;
}

/* start|temps|chances|codes, each list HOURS_MAX words */
static void hours_line(board_data *b, const char *val, int vl)
{
    const char *f[4], *w[HOURS_MAX];
    int fl[4], wl[HOURS_MAX];
    int c = split(val, vl, f, fl, 4), k;
    b->hours_start = field_c(f, fl, c, 0, 0, 23, 1, -1);
    k = c > 1 ? words(f[1], fl[1], w, wl, HOURS_MAX) : 0;
    b->n_hours = k;
    for (int i = 0; i < HOURS_MAX; i++) b->hour_temp[i] = i < k ? num_c(w[i], wl[i], -90, 70, 0, TEMP_NONE) : TEMP_NONE;
    k = c > 2 ? words(f[2], fl[2], w, wl, HOURS_MAX) : 0;
    for (int i = 0; i < HOURS_MAX; i++) b->hour_pop[i] = i < k ? num_c(w[i], wl[i], 0, 100, 0, -1) : -1;
    k = c > 3 ? words(f[3], fl[3], w, wl, HOURS_MAX) : 0;
    for (int i = 0; i < HOURS_MAX; i++) b->hour_code[i] = i < k ? num_c(w[i], wl[i], 0, 99, 1, -1) : -1;
}

/* code|cover|mm|dir|speed|gusts */
static void fx_line(board_data *b, const char *val, int vl)
{
    const char *f[6];
    int fl[6];
    double mm;
    int c = split(val, vl, f, fl, 6);
    b->fx_code = field_c(f, fl, c, 0, 0, 99, 1, -1);
    b->fx_cover = field_c(f, fl, c, 1, 0, 100, 0, -1);
    b->fx_mm = c > 2 && to_dec(f[2], fl[2], &mm) && mm >= 0 && mm <= 1000 ? mm : -1;
    b->fx_dir = field_c(f, fl, c, 3, 0, 360, 0, -1);
    if (b->fx_dir == 360) b->fx_dir = 0;
    b->fx_speed = field_c(f, fl, c, 4, 0, 500, 0, -1);
    b->fx_gusts = field_c(f, fl, c, 5, 0, 500, 0, -1);
    b->has_fx = 1;
}

static void fx_none(board_data *b)
{
    b->fx_code = b->fx_cover = b->fx_dir = b->fx_speed = b->fx_gusts = b->hours_start = -1;
    b->fx_mm = -1;
    for (int i = 0; i < BOARD_DAYS; i++) b->day_code[i] = -1;
}

int board_parse(const char *body, int n, int fold, board_data *b)
{
    static const char magic[] = "couch-board ";
    const int ml = (int)sizeof magic - 1;
    const char *p = body, *end = body + n, *ls;
    int ll;
    long v;
    memset(b, 0, sizeof *b);
    b->usd_feed.age = b->wx_feed.age = b->news_feed.age = -1;
    fx_none(b);
    if (!next_line(&p, end, &ls, &ll) || ll <= ml || memcmp(ls, magic, (size_t)ml) || !to_long(ls + ml, ll - ml, &v) || v < 0 ||
        v > 9999)
        return BP_NOT_BOARD;
    b->version = (int)v;
    if (v != 1) return BP_VERSION;
    while (next_line(&p, end, &ls, &ll)) {
        if (ll == 3 && !memcmp(ls, "end", 3)) return BP_OK;
        const char *eq = memchr(ls, '=', (size_t)ll);
        if (!eq) continue;
        const char *k = ls, *val = eq + 1;
        int kl = (int)(eq - ls), vl = ll - kl - 1;
        const char *f[7];
        int fl[7], c;
        if (key_is(k, kl, "now")) {
            if (to_long(val, vl, &v) && v > 0) b->now = v;
        } else if (key_is(k, kl, "offset")) {
            if (to_long(val, vl, &v) && v >= -43200 && v <= 50400) b->has_offset = 1, b->offset = (int)v;
        } else if (key_is(k, kl, "city")) {
            text_field(b->city, sizeof b->city, val, vl, fold);
        } else if (key_is(k, kl, "usd")) {
            if (b->n_usd == BOARD_USD) continue;
            board_usd *u = &b->usd[b->n_usd++];
            c = split(val, vl, f, fl, 4);
            put(u->label, sizeof u->label, f, fl, c, 0, fold);
            put(u->buy, sizeof u->buy, f, fl, c, 1, fold);
            put(u->sell, sizeof u->sell, f, fl, c, 2, fold);
            put(u->when, sizeof u->when, f, fl, c, 3, fold);
        } else if (key_is(k, kl, "usd_age")) {
            feed_age(&b->usd_feed, val, vl);
        } else if (key_is(k, kl, "usd_state")) {
            feed_state(&b->usd_feed, val, vl, fold);
        } else if (key_is(k, kl, "wx_now")) {
            /* 7 fields: the 7th is the source. split ends each field at its
             * '|', so a Couch that splits 6 reads "day" and drops the rest. */
            char dn[8];
            c = split(val, vl, f, fl, 7);
            put(b->temp, sizeof b->temp, f, fl, c, 0, fold);
            put(b->feels, sizeof b->feels, f, fl, c, 1, fold);
            put(b->humidity, sizeof b->humidity, f, fl, c, 2, fold);
            put(b->words, sizeof b->words, f, fl, c, 3, fold);
            put(b->wind, sizeof b->wind, f, fl, c, 4, fold);
            put(dn, sizeof dn, f, fl, c, 5, 1);
            b->is_day = !strcmp(dn, "day");
            b->wx_src[0] = 0;                        /* absent or empty: "", not put's "-" */
            if (c > 6) text_field(b->wx_src, sizeof b->wx_src, f[6], fl[6], fold);
            b->has_wx = 1;
        } else if (key_is(k, kl, "day")) {
            if (b->n_day == BOARD_DAYS) continue;
            board_day *d = &b->day[b->n_day++];
            c = split(val, vl, f, fl, 5);
            put(d->label, sizeof d->label, f, fl, c, 0, fold);
            put(d->words, sizeof d->words, f, fl, c, 1, fold);
            put(d->max, sizeof d->max, f, fl, c, 2, fold);
            put(d->min, sizeof d->min, f, fl, c, 3, fold);
            put(d->rain, sizeof d->rain, f, fl, c, 4, fold);
        } else if (key_is(k, kl, "wx_age")) {
            feed_age(&b->wx_feed, val, vl);
        } else if (key_is(k, kl, "wx_state")) {
            feed_state(&b->wx_feed, val, vl, fold);
        } else if (key_is(k, kl, "money")) {
            /* label|value|date|state|reason: a state that is not ok or stale is none */
            if (b->n_money == BOARD_MONEY) continue;
            board_money *m = &b->money[b->n_money++];
            char st[8];
            c = split(val, vl, f, fl, 5);
            put(m->label, sizeof m->label, f, fl, c, 0, fold);
            put(m->value, sizeof m->value, f, fl, c, 1, fold);
            put(m->when, sizeof m->when, f, fl, c, 2, fold);
            put(st, sizeof st, f, fl, c, 3, 1);
            snprintf(m->state, sizeof m->state, "%s", !strcmp(st, "ok") || !strcmp(st, "stale") ? st : "none");
            m->reason[0] = 0;
            if (c > 4) text_field(m->reason, sizeof m->reason, f[4], fl[4], fold);
        } else if (key_is(k, kl, "wx_rain")) {
            char kind[8];
            c = split(val, vl, f, fl, 2);
            put(kind, sizeof kind, f, fl, c, 0, 1);
            put(b->rain, sizeof b->rain, f, fl, c, 1, fold);
            b->rain_wet = !strcmp(kind, "rain");
            b->has_rain = 1;
        } else if (key_is(k, kl, "wx_sun")) {
            c = split(val, vl, f, fl, 2);
            put(b->sunrise, sizeof b->sunrise, f, fl, c, 0, fold);
            put(b->sunset, sizeof b->sunset, f, fl, c, 1, fold);
            b->has_sun = strcmp(b->sunrise, "-") && strcmp(b->sunset, "-");
        } else if (key_is(k, kl, "wx_moon")) {
            text_field(b->moon, sizeof b->moon, val, vl, fold);
        } else if (key_is(k, kl, "news")) {
            /* source|age|title: the title ends at a '|', which the Worker removes from upstream text */
            if (b->n_news == BOARD_NEWS) continue;
            board_news *n = &b->news[b->n_news];
            c = split(val, vl, f, fl, 3);
            put(n->source, sizeof n->source, f, fl, c, 0, fold);
            n->age = -1;
            if (c > 1 && to_long(f[1], fl[1], &v) && v >= 0) n->age = v;
            n->title[0] = 0;
            if (c > 2) text_field(n->title, sizeof n->title, f[2], fl[2], fold);
            if (n->title[0]) b->n_news++;            /* a line with no title is not a headline */
        } else if (key_is(k, kl, "news_age")) {
            feed_age(&b->news_feed, val, vl);
        } else if (key_is(k, kl, "news_state")) {
            feed_state(&b->news_feed, val, vl, fold);
        } else if (key_is(k, kl, "geo")) {
            double la, lo;
            c = split(val, vl, f, fl, 2);
            b->has_geo = c == 2 && to_dec(f[0], fl[0], &la) && to_dec(f[1], fl[1], &lo) && la >= -90 && la <= 90 && lo >= -180 && lo <= 180;
            b->lat = b->has_geo ? la : 0;
            b->lon = b->has_geo ? lo : 0;
        } else if (key_is(k, kl, "wx_fx")) {
            fx_line(b, val, vl);
        } else if (key_is(k, kl, "wx_hours")) {
            hours_line(b, val, vl);
        } else if (key_is(k, kl, "days_fx")) {
            c = split(val, vl, f, fl, BOARD_DAYS);
            b->n_day_code = c;
            for (int i = 0; i < BOARD_DAYS; i++) b->day_code[i] = field_c(f, fl, c, i, 0, 99, 1, -1);
        } else if (key_is(k, kl, "usd_chg")) {
            if (b->n_usd_chg < BOARD_USD) chg_line(&b->usd_chg[b->n_usd_chg++], val, vl, fold, (int)sizeof b->usd[0].label);
        } else if (key_is(k, kl, "money_chg")) {
            if (b->n_money_chg < BOARD_MONEY) chg_line(&b->money_chg[b->n_money_chg++], val, vl, fold, (int)sizeof b->money[0].label);
        } else if (key_is(k, kl, "spark")) {
            if (b->n_spark < BOARD_SPARKS) spark_line(&b->spark[b->n_spark++], val, vl, fold);
        }
    }
    return BP_CUT;
}

int board_has_end(const char *body, int n)
{
    for (int i = 0; i + 4 < n; i++)
        if (body[i] == '\n' && body[i + 1] == 'e' && body[i + 2] == 'n' && body[i + 3] == 'd' &&
            (body[i + 4] == '\n' || (body[i + 4] == '\r' && i + 5 < n && body[i + 5] == '\n')))
            return 1;
    return 0;
}

/* ---- HTTP ------------------------------------------------------------------ */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* 1 when the n bytes at s start with p, case not counted. */
static int iprefix(const char *s, int n, const char *p)
{
    int k = (int)strlen(p);
    if (n < k) return 0;
    for (int i = 0; i < k; i++)
        if (lower((unsigned char)s[i]) != lower((unsigned char)p[i])) return 0;
    return 1;
}

int http_head(const char *buf, int n, int *status, char *loc, int loc_size, int *chunked)
{
    *status = 0;
    *chunked = 0;
    if (loc_size > 0) loc[0] = 0;
    if (memcmp(buf, "HTTP/", (size_t)(n < 5 ? n : 5))) return -1;
    int hl = 0;
    for (int i = 0; i + 1 < n && !hl; i++)           /* the blank line: LF LF or LF CR LF */
        if (buf[i] == '\n') {
            if (buf[i + 1] == '\n') hl = i + 2;
            else if (buf[i + 1] == '\r' && i + 2 < n && buf[i + 2] == '\n') hl = i + 3;
        }
    if (!hl) return 0;
    int i = 5;
    while (i < hl && buf[i] != ' ' && buf[i] != '\n') i++;   /* the version */
    while (i < hl && buf[i] == ' ') i++;
    if (i + 3 > hl || buf[i] < '1' || buf[i] > '5' || buf[i + 1] < '0' || buf[i + 1] > '9' || buf[i + 2] < '0' || buf[i + 2] > '9')
        return -1;
    *status = (buf[i] - '0') * 100 + (buf[i + 1] - '0') * 10 + (buf[i + 2] - '0');
    const char *s = memchr(buf, '\n', (size_t)hl);
    while (s && s + 1 < buf + hl) {
        const char *ls = s + 1, *e = memchr(ls, '\n', (size_t)(buf + hl - ls));
        int ll = (int)((e ? e : buf + hl) - ls);
        if (ll > 0 && ls[ll - 1] == '\r') ll--;
        if (iprefix(ls, ll, "location:")) {
            int a = 9;
            while (a < ll && ls[a] == ' ') a++;
            int m = ll - a < loc_size - 1 ? ll - a : loc_size - 1;
            if (m > 0) memcpy(loc, ls + a, (size_t)m), loc[m] = 0;
        } else if (iprefix(ls, ll, "transfer-encoding:")) {
            for (int a = 18; a + 7 <= ll; a++)
                if (iprefix(ls + a, ll - a, "chunked")) *chunked = 1;
        }
        s = e;
    }
    return hl;
}

int url_split(const char *url, char *host, int hs, int *port, char *path, int ps)
{
    const char *u = url, *sch = strstr(url, "://");
    if (sch && sch + 1 == strchr(url, '/')) {        /* a scheme: "://" before any other '/' */
        if (sch - url != 4 || !iprefix(url, 4, "http")) return -2;
        u = sch + 3;
    }
    const char *slash = strchr(u, '/');
    int hl = slash ? (int)(slash - u) : (int)strlen(u);
    const char *colon = memchr(u, ':', (size_t)hl);
    int nl = colon ? (int)(colon - u) : hl;
    if (nl < 1 || nl > hs - 1) return -1;
    for (int i = 0; i < hl; i++)
        if ((unsigned char)u[i] <= ' ' || u[i] == 0x7f) return -1;
    *port = 80;
    if (colon) {
        long p;
        int pl = hl - nl - 1;
        if (pl < 1 || pl > 5 || colon[1] == '-' || colon[1] == '+' || !to_long(colon + 1, pl, &p) || p < 1 || p > 65535) return -1;
        *port = (int)p;
    }
    const char *pa = slash ? slash : "/";
    int pl = (int)strlen(pa);
    if (pl > ps - 1) return -1;
    for (int i = 0; i < pl; i++)
        if ((unsigned char)pa[i] <= ' ' || pa[i] == 0x7f) return -1;
    memcpy(host, u, (size_t)nl);
    host[nl] = 0;
    memcpy(path, pa, (size_t)pl + 1);
    return 0;
}

/* ---- the clock ------------------------------------------------------------- */

int url_sibling(const char *path, const char *leaf, char *out, int n)
{
    int q = (int)strcspn(path, "?#"), slash = -1;
    for (int i = 0; i < q; i++)
        if (path[i] == '/') slash = i;
    int k = snprintf(out, (size_t)n, "%.*s%s", slash + 1, slash >= 0 ? path : "/", leaf);
    if (slash < 0 && k >= 0 && k < n) k = snprintf(out, (size_t)n, "/%s", leaf);
    return k < 0 || k >= n ? -1 : 0;
}

const char *const weekday_name[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char *const month_name[12] = {"January", "February", "March", "April", "May", "June",
                                    "July", "August", "September", "October", "November", "December"};

/* The civil date of a day count from 1970-01-01 (H. Hinnant's civil_from_days). */
void civil_from_unix(long t, civil_time *c)
{
    long days = t / 86400, rem = t % 86400;
    if (rem < 0) rem += 86400, days--;
    c->hour = (int)(rem / 3600);
    c->min = (int)(rem % 3600 / 60);
    c->sec = (int)(rem % 60);
    c->wday = (int)((days % 7 + 11) % 7);            /* 1970-01-01 was a Thursday */
    long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    long doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153;
    c->day = (int)(doy - (153 * mp + 2) / 5 + 1);
    c->month = (int)(mp < 10 ? mp + 3 : mp - 9);
    c->year = (int)(yoe + era * 400 + (c->month <= 2));
}

/* H. Hinnant's days_from_civil */
long days_from_civil(long y, int m, int d)
{
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    long yoe = y - era * 400;
    long doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

void clock_hm(char *buf, int n, int hour, int min, int h24, const char **ampm)
{
    if (h24) {
        snprintf(buf, (size_t)n, "%02d:%02d", hour, min);
        *ampm = NULL;
        return;
    }
    snprintf(buf, (size_t)n, "%d:%02d", hour % 12 ? hour % 12 : 12, min);
    *ampm = hour < 12 ? "AM" : "PM";
}

void utc_label(char *buf, int n, int offset)
{
    int a = offset < 0 ? -offset : offset, h = a / 3600, m = a % 3600 / 60;
    char sign = offset < 0 ? '-' : '+';
    if (!h && !m) snprintf(buf, (size_t)n, "UTC");
    else if (m) snprintf(buf, (size_t)n, "UTC%c%d:%02d", sign, h, m);
    else snprintf(buf, (size_t)n, "UTC%c%d", sign, h);
}

int tz_offset(int tz_min, int summer, int *offset)
{
    if (tz_min < -720 || tz_min > 840) return 0;
    *offset = (tz_min + (summer ? 60 : 0)) * 60;
    return 1;
}

void age_words(char *buf, int n, long s)
{
    if (s < 0) s = 0;
    if (s < 60) snprintf(buf, (size_t)n, "%ld s", s);
    else if (s < 3600) snprintf(buf, (size_t)n, "%ld min", s / 60);
    else if (s < 86400 && s % 3600 / 60) snprintf(buf, (size_t)n, "%ld h %ld min", s / 3600, s % 3600 / 60);
    else if (s < 86400) snprintf(buf, (size_t)n, "%ld h", s / 3600);
    else if (s % 86400 / 3600) snprintf(buf, (size_t)n, "%ld d %ld h", s / 86400, s % 86400 / 3600);
    else snprintf(buf, (size_t)n, "%ld d", s / 86400);
}

/* The BSD numbers of PSL1GHT's NET_E* (net/net.h). */
const char *net_err_words(int e, char *buf, int n)
{
    switch (e) {
    case 35: return "no answer in 10 s";             /* NET_EAGAIN: SO_RCVTIMEO ran out */
    case 50: return "network down";
    case 51: return "network unreachable";
    case 54: return "reset by the server";
    case 60: return "timed out";
    case 61: return "refused";
    case 64: return "the host is down";
    case 65: return "no route to the host";
    default: snprintf(buf, (size_t)n, "error %d", e); return buf;
    }
}

/* ---- auto.txt -------------------------------------------------------------- */

static int blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

int auto_parse(const char *s, int n)
{
    int i = 0, digits = 0;
    long v = 0;
    while (i < n && blank(s[i])) i++;
    if (i == n) return -1;
    for (; i < n && s[i] >= '0' && s[i] <= '9'; i++, digits++)
        if (v < 100000) v = v * 10 + (s[i] - '0');
    while (i < n && blank(s[i])) i++;
    if (!digits || i < n) return 0;
    if (v == 0) return -1;
    return v < 60 ? 60 : v > 600 ? 600 : (int)v;
}

/* ---- the internet, the ambient mode ------------------------------------------- */

int net_quality(int answered, long long total_us)
{
    if (!answered) return NQ_DOWN;
    return total_us >= NQ_SLOW_US ? NQ_SLOW : NQ_OK;
}

/* A walk over the 3 x 3 grid of -6, 0 and 6 pixels, one 6-pixel move per
 * step, back to the start after 9 steps: no place twice in a row, and the
 * picture never more than 6 pixels from its place on either axis. */
static const signed char shift_xy[AMBIENT_SHIFTS][2] = {
    {0, 0}, {AMBIENT_PX, 0}, {AMBIENT_PX, AMBIENT_PX}, {0, AMBIENT_PX}, {-AMBIENT_PX, AMBIENT_PX},
    {-AMBIENT_PX, 0}, {-AMBIENT_PX, -AMBIENT_PX}, {0, -AMBIENT_PX}, {AMBIENT_PX, -AMBIENT_PX}};

void ambient_shift(long step, int *dx, int *dy)
{
    long i = step % AMBIENT_SHIFTS;
    if (i < 0) i += AMBIENT_SHIFTS;
    *dx = shift_xy[i][0];
    *dy = shift_xy[i][1];
}

int ambient_page(long step, int has_news) { return has_news && (step % 2 == 1 || step % 2 == -1); }

void ambient_place(double t, float *x, float *y)
{
    if (t < 0) t = 0;
    long step = (long)(t / AMBIENT_STEP_SEC);
    double in = t - (double)step * AMBIENT_STEP_SEC, k = in >= AMBIENT_GLIDE_S ? 1 : in / AMBIENT_GLIDE_S;
    int x1, y1, x0, y0;
    ambient_shift(step, &x1, &y1);
    ambient_shift(step - 1, &x0, &y0);
    if (step == 0) x0 = x1, y0 = y1;
    k = k * k * (3 - 2 * k);                     /* ease in and out */
    *x = (float)((x0 + (x1 - x0) * k) / AMBIENT_PX * AMBIENT_BLOCK_X);
    *y = (float)((y0 + (y1 - y0) * k) / AMBIENT_PX * AMBIENT_BLOCK_Y);
}

int ambient_line(long minute, unsigned has)
{
    if (minute < 0) minute = 0;
    for (int k = 0; k < 3; k++) {
        int l = (int)((minute + k) % 3);
        if (has & (1u << l)) return l;
    }
    return -1;
}

const char *news_category(const char *source)
{
    if (!strncmp(source, "Ambito", 6) || !strncmp(source, "\xc3\x81mbito", 7) || !strncmp(source, "\xc3\xa1mbito", 7)) return "Economy";
    if (!strncmp(source, "BBC", 3)) return "World";
    return NULL;
}

double ticker_offset(double t, double px_s, double period)
{
    if (!(period > 0) || !(px_s > 0) || t < 0) return 0;
    double d = fmod(t * px_s, period);
    return d < 0 ? 0 : d;
}

/* ---- the reader (0.3.1) ---------------------------------------------------------- */

/* "magic N" on line 1: BP_OK for version 1, BP_VERSION (*version set), BP_NOT_BOARD. */
static int head_line(const char **p, const char *end, const char *magic, int *version)
{
    const char *ls;
    int ll, ml = (int)strlen(magic);
    long v;
    if (!next_line(p, end, &ls, &ll) || ll <= ml || memcmp(ls, magic, (size_t)ml) || !to_long(ls + ml, ll - ml, &v) || v < 0 || v > 9999)
        return BP_NOT_BOARD;
    *version = (int)v;
    return v == 1 ? BP_OK : BP_VERSION;
}

/* An id: 8 characters 0-9a-f into dst (9 bytes), else "". */
static void id_field(char *dst, const char *s, int n)
{
    dst[0] = 0;
    if (n != 8) return;
    for (int i = 0; i < 8; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return;
    memcpy(dst, s, 8);
    dst[8] = 0;
}

/* A category: the word, "" for "-" or nothing. */
static void cat_field(char *dst, int size, const char *s, int n, int fold)
{
    text_field(dst, size, s, n, fold);
    if (!strcmp(dst, "-")) dst[0] = 0;
}

/* An age: whole seconds of at most 15 digits, -1 for "-" or anything else. */
static long age_field(const char *s, int n)
{
    long v;
    return to_long(s, n, &v) && v >= 0 ? v : -1;
}

int news_parse(const char *body, int n, int fold, news_data *d)
{
    const char *p = body, *end = body + n, *ls;
    int ll;
    long v;
    memset(d, 0, sizeof *d);
    d->feed.age = -1;
    int rc = head_line(&p, end, "couch-news ", &d->version);
    if (rc != BP_OK) return rc;
    while (next_line(&p, end, &ls, &ll)) {
        if (ll == 3 && !memcmp(ls, "end", 3)) return BP_OK;
        const char *eq = memchr(ls, '=', (size_t)ll);
        if (!eq) continue;
        const char *k = ls, *val = eq + 1;
        int kl = (int)(eq - ls), vl = ll - kl - 1;
        if (key_is(k, kl, "now")) {
            if (to_long(val, vl, &v) && v > 0) d->now = v;
        } else if (key_is(k, kl, "item")) {
            /* id|source|category|age|title|summary: the summary ends at a '|', which the Worker never sends */
            if (d->n == NEWS_ITEMS) continue;
            const char *f[6];
            int fl[6], c = split(val, vl, f, fl, 6);
            news_item *it = &d->item[d->n];
            memset(it, 0, sizeof *it);
            if (c > 0) id_field(it->id, f[0], fl[0]);
            put(it->source, sizeof it->source, f, fl, c, 1, fold);
            if (c > 2) cat_field(it->category, sizeof it->category, f[2], fl[2], fold);
            it->age = c > 3 ? age_field(f[3], fl[3]) : -1;
            if (c > 4) text_field(it->title, sizeof it->title, f[4], fl[4], fold);
            if (c > 5) text_field(it->summary, sizeof it->summary, f[5], fl[5], fold);
            if (it->title[0]) d->n++;                /* a line with no title is not an item */
        } else if (key_is(k, kl, "news_age")) {
            feed_age(&d->feed, val, vl);
        } else if (key_is(k, kl, "news_state")) {
            feed_state(&d->feed, val, vl, fold);
        }
    }
    return BP_CUT;
}

int article_parse(const char *body, int n, int fold, article_data *a)
{
    const char *p = body, *end = body + n, *ls;
    int ll, used = 0;
    long v;
    memset(a, 0, sizeof *a);
    a->age = -1;
    int rc = head_line(&p, end, "couch-article ", &a->version);
    if (rc != BP_OK) return rc;
    while (next_line(&p, end, &ls, &ll)) {
        if (ll == 3 && !memcmp(ls, "end", 3)) return BP_OK;
        const char *eq = memchr(ls, '=', (size_t)ll);
        if (!eq) continue;
        const char *k = ls, *val = eq + 1;
        int kl = (int)(eq - ls), vl = ll - kl - 1;
        if (key_is(k, kl, "now")) {
            if (to_long(val, vl, &v) && v > 0) a->now = v;
        } else if (key_is(k, kl, "id")) {
            id_field(a->id, val, vl);
        } else if (key_is(k, kl, "source")) {
            text_field(a->source, sizeof a->source, val, vl, fold);
            if (!strcmp(a->source, "-")) a->source[0] = 0;
        } else if (key_is(k, kl, "category")) {
            cat_field(a->category, sizeof a->category, val, vl, fold);
        } else if (key_is(k, kl, "title")) {
            text_field(a->title, sizeof a->title, val, vl, fold);
        } else if (key_is(k, kl, "date")) {
            const char *f[2];
            int fl[2], c = split(val, vl, f, fl, 2);
            text_field(a->date, sizeof a->date, f[0], fl[0], fold);
            if (!strcmp(a->date, "-")) a->date[0] = 0;
            a->age = c > 1 ? age_field(f[1], fl[1]) : -1;
        } else if (key_is(k, kl, "link")) {
            if (vl > (int)sizeof a->link - 1) continue;      /* a cut link is a broken one: none (the Worker sends 1019 bytes at most) */
            text_field(a->link, sizeof a->link, val, vl, 0);   /* bytes as they came: a fold would break the link */
            for (int i = 0; a->link[i]; i++)
                if (a->link[i] == ' ') {                     /* the Worker encodes spaces: one here is not a link */
                    a->link[0] = 0;
                    break;
                }
        } else if (key_is(k, kl, "state")) {
            const char *bar = memchr(val, '|', (size_t)vl);
            char w[8];
            text_field(w, sizeof w, val, bar ? (int)(bar - val) : vl, 1);
            a->reason[0] = 0;
            if (!strcmp(w, "ok") || !strcmp(w, "cut") || !strcmp(w, "summary") || !strcmp(w, "none")) {
                snprintf(a->state, sizeof a->state, "%s", w);
                if (bar) text_field(a->reason, sizeof a->reason, bar + 1, vl - (int)(bar + 1 - val), fold);
            } else {
                snprintf(a->state, sizeof a->state, "none");
                text_field(a->reason, sizeof a->reason, val, vl, fold);
            }
        } else if (key_is(k, kl, "p")) {
            if (a->n_para == ART_PARAS || used >= ART_TEXT - 1) {
                a->text_cut = 1;
                continue;
            }
            char *dst = a->text + used;
            int room = ART_TEXT - used;
            /* the field cut on a code point boundary when the rest is short; a cut paragraph marks the text cut */
            text_field(dst, room, val, vl, fold);
            int len = (int)strlen(dst);
            if (!len) continue;                      /* an empty paragraph: nothing */
            if (len >= room - 1) a->text_cut = 1;          /* it filled the rest: cut, or exactly at the end */
            a->para_off[a->n_para++] = used;
            used += len + 1;
        }
    }
    return BP_CUT;
}

/* The length of the UTF-8 sequence that starts with byte c (1 for a byte
 * that starts none: it counts as one code point, as ps3gfx draws it). */
static int seq_len(unsigned char c) { return c >= 0xf0 && c <= 0xf4 ? 4 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xc2 && c <= 0xdf ? 2 : 1; }

int wrap_lines(const char *s, int n, float maxw, wrap_width_fn width, void *ctx, int *off, int *len, int max)
{
    int lines = 0, i = 0;
    float sp = width(" ", 1, ctx);
    while (i < n && lines < max) {
        while (i < n && s[i] == ' ') i++;            /* the spaces at a break */
        if (i >= n) break;
        int start = i, end = i;                      /* end: the line's last word's end */
        float w = 0;
        while (i < n) {
            int ws = i, we = i;
            while (we < n && s[we] != ' ') we++;     /* a word: ws .. we */
            float ww = width(s + ws, we - ws, ctx);
            if (end == start) {                      /* the line's first word */
                if (ww > maxw) {                     /* wider than the line: cut at the last code point that fits, one at least */
                    int k = ws, fit = ws;
                    while (k < we) {
                        int l = seq_len((unsigned char)s[k]);
                        if (k + l > we) l = we - k;
                        if (fit > ws && width(s + ws, k + l - ws, ctx) > maxw) break;
                        k += l;
                        fit = k;
                    }
                    end = fit;
                    i = fit;
                    break;
                }
                w = ww;
            } else {
                float add = (float)(ws - end) * sp + ww;   /* the spaces between, as drawn */
                if (w + add > maxw) break;           /* the word goes to the next line */
                w += add;
            }
            end = we;
            i = we;
            while (i < n && s[i] == ' ') i++;
        }
        off[lines] = start;
        len[lines] = end - start;
        lines++;
    }
    return lines;
}

int grid_row(int i) { return i <= 0 ? 0 : 1 + (i - 1) / 2; }

int grid_move(int sel, int n, int dir)
{
    if (n <= 0) return 0;
    if (sel < 0) sel = 0;
    if (sel >= n) sel = n - 1;
    int row = grid_row(sel), col = sel == 0 ? 0 : (sel - 1) % 2, rows = grid_row(n - 1);
    switch (dir) {
    case NAV_LEFT: return sel > 0 && col == 1 ? sel - 1 : sel;
    case NAV_RIGHT: return sel > 0 && col == 0 && sel + 1 < n ? sel + 1 : sel;
    case NAV_UP:
        if (row == 0) return sel;
        if (row == 1) return 0;
        return sel - 2;
    case NAV_DOWN:
        if (row == rows) return sel;
        if (row == 0) return 1;
        return sel + 2 < n ? sel + 2 : n - 1;        /* the row below has no right card: its left one */
    }
    return sel;
}

double scroll_clamp(double s, double view, double total)
{
    double m = total - view;
    if (!(m > 0)) return 0;
    return s < 0 ? 0 : s > m ? m : s;
}

double scroll_keep(double cur, double y0, double y1, double view, double total, double margin)
{
    double t = cur;
    double top = y0 > 0 ? y0 - margin : 0, bot = y1 < total ? y1 + margin : total;
    if (bot - top > view) bot = top + view;          /* taller than the view: its top shows */
    if (top < t) t = top;
    else if (bot > t + view) t = bot - view;
    return scroll_clamp(t, view, total);
}

double scroll_follow(double s, double target, double dt, double tau)
{
    if (!(dt > 0) || !(tau > 0)) return s;
    double d = (target - s) * exp(-dt / tau);
    return fabs(d) < 0.02 ? target : target - d;
}

/* ---- the changes, the series, the numbers (0.3.0) ------------------------------ */

const board_chg *board_chg_find(const board_data *b, int money, const char *label)
{
    const board_chg *c = money ? b->money_chg : b->usd_chg;
    int n = money ? b->n_money_chg : b->n_usd_chg;
    for (int i = 0; i < n; i++)
        if (!strcmp(c[i].label, label)) return &c[i];
    return NULL;
}

const board_spark *board_spark_find(const board_data *b, const char *label)
{
    for (int i = 0; i < b->n_spark; i++)
        if (!strcmp(b->spark[i].label, label)) return &b->spark[i];
    return NULL;
}

int spark_range(const board_spark *s, int *lo, int *hi)
{
    int k = 0;
    for (int i = 0; i < s->n && i < SPARK_MAX; i++) {
        if (s->v[i] < 0) continue;
        if (!k || s->v[i] < *lo) *lo = s->v[i];
        if (!k || s->v[i] > *hi) *hi = s->v[i];
        k++;
    }
    return k;
}

#define NUM_DIGITS 12
#define NUM_DEC 4

static const long long pow10_ll[NUM_DEC + 1] = {1, 10, 100, 1000, 10000};

/* the digits at s[*i] into *v (at most *left of them): their count */
static int digit_run(const char *s, int *i, long long *v, int *left)
{
    int k = 0;
    while (s[*i] >= '0' && s[*i] <= '9') {
        if (--*left < 0) return -1;
        *v = *v * 10 + (s[(*i)++] - '0');
        k++;
    }
    return k;
}

int num_parse(const char *s, board_num *x)
{
    int i = 0, left = NUM_DIGITS, k;
    long long v = 0;
    memset(x, 0, sizeof *x);
    if (s[i] == '+') x->plus = 1, i++;
    else if (s[i] == '-') x->neg = 1, i++;
    k = digit_run(s, &i, &v, &left);
    if (k <= 0) return 0;
    if (s[i] == '.') {                               /* the thousands groups: 1 to 3 digits, then 3 each */
        if (k > 3) return 0;
        while (s[i] == '.') {
            i++;
            if (digit_run(s, &i, &v, &left) != 3) return 0;
        }
    }
    if (s[i] == ',') {
        i++;
        k = digit_run(s, &i, &v, &left);
        if (k <= 0 || k > NUM_DEC) return 0;
        x->dec = k;
    }
    if (s[i] == '%') x->pct = 1, i++;
    if (s[i]) return 0;
    x->v = x->neg ? -v : v;
    return 1;
}

void num_format(char *buf, int n, const board_num *x)
{
    char w[32], g[48];
    int dec = x->dec < 0 ? 0 : x->dec > NUM_DEC ? NUM_DEC : x->dec;
    unsigned long long a = x->v < 0 ? 0ULL - (unsigned long long)x->v : (unsigned long long)x->v;
    unsigned long long p = (unsigned long long)pow10_ll[dec];
    int wl = snprintf(w, sizeof w, "%llu", a / p), k = 0;
    for (int i = 0; i < wl; i++) {                   /* a '.' before each group of three from the right */
        if (i && (wl - i) % 3 == 0) g[k++] = '.';
        g[k++] = w[i];
    }
    g[k] = 0;
    const char *sign = x->v < 0 || (x->v == 0 && x->neg) ? "-" : x->plus ? "+" : "";
    if (dec) snprintf(buf, (size_t)n, "%s%s,%0*llu%s", sign, g, dec, a % p, x->pct ? "%" : "");
    else snprintf(buf, (size_t)n, "%s%s%s", sign, g, x->pct ? "%" : "");
}

void num_mix(const board_num *a, const board_num *b, double t, board_num *out)
{
    *out = *b;
    if (!(t < 1)) return;                            /* 1, above, or NaN: the end value */
    if (t < 0) t = 0;
    int ad = a->dec < 0 ? 0 : a->dec > NUM_DEC ? NUM_DEC : a->dec, bd = b->dec < 0 ? 0 : b->dec > NUM_DEC ? NUM_DEC : b->dec;
    long long av = a->v;
    if (ad < bd) av *= pow10_ll[bd - ad];
    else if (ad > bd) {
        long long q = pow10_ll[ad - bd];
        av = av < 0 ? -((-av + q / 2) / q) : (av + q / 2) / q;
    }
    double d = (double)av + ((double)b->v - (double)av) * t;
    out->v = d < 0 ? -(long long)(-d + 0.5) : (long long)(d + 0.5);
    out->neg = 0;                                    /* the value's own sign, until the end */
}

int hm_parse(const char *s)
{
    int i = 0, h = 0, m = 0;
    for (; s[i] >= '0' && s[i] <= '9' && i < 2; i++) h = h * 10 + (s[i] - '0');
    if (i < 1 || s[i] != ':') return -1;
    const char *p = s + i + 1;
    if (p[0] < '0' || p[0] > '5' || p[1] < '0' || p[1] > '9' || p[2]) return -1;
    m = (p[0] - '0') * 10 + (p[1] - '0');
    return h > 23 ? -1 : h * 60 + m;
}

/* ---- the sky's colours, the weather codes, the wind ------------------------------ */

#define SKY_STOPS 6
static const double sky_elev[SKY_STOPS] = {-12, -9, -3, 3, 13, 20};
static const unsigned sky_top[SKY_STOPS] = {0x050a18, 0x0a1530, 0x1b2550, 0x3a5fa0, 0x2f6fc0, 0x2a66c4};
static const unsigned sky_hor[SKY_STOPS] = {0x0d1a33, 0x1f2d55, 0xc8644a, 0xf2a65a, 0x8fc3ec, 0x9fd0f0};

/* each channel of a toward b by t (0..1), rounded */
static unsigned rgb_mix(unsigned a, unsigned b, double t)
{
    unsigned o = 0;
    for (int sh = 16; sh >= 0; sh -= 8) {
        double ca = (a >> sh) & 0xff, cb = (b >> sh) & 0xff, c = ca + (cb - ca) * t;
        o |= (unsigned)(c + 0.5) << sh;
    }
    return o;
}

/* the luminance of a colour, 0 to 255 (ITU-R BT.601 weights) */
static double rgb_lum(unsigned c) { return 0.299 * ((c >> 16) & 0xff) + 0.587 * ((c >> 8) & 0xff) + 0.114 * (c & 0xff); }

/* The cover's grey at the sky's light (parse.h): the grey as it is from 6
 * deg up; below, scaled toward black, at most to the clear colour's
 * luminance. */
static unsigned grey_lit(unsigned grey, unsigned clear, double elev)
{
    double k = rgb_lum(clear) / rgb_lum(grey), day = (elev + 6) / 12;
    if (day > k) k = day;
    return k >= 1 ? grey : rgb_mix(0, grey, k);
}

void sky_colors(double elev, int cover, int wet, unsigned *top, unsigned *horizon)
{
    int i = 0;
    double t = 0;
    if (!(elev > sky_elev[0])) elev = sky_elev[0];   /* below the first stop, or NaN: night */
    if (elev >= sky_elev[SKY_STOPS - 1]) i = SKY_STOPS - 2, t = 1;
    else {
        while (elev >= sky_elev[i + 1]) i++;
        t = (elev - sky_elev[i]) / (sky_elev[i + 1] - sky_elev[i]);
    }
    unsigned tp = rgb_mix(sky_top[i], sky_top[i + 1], t), hz = rgb_mix(sky_hor[i], sky_hor[i + 1], t);
    if (cover > 0) {
        double c = (cover > 100 ? 100 : cover) / 100.0 * 0.7;
        tp = rgb_mix(tp, grey_lit(0x5d6b80, tp, elev), c);
        hz = rgb_mix(hz, grey_lit(0x8a96a8, hz, elev), c);
    }
    if (wet) {
        tp = rgb_mix(tp, 0, 0.25);
        hz = rgb_mix(hz, 0, 0.25);
    }
    *top = tp;
    *horizon = hz;
}

int wmo_kind(int code)
{
    if (code < 0) return WK_NONE;
    if (code <= 1) return WK_CLEAR;
    if (code == 2) return WK_PARTLY;
    if (code == 45 || code == 48) return WK_FOG;
    if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return WK_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WK_SNOW;
    if (code >= 95 && code <= 99) return WK_STORM;
    return WK_CLOUD;
}

int wmo_wet(int code)
{
    int k = wmo_kind(code);
    return k == WK_RAIN || k == WK_STORM;
}

int wx_precip(int code, double mm, int *kind)
{
    int k = wmo_kind(code), n = 0;
    *kind = WP_DROPS;
    if (k == WK_SNOW) {
        *kind = WP_FLAKES;
        return 300;
    }
    if (code >= 51 && code <= 57) n = 300;
    else if (code == 61 || code == 66 || code == 80) n = 700;
    else if (code == 63 || code == 81) n = 1100;
    else if (code == 65 || code == 67 || code == 82 || k == WK_STORM) n = 1500;
    int m = !(mm > 0) ? 0 : mm < 0.5 ? 300 : mm < 2.5 ? 700 : mm < 7.6 ? 1100 : 1500;
    return m > n ? m : n;
}

int wx_icon(int kind, int is_day)
{
    switch (kind) {
    case WK_CLEAR: return is_day ? WI_SUN : WI_MOON_STARS;
    case WK_PARTLY: return is_day ? WI_CLOUD_SUN : WI_CLOUD_MOON;
    case WK_CLOUD: return WI_CLOUD;
    case WK_FOG: return WI_CLOUD_FOG;
    case WK_RAIN: return WI_CLOUD_RAIN;
    case WK_SNOW: return WI_CLOUD_SNOW;
    case WK_STORM: return WI_CLOUD_LIGHTNING;
    default: return WI_NONE;
    }
}

const char *wind_compass(int deg)
{
    static const char *const pt[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    if (deg < 0) return "";
    return pt[((deg % 360) * 2 + 45) / 90 % 8];
}

void wind_words(char *buf, int n, int deg, int gusts)
{
    const char *c = wind_compass(deg);
    if (c[0] && gusts >= 0) snprintf(buf, (size_t)n, "%s, gusts %d", c, gusts);
    else if (c[0]) snprintf(buf, (size_t)n, "%s", c);
    else if (gusts >= 0) snprintf(buf, (size_t)n, "Gusts %d", gusts);
    else if (n > 0) buf[0] = 0;
}
