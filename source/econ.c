/* The Economía page (econ.h, 0.6.0): page.h's econ_body and econ_bar, the
 * schedule and the replies. One glass tile: the title row, three columns
 * of rows (Precios, Dólar, Tasas y BCRA), then the Actividad rows and the
 * inflation chart of the last 13 months. The main thread only. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/icons.h>
#include "couch.h"
#include "page.h"
#include "board.h"
#include "clock.h"
#include "http.h"
#include "parse.h"
#include "start.h"
#include "econ.h"

#define PERIOD_US 1800000000LL       /* the reply every 30 min while the page shows */
#define RETRY_US 60000000LL          /* 60 s after a failure */
#define OPEN_US 600000000LL          /* the page's opening asks unless the reply came in the last 10 min */
#define GAP_US 10000000LL            /* SQUARE: at most once in 10 s */
#define LOG_ECON 20                  /* econ lines in couch.txt per start */
#define ROW_H 60

static float rnd(float x) { return floorf(x + 0.5f); }

static econ_data D;                  /* the last good reply */
static int have, last_ok = 1, fetches, pending;
static s64 got_at, asked_at;         /* the reply's system time; the last request's (0: none) */
static char words[160];              /* the last fetch's result in words */
static int was_econ, logged;

/* ---- the schedule and the pad -------------------------------------------------------------- */

static void ask(s64 now)
{
    asked_at = now ? now : 1;
    pending = 1;
    board_ask_econ();
}

void econ_tick(void)
{
    int opened = page == PAGE_ECON && !was_econ;
    was_econ = page == PAGE_ECON;
    if (page != PAGE_ECON || ambient || pending || net_state != NET_UP || !board_running()) return;
    s64 now = sysGetSystemTime(), since = now - asked_at;
    s64 period = !last_ok ? RETRY_US : PERIOD_US;
    if (!asked_at || since >= period || (opened && since >= OPEN_US)) ask(now);
}

void econ_refresh(void)
{
    s64 now = sysGetSystemTime();
    if (!pending && net_state == NET_UP && board_running() && (!asked_at || now - asked_at >= GAP_US)) ask(now);
}

/* ---- the replies ------------------------------------------------------------------------------- */

void econ_take(const board_reply *b)
{
    char what[300] = "", cn[24] = "no connection";
    pending = 0;
    fetches++;
    last_ok = b->prc == BP_OK;
    snprintf(words, sizeof words, "%s", b->words);
    if (last_ok) {
        D = *b->econ;
        have = 1;
        got_at = b->at;
        snprintf(what, sizeof what, "%d indicators, %d months of inflation, state %.8s%s%.96s%s%.100s", D.n, D.n_hist, D.feed.state[0] ? D.feed.state : "-",
                 D.feed.reason[0] ? ", " : "", D.feed.reason, D.missing[0] ? "; missing " : "", D.missing);
    }
    if (logged >= LOG_ECON) return;
    logged++;
    if (b->r->connect_us >= 0) snprintf(cn, sizeof cn, "connect %.2f s", (double)b->r->connect_us / 1e6);
    couch_log("econ %d at %.1f s: %.2f s (%s), HTTP %d, %d bytes: %s%s%s", fetches, (double)(b->at - t_start) / 1e6, (double)b->us / 1e6, cn,
              b->r->http, b->r->len, b->words, what[0] ? "; " : "", what);
}

/* ---- the numbers ------------------------------------------------------------------------------- */

/* v with dec decimals in the Argentine style: a thousands '.', a decimal ','. */
static void fmt_ar(double v, int dec, char *out, int n)
{
    char raw[48], whole[64];
    snprintf(raw, sizeof raw, "%.*f", dec, fabs(v));
    const char *dot = strchr(raw, '.');
    int wl = dot ? (int)(dot - raw) : (int)strlen(raw), j = 0;
    for (int i = 0; i < wl && j < (int)sizeof whole - 2; i++) {
        if (i && (wl - i) % 3 == 0) whole[j++] = '.';
        whole[j++] = raw[i];
    }
    whole[j] = 0;
    snprintf(out, (size_t)n, "%s%s%s%s", v < 0 ? "-" : "", whole, dot ? "," : "", dot ? dot + 1 : "");
}

