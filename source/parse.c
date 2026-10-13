/* Couch's pure functions (parse.h). The C library only: the host check
 * builds this file with the host's cc, the preview and ppu-gcc as it is. */
#include <stdio.h>
#include <string.h>
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

int board_parse(const char *body, int n, int fold, board_data *b)
{
    static const char magic[] = "couch-board ";
    const int ml = (int)sizeof magic - 1;
    const char *p = body, *end = body + n, *ls;
    int ll;
    long v;
    memset(b, 0, sizeof *b);
    b->usd_feed.age = b->wx_feed.age = b->news_feed.age = -1;
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