/* The trailing zeros of a comma decimal dropped: 1,90 is 1,9; 24,00 is 24. */
static void trim_zeros(char *s)
{
    char *comma = strrchr(s, ',');
    if (!comma) return;
    char *e = s + strlen(s);
    while (e > comma + 1 && e[-1] == '0') *--e = 0;
    if (e == comma + 1) *comma = 0;
}

/* The value by its unit: 1,9%, $1.538,47, US$ 50.482 M, $46.804.518 M, 763 pts, 861,75. */
static void value_text(const econ_ind *x, char *out, int n)
{
    char t[48];
    if (!strcmp(x->unit, "%")) {
        fmt_ar(x->value, 2, t, sizeof t);
        trim_zeros(t);
        snprintf(out, (size_t)n, "%s%%", t);
    } else if (!strcmp(x->unit, "ARS")) {
        fmt_ar(x->value, fabs(x->value) >= 100000 ? 0 : 2, t, sizeof t);
        snprintf(out, (size_t)n, "$%s", t);
    } else if (!strcmp(x->unit, "USDM")) {
        fmt_ar(x->value, 0, t, sizeof t);
        snprintf(out, (size_t)n, "US$ %s M", t);
    } else if (!strcmp(x->unit, "ARSM")) {
        fmt_ar(x->value, 0, t, sizeof t);
        snprintf(out, (size_t)n, "$%s M", t);
    } else if (!strcmp(x->unit, "pts")) {
        fmt_ar(x->value, 0, t, sizeof t);
        snprintf(out, (size_t)n, "%s pts", t);
    } else {
        fmt_ar(x->value, 2, t, sizeof t);
        trim_zeros(t);
        snprintf(out, (size_t)n, "%s", t);
    }
}

/* The move since the previous reading: points for a rate (+0,20 pp), a
 * percentage for a level (+0,06%). 0 for none, 1 up, -1 down, 2 flat. */
static int change_text(const econ_ind *x, char *out, int n)
{
    char t[48];
    double d;
    out[0] = 0;
    if (!x->has_prev) return 0;
    if (!strcmp(x->unit, "%")) {
        d = x->value - x->prev;
        fmt_ar(d, 2, t, sizeof t);
        snprintf(out, (size_t)n, "%s%s pp", d >= 0 ? "+" : "", t);
    } else if (x->prev != 0) {
        d = (x->value / x->prev - 1) * 100;
        fmt_ar(d, 2, t, sizeof t);
        snprintf(out, (size_t)n, "%s%s%%", d >= 0 ? "+" : "", t);
    } else return 0;
    return d >= 0.005 ? 1 : d <= -0.005 ? -1 : 2;
}

static const char *const mon[12] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};

/* "2026-09-30" as "30 sep"; another text as it is. */
static void date_words(const char *ymd, char *out, int n)
{
    int y, m, d;
    if (sscanf(ymd, "%d-%d-%d", &y, &m, &d) == 3 && m >= 1 && m <= 12 && d >= 1 && d <= 31) snprintf(out, (size_t)n, "%d %s", d, mon[m - 1]);
    else snprintf(out, (size_t)n, "%s", ymd);
}

/* ---- the page ---------------------------------------------------------------------------------- */

typedef struct {
    float x, y, w, h;                /* the tile */
    float xl, xr;                    /* the inner edges */
    float top;                       /* the columns' top */
    float cw, cg;                    /* a column's width, the gap */
    float band;                      /* the Actividad band's top */
} elay;

static void layout(elay *L)
{
    int wide = page_wide();
    L->x = MG;
    L->y = page_top();
    L->w = SW - 2 * MG;
    L->h = page_bar_y() - 12 - L->y;
    L->xl = L->x + 36;
    L->xr = L->x + L->w - 36;
    L->top = L->y + (wide ? 92 : 80);
    L->cg = wide ? 36 : 24;
    L->cw = (L->xr - L->xl - 2 * L->cg) / 3;
    L->band = L->top + 7 * ROW_H + (wide ? 36 : 24);
}

static const char *const sections[4] = {"Precios", "Dólar", "Tasas y BCRA", "Actividad"};

/* One row: the label, under it the date and the move, the value on the right. */
static void row(float x, float y, float w, const econ_ind *it)
{
    char v[48], c[48], d[24];
    value_text(it, v, sizeof v);
    int dir = change_text(it, c, sizeof c);
    date_words(it->date, d, sizeof d);
    float vw = (float)text_w(F_MED | TNUM, v);
    text_fit(x, y + 2, F_SMALL, WHITE, w - vw - 16, "%s", it->label);
    float y2 = y + 2 + (float)line_h(F_SMALL) + 2;
    float xe = text(x, y2, F_SMALL, DIM, "%s", d);
    if (dir) {
        u32 col = dir == 1 ? UP : dir == -1 ? DOWN : DIM;
        xe += 14;
        if (!icons_rc && dir != 2) {
            icon(dir == 1 ? IC_S_CARET_UP : IC_S_CARET_DOWN, xe + 7, y2 + (float)line_h(F_SMALL) / 2, col);
            xe += 18;
        }
        text(xe, y2, F_SMALL | TNUM, col, "%s", c);
    }
    text_r(x + w, y + 6, F_MED | TNUM, WHITE, "%s", v);
}

/* A section's title, its rows under it; the y after them. */
static float column(const elay *L, float x, float y, float w, const char *section, int max)
{
    int k = 0;
    text(x, y, F_SMALL, BLUE, "%s", section);
    y += (float)line_h(F_SMALL) + 10;
    for (int i = 0; i < D.n && k < max; i++) {
        if (strcmp(D.ind[i].section, section)) continue;
        if (y + ROW_H > L->y + L->h - 8) break;
        row(x, y, w, &D.ind[i]);
        y += ROW_H;
        k++;
    }
    if (!k) {
        text(x, y + 4, F_SMALL, DIM, "%s", "no data");
        y += ROW_H;
    }
    return y;
}

/* The inflation chart: a bar per month, the last one lit with its value,
 * the first and the last months named (counted back from the inflation
 * reading's month). */
static void chart(const elay *L, float x, float y, float w, float h)
{
    int n = D.n_hist;
    text(x, y, F_SMALL, BLUE, "Inflación mensual, %d meses", n);
    float top = y + (float)line_h(F_SMALL) + 14, bot = y + h - (float)line_h(F_SMALL) - 8;
    if (n < 2 || bot - top < 40) {
        text(x, top, F_SMALL, DIM, "no data");
        return;
    }
    double hi = 0;
    for (int i = 0; i < n; i++) if (D.hist[i] > hi) hi = D.hist[i];
    if (hi <= 0) hi = 1;
    float step = (w - 8) / (float)n, bw = rnd(step * 0.62f);
    int ey = 0, em = 0;
    for (int i = 0; i < D.n; i++)
        if (!strcmp(D.ind[i].key, D.hist_key)) sscanf(D.ind[i].date, "%d-%d", &ey, &em);
    for (int i = 0; i < n; i++) {
        float bh = rnd((float)(D.hist[i] / hi) * (bot - top - 28)), bx = rnd(x + 4 + i * step + (step - bw) / 2);
        if (bh < 2) bh = 2;
        round_rect(bx, bot - bh, bw, bh, 3, i == n - 1 ? BLUE : 0xffffff40);
        if (em && (i == 0 || i == n - 1)) {
            int m = em - (n - 1 - i), yy = ey;
            while (m < 1) { m += 12; yy--; }
            char t[24];
            snprintf(t, sizeof t, "%s %02d", mon[m - 1], yy % 100);
            if (i == 0) text(bx, bot + 6, F_SMALL, DIM, "%s", t);
            else text_r(bx + bw, bot + 6, F_SMALL, DIM, "%s", t);
        }
        if (i == n - 1) {
            char t[24];
            fmt_ar(D.hist[i], 2, t, sizeof t);
            trim_zeros(t);
            text_r(bx + bw, bot - bh - (float)line_h(F_SMALL) - 2, F_SMALL | TNUM, WHITE, "%s%%", t);
        }
    }
    (void)L;
}

/* One line in the tile's middle: an error, the setup. */
static void tile_words(const elay *L, const char *line, u32 c)
{
    if (!icons_rc) icon(IC_S_INFO, L->xl + 14, L->top + 40, DIM);
    text_wrap(L->xl + (icons_rc ? 0 : 40), L->top + 24, F_MED, c, L->xr - L->xl - 40, 3, line);
}

void econ_body(void)
{
    elay L;
    layout(&L);
    int dy = (int)rnd(SA_RISE * (1 - start_k(SA_LEFT)));
    shift_push(0, dy);
    glass_tile(L.x, L.y, L.w, L.h);
    text(L.xl, L.y + 26, F_MED, WHITE, "Economía argentina");
    rect(L.xl, L.top - 10, L.xr - L.xl, 1, 0xffffff30);
    char line[300];
    if (!board_url[0]) {
        tile_words(&L, "No Board URL yet: the Board page has the steps.", YELLOW);
    } else if (!have && !last_ok) {
        if (strstr(words, "HTTP 404")) snprintf(line, sizeof line, "The Board's Worker has no economy data (HTTP 404): this page needs a Worker with /economia.");
        else snprintf(line, sizeof line, "The economy data did not load: %s. SQUARE tries again.", words);
        tile_words(&L, line, YELLOW);
    } else if (!have) {                                      /* before the first reply: the skeleton */
        for (int c = 0; c < 3; c++) {
            float x = L.xl + c * (L.cw + L.cg);
            skel_bar(x, L.top + 4, 160, 22);
            for (int i = 0; i < (c == 0 ? 7 : c == 1 ? 4 : 6); i++) {
                float y = L.top + 40 + i * ROW_H;
                skel_bar(x, y + 4, L.cw * 0.5f, 22);
                skel_bar(x + L.cw - 120, y + 4, 120, 26);
            }
        }
    } else if (!D.n) {
        snprintf(line, sizeof line, "No economy data: %s.", D.feed.reason[0] ? D.feed.reason : "the reply has none");
        tile_words(&L, line, YELLOW);
    } else {
        for (int c = 0; c < 3; c++) column(&L, L.xl + c * (L.cw + L.cg), L.top, L.cw, sections[c], 7);
        rect(L.xl, L.band - 16, L.xr - L.xl, 1, 0xffffff30);
        float bh = L.y + L.h - 16 - L.band;
        column(&L, L.xl, L.band, L.cw, sections[3], 3);
        chart(&L, L.xl + L.cw + L.cg, L.band, 2 * L.cw + L.cg, bh);
    }
    shift_pop();
}

void econ_bar(float y)
{
    static const char hints[] = "L1/R1 pages  SQUARE refresh  START exit";
    char line[300], t[24];
    u32 c = GREY;
    long age = have ? (long)((sysGetSystemTime() - got_at) / 1000000) : 0;
    if (!board_url[0] || net_state != NET_UP || !board_running()) c = board_status_words(line, sizeof line);
    else if (pending && !have) snprintf(line, sizeof line, "Fetching the economy data ...");
    else if (!last_ok && have) {
        clock_words(t, sizeof t, clock_utc() - age, 0);
        snprintf(line, sizeof line, "The economy data did not update: %s. It is the data of %s.", words, t);
        c = YELLOW;
    } else if (!have) c = board_status_words(line, sizeof line);
    else if (!strcmp(D.feed.state, "stale")) {
        char a[24];
        age_words(a, sizeof a, (D.feed.age >= 0 ? D.feed.age : 0) + age);
        snprintf(line, sizeof line, "%s. The data is %s old.", D.feed.reason[0] ? D.feed.reason : "The source failed", a);
        c = YELLOW;
    } else {
        clock_words(t, sizeof t, clock_utc() - age, 0);
        if (D.missing[0]) {
            snprintf(line, sizeof line, "The data of %s; some sources gave nothing (%s).", t, D.missing);
            c = YELLOW;
        } else snprintf(line, sizeof line, "The data of %s. BCRA, INDEC, DolarAPI and ArgentinaDatos.", t);
    }
    bar_draw(y, line, c, board_url_note, hints);
}
