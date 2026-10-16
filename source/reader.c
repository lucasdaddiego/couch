/* The news reader (reader.h): page.h's news_body and news_bar, the pad on
 * the News page, the list's schedule, and the replies. Every motion reads
 * sky_time (the preview's frame clock); the scrolls ease toward their
 * target (parse.h's scroll_follow) and draw through stext.h, so the text
 * and the cards move by fractions of a pixel, cut and faded at the edges
 * of the scrolling band. The main thread only. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fx.h>
#include <ps3gfx/icons.h>
#include "couch.h"
#include "page.h"
#include "board.h"
#include "clock.h"
#include "http.h"
#include "parse.h"
#include "sky.h"
#include "spu_fx_run.h"
#include "start.h"
#include "stext.h"
#include "reader.h"

#define LIST_PERIOD_US 300000000LL   /* the list every 5 min while the News page shows */
#define LIST_RETRY_US 60000000LL     /* 60 s after a failure */
#define LIST_OPEN_US 60000000LL      /* the page's opening asks unless a list was asked in the last 60 s */
#define LIST_GAP_US 10000000LL       /* SQUARE: at most once in 10 s */
#define TAU 0.075                    /* the scrolls' ease: the rest falls by e every 75 ms (about 95 % in 225 ms) */
#define FADE 36.0f                   /* the band's fade at an edge where the content goes on */
#define KEEP 44.0                    /* the room kept around the selection where the list goes on */
#define REP_FIRST 0.35               /* a held direction repeats after 350 ms, */
#define REP_EVERY 0.11               /* then every 110 ms */
#define HOLD_PX_S 900.0              /* a held UP or DOWN in an article: 900 px a second */
#define LOG_READER 60                /* reader lines in couch.txt per start */
#define PI_F 3.14159265f

static float rnd(float x) { return floorf(x + 0.5f); }

/* ---- the state ------------------------------------------------------------------------------ */

static news_data list;               /* the last good /news reply */
static int list_have, list_last_ok = 1, list_fetches, list_pending;
static s64 list_at, list_asked;      /* its system time; the last request's (0: none yet) */
static char list_words[160];         /* the last /news fetch's result in words */
static int was_news;                 /* the News page showed in the last frame */

static int sel;                      /* the selected item */
static char sel_id[9];
static double ls_pos, ls_target;     /* the list's scroll, px */
static int keep_view;                /* the next draw moves the target to show the selection */
static double t_last = -1, sel_t = -10, note_t = -10;
static char note[160];               /* a short note in the bar (3 s) */
static unsigned rep_btn;
static double rep_next;
static int logged;

enum { ART_NONE, ART_LOADING, ART_SHOWN, ART_ERROR };
static int art;
static char art_id[9], art_title[336], art_source[32], art_cat[20];
static article_data A;               /* the open article */
static char art_err[220];
static double as_pos, as_target, hold_t0 = -1;
static float a_view = 1, a_total = 1, a_step = 50;   /* the article's band, content and line step, from the last draw */

/* the article's rows: the title's lines, the meta row, the state line, the rule, the paragraphs' lines, the end */
enum { R_TITLE, R_META, R_STATE, R_RULE, R_LINE, R_END };
#define AROWS 900
static struct {
    short kind, para;
    int off, len;                    /* a line's bytes: of A.title (R_TITLE) or of A.text */
    float y;
} row[AROWS];
static int nrows, wrapped_w;         /* the column width the rows were made for (0: none) */
static float a_col;

/* the QR code of the link (Health's help.c: the dark runs of each row) */
#define QR_SIDE (QR_MAX_VERSION * 4 + 17)
static int qr_ok, qr_side, qr_nruns;
static struct {
    unsigned char r, c, len;
} qr_run[QR_SIDE * (QR_SIDE + 1) / 2];

/* ---- the items: the list's, or the Board's 8 before it ------------------------------------- */

typedef struct {
    const char *id, *source, *title;
    char cat[20];
    long age;                        /* seconds now, -1 none */
} item_view;

static int n_items(void) { return list_have ? list.n : board_have ? board_cur.n_news : 0; }

static void item_at(int i, item_view *v)
{
    memset(v, 0, sizeof *v);
    v->age = -1;
    if (list_have) {
        const news_item *it = &list.item[i];
        v->id = it->id;
        v->source = it->source;
        v->title = it->title;
        snprintf(v->cat, sizeof v->cat, "%s", it->category);
        if (v->cat[0] >= 'a' && v->cat[0] <= 'z') v->cat[0] = (char)(v->cat[0] - 32);   /* "economy": "Economy" */
        if (it->age >= 0) v->age = it->age + (long)((sysGetSystemTime() - list_at) / 1000000);
    } else {
        const board_news *b = &board_cur.news[i];
        v->id = "";
        v->source = b->source;
        v->title = b->title;
        const char *c = news_category(b->source);
        snprintf(v->cat, sizeof v->cat, "%s", c ? c : "");
        if (b->age >= 0) v->age = b->age + board_age();
    }
}

/* ---- the schedule and the pad ----------------------------------------------------------------- */

static void ask_list(s64 now)
{
    list_asked = now ? now : 1;
    list_pending = 1;
    board_ask_news();
}

int reader_reading(void) { return art != ART_NONE && page == PAGE_NEWS; }

void reader_tick(void)
{
    double t = sky_time(), dt = t_last < 0 ? 0 : t - t_last;
    t_last = t;
    if (dt > 0.1) dt = 0.1;                  /* a long frame (a shot): no jump */
    if (art == ART_SHOWN && page == PAGE_NEWS && hold_t0 >= 0) {   /* a held UP or DOWN: a steady glide, which the ease follows */
        if (!(held & (BTN_UP | BTN_DOWN))) {
            hold_t0 = -1;
            as_target = scroll_clamp(floor(as_target + 0.5), a_view, a_total);   /* at rest on a whole pixel: the text lands 1:1 */
        } else if (t - hold_t0 >= REP_FIRST) {
            as_target += (held & BTN_DOWN ? 1 : -1) * HOLD_PX_S * dt;
            as_target = scroll_clamp(as_target, a_view, a_total);
        }
    }
    ls_pos = scroll_follow(ls_pos, ls_target, dt, TAU);
    as_pos = scroll_follow(as_pos, as_target, dt, TAU);
    int opened = page == PAGE_NEWS && !was_news;
    was_news = page == PAGE_NEWS;
    if (page != PAGE_NEWS || ambient || art != ART_NONE || list_pending || net_state != NET_UP || !board_running()) return;
    s64 now = sysGetSystemTime(), since = now - list_asked;
    if (!list_asked || since >= (list_last_ok ? LIST_PERIOD_US : LIST_RETRY_US) || (opened && since >= LIST_OPEN_US)) ask_list(now);
}

void reader_refresh(void)
{
    s64 now = sysGetSystemTime();
    if (art == ART_NONE && !list_pending && net_state == NET_UP && board_running() && (!list_asked || now - list_asked >= LIST_GAP_US))
        ask_list(now);
}

/* The directions of mask that went down, and a held one again after
 * REP_FIRST, then every REP_EVERY. */
static unsigned repeat(unsigned mask, double t)
{
    unsigned p = pressed & mask;
    if (p) {
        rep_btn = p & (0u - p);              /* the lowest: one direction at a time */
        rep_next = t + REP_FIRST;
        return rep_btn;
    }
    if (!rep_btn || !(held & rep_btn)) {
        rep_btn = 0;
        return 0;
    }
    if (t < rep_next) return 0;
    rep_next = rep_next + REP_EVERY > t ? rep_next + REP_EVERY : t + REP_EVERY;
    return rep_btn;
}

static void set_note(const char *s)
{
    snprintf(note, sizeof note, "%s", s);
    note_t = sky_time();
}

static void open_article(void)
{
    item_view v;
    if (sel < 0 || sel >= n_items()) return;
    item_at(sel, &v);
    if (!v.id || !v.id[0]) {
        set_note(list_have ? "This headline has no id: it does not open." : "The full list is not in yet: a headline opens once it is.");
        return;
    }
    art = ART_LOADING;
    snprintf(art_id, sizeof art_id, "%s", v.id);
    snprintf(art_title, sizeof art_title, "%s", v.title);
    snprintf(art_source, sizeof art_source, "%s", v.source);
    snprintf(art_cat, sizeof art_cat, "%s", v.cat);
    as_pos = as_target = 0;
    hold_t0 = -1;
    nrows = wrapped_w = 0;
    qr_ok = 0;
    if (net_state != NET_UP || !board_running()) {
        art = ART_ERROR;
        snprintf(art_err, sizeof art_err, "The article cannot load: the network is not up.");
        return;
    }
    board_ask_article(art_id);
}

int reader_input(int circle)
{
    double t = sky_time();
    if (art != ART_NONE) {
        if (circle) {                        /* back to the list, its selection and its scroll as they were */
            art = ART_NONE;
            hold_t0 = -1;
            keep_view = 1;
            return 1;
        }
        if (art != ART_SHOWN) return (pressed & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_L2 | BTN_R2 | BTN_CROSS)) != 0;
        double page_px = a_view - 2 * a_step > a_step ? a_view - 2 * a_step : a_view;
        int used = 0;
        if (pressed & (BTN_UP | BTN_DOWN)) {             /* a press: three lines; held, reader_tick glides on */
            as_target += (pressed & BTN_DOWN ? 1 : -1) * 3 * a_step;
            hold_t0 = t;
            used = 1;
        }
        if (pressed & (BTN_RIGHT | BTN_R2 | BTN_LEFT | BTN_L2)) {
            as_target += (pressed & (BTN_RIGHT | BTN_R2) ? 1 : -1) * page_px;
            used = 1;
        }
        as_target = scroll_clamp(as_target, a_view, a_total);
        return used || (pressed & BTN_CROSS);
    }
    int n = n_items();
    if (!n) return 0;
    unsigned d = repeat(BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT, t);
    if (d) {
        int to = grid_move(sel, n, d == BTN_UP ? NAV_UP : d == BTN_DOWN ? NAV_DOWN : d == BTN_LEFT ? NAV_LEFT : NAV_RIGHT);
        if (to != sel) {
            sel = to;
            sel_t = t;
            item_view v;
            item_at(sel, &v);
            snprintf(sel_id, sizeof sel_id, "%s", v.id ? v.id : "");
        }
        keep_view = 1;
        return 1;
    }
    if (pressed & BTN_CROSS) {
        open_article();
        return 1;
    }
    return 0;
}

/* ---- the replies --------------------------------------------------------------------------------- */

/* One couch.txt line a reply (the first LOG_READER): as the Board's fetch
 * lines, then what came. */
static void log_reply(const board_reply *b, const char *head, const char *tail)
{
    char cn[24] = "no connection";
    if (logged >= LOG_READER) return;
    logged++;
    if (b->r->connect_us >= 0) snprintf(cn, sizeof cn, "connect %.2f s", (double)b->r->connect_us / 1e6);
    couch_log("%s at %.1f s: %.2f s (%s), HTTP %d, %d bytes: %s%s%s", head, (double)(b->at - t_start) / 1e6, (double)b->us / 1e6, cn,
              b->r->http, b->r->len, b->words, tail[0] ? "; " : "", tail);
}

/* The QR code's dark runs, from qrcodegen's buffer (Health's build_qr). */
static void qr_runs(const unsigned char *qr)
{
    qr_nruns = 0;
    qr_side = qrcodegen_getSize(qr);
    if (qr_side > QR_SIDE) qr_side = 0;
    for (int r = 0; r < qr_side; r++)
        for (int c = 0; c < qr_side; c++) {
            if (!qrcodegen_getModule(qr, c, r)) continue;
            int c2 = c;
            while (c2 + 1 < qr_side && qrcodegen_getModule(qr, c2 + 1, r)) c2++;
            qr_run[qr_nruns].r = (unsigned char)r;
            qr_run[qr_nruns].c = (unsigned char)c;
            qr_run[qr_nruns++].len = (unsigned char)(c2 - c + 1);
            c = c2;
        }
    qr_ok = qr_side > 0;
}

void reader_take(const board_reply *b)
{
    char head[64], what[240] = "";
    if (b->kind == REQ_NEWS) {
        list_pending = 0;
        list_fetches++;
        list_last_ok = b->prc == BP_OK;
        snprintf(list_words, sizeof list_words, "%s", b->words);
        if (list_last_ok) {
            list = *b->news;
            list_have = 1;
            list_at = b->at;
            int found = -1;                  /* the selection stays on its headline when it is still listed */
            for (int i = 0; sel_id[0] && i < list.n; i++)
                if (!strcmp(list.item[i].id, sel_id)) found = i;
            sel = found >= 0 ? found : sel < list.n ? sel : list.n > 0 ? list.n - 1 : 0;
            snprintf(sel_id, sizeof sel_id, "%s", list.n ? list.item[sel].id : "");
            keep_view = 1;
            snprintf(what, sizeof what, "%d items, state %s%s%s", list.n, list.feed.state[0] ? list.feed.state : "-", list.feed.reason[0] ? ", " : "",
                     list.feed.reason);
        }
        snprintf(head, sizeof head, "news %d", list_fetches);
        log_reply(b, head, what);
        return;
    }
    const article_data *a = b->art;
    int open = art == ART_LOADING && !strcmp(b->id, art_id);
    if (b->prc == BP_OK)
        snprintf(what, sizeof what, "state %s%s%s, %d paragraphs%s, link %d bytes, QR %s", a->state, a->reason[0] ? " " : "", a->reason, a->n_para,
                 a->text_cut ? " (cut here)" : "", (int)strlen(a->link), b->qr_ok ? "ok" : a->link[0] ? "too long" : "none");
    snprintf(head, sizeof head, "article %s%s", b->id, open ? "" : " (closed: dropped)");
    log_reply(b, head, what);
    if (!open) return;
    if (b->prc == BP_OK && strcmp(a->state, "none")) {
        A = *a;
        if (!A.title[0]) snprintf(A.title, sizeof A.title, "%s", art_title);
        if (!A.source[0]) snprintf(A.source, sizeof A.source, "%s", art_source);
        qr_ok = 0;
        if (b->qr_ok) qr_runs(b->qr);
        nrows = wrapped_w = 0;
        as_pos = as_target = 0;
        art = ART_SHOWN;
    } else {
        art = ART_ERROR;
        if (b->prc == BP_OK)
            snprintf(art_err, sizeof art_err, "The Worker has no article for this headline: %s.", a->reason[0] ? a->reason : "no reason given");
        else snprintf(art_err, sizeof art_err, "The article did not load: %s.", b->words);
    }
}

/* ---- the drawing's parts ----------------------------------------------------------------------- */

/* A text's colour inside an element of alpha k (stext_alpha): the band
 * fades the text itself, so it only goes when the element goes. */
static u32 vis(u32 c, float k) { return k > 0 ? c : c & ~0xffu; }

/* A source's pill at any fractional y: the name on its colour, faded with k; its end x. */
static float pill(float x, float y, float h, const char *source, float k)
{
    float w = stext_w(F_SMALL, source, (int)strlen(source)) + 24;
    if (w > 220) w = 220;
    u32 c = alpha_times(source_colour(source), k);
    if (k > 0) {
        round_rect(x, y, w, h, h / 2, c);
        aa_corners(x, y, w, h, h / 2, c);
    }
    stext_fit(x + 12, y + (h - (float)line_h(F_SMALL)) / 2, F_SMALL, vis(WHITE, k), w - 22, source);
    return x + w;
}

/* The category, an outlined pill; its end x. */
static float tag(float x, float y, float h, const char *cat, float k)
{
    if (!cat[0]) return x;
    float w = stext_w(F_SMALL, cat, (int)strlen(cat)) + 22;
    if (k > 0) round_frame(x, y, w, h, h / 2, 1, alpha_times(0xffffff50, k));
    stext(x + 11, y + (h - (float)line_h(F_SMALL)) / 2, F_SMALL, vis(GREY, k), cat, (int)strlen(cat));
    return x + w;
}

/* The age with the clock icon, right-aligned at xr; its left x. */
static float age_at(float xr, float y, float h, long a, float k)
{
    char w[24];
    if (a < 0) return xr;
    age_words(w, sizeof w, a);
    float x = stext_r(xr, y + (h - (float)line_h(F_SMALL)) / 2, F_SMALL, vis(GREY, k), w);
    if (!icons_rc && k > 0) icon_rot(IC_S_CLOCK, x - 16, y + h / 2, 0, 0.75f, alpha_times(GREY, k));
    return x - 30;
}

static float measure(const char *s, int len, void *ctx) { return stext_w(*(const int *)ctx, s, len); }

/* A title in up to maxl lines of font at x, y (any fraction), the last one
 * ending in "..." when the text goes on; the lines drawn. */
static int title_lines(float x, float y, int font, u32 c, float maxw, int maxl, const char *s)
{
    int off[4], len[4], n = (int)strlen(s), f = font;
    if (maxl > 3) maxl = 3;
    int k = wrap_lines(s, n, maxw, measure, &f, off, len, maxl + 1);
    float step = (float)wrap_step(font);
    for (int i = 0; i < k && i < maxl; i++) {
        if (i == maxl - 1 && k > maxl) stext_fit(x, y + (float)i * step, font, c, maxw, s + off[i]);
        else stext(x, y + (float)i * step, font, c, s + off[i], len[i]);
    }
    return k < maxl ? k : maxl;
}

static float lines_h(int font, int n) { return (float)((n - 1) * wrap_step(font) + line_h(font)); }

/* The selection: a lighter glass and a ring 4 px out, brightening for 150 ms after a move. */
static u32 sel_tint(void)
{
    u32 a = glass_tint() & 255;
    return (glass_tint() & ~0xffu) | (a > 0x40 ? a - 0x40 : 0);
}

static void sel_ring(float x, float y, float w, float h)
{
    float k = stext_alpha(y - 6, y + h + 6), p = (float)((sky_time() - sel_t) / 0.15);
    if (k <= 0) return;
    if (p > 1) p = 1;
    if (p < 0) p = 0;
    round_frame(x - 4, y - 4, w + 8, h + 8, TR + 4, 3, alpha_times(0xffffffe0, k * (0.55f + 0.45f * p)));
    aa_corners(x - 4, y - 4, w + 8, h + 8, TR + 4, alpha_times(0xffffff60, k));
}

/* The newest headline: the featured card (stage 10's), large, two lines. */
static void featured(float x, float y, float w, float h, const item_view *v, int on)
{
    mglass(x, y, w, h, TR, on ? sel_tint() : glass_tint());
    float k = stext_alpha(y, y + h);
    u32 c = source_colour(v->source);
    if (k > 0) rect(x + TR, y, w - 2 * TR, 3, alpha_times(c, k));
    if (!icons_rc && k > 0) icon_rot(IC_S_NEWSPAPER, x + w - 90, y + h / 2, 0, 3.0f, alpha_times(0xffffff14, k));
    float pk = stext_alpha(y + 22, y + 52);
    float xe = pill(x + 28, y + 22, 30, v->source, pk);
    xe = tag(xe + 12, y + 22, 30, v->cat, pk);
    stext(xe + 16, y + 24, F_SMALL, vis(BLUE, pk), "Latest", 6);
    age_at(x + w - 28, y + 22, 30, v->age, pk);
    float ty = y + 22 + 30 + 14, room = y + h - 12 - ty;
    int font = room >= lines_h(F_TITLE, 2) ? F_TITLE : room >= lines_h(F_MED, 2) ? F_MED : F_BODY;
    title_lines(x + 28, ty, font, WHITE, w - 56 - 120, room >= lines_h(font, 2) ? 2 : 1, v->title);
}

/* A card of the grid: the pill, the category, the age, the title in two lines. */
static void news_card(float x, float y, float w, float h, const item_view *v, int on)
{
    mglass(x, y, w, h, TR, on ? sel_tint() : glass_tint());
    int wide = page_wide();
    float tp = wide ? 12 : 10, ph = wide ? 30 : 26, pk = stext_alpha(y + tp, y + tp + ph);
    float xe = pill(x + 20, y + tp, ph, v->source, pk);
    tag(xe + 10, y + tp, ph, v->cat, pk);
    age_at(x + w - 20, y + tp, ph, v->age, pk);
    float ty = y + tp + ph + (wide ? 8 : 6), room = y + h - (wide ? 8 : 6) - ty;
    int font = room >= lines_h(F_MED, 2) ? F_MED : F_BODY;
    title_lines(x + 20, ty, font, WHITE, w - 40, room >= lines_h(font, 2) ? 2 : 1, v->title);
}

/* The scroll bar at x, beside the band from y to y + h: a track, and the
 * part in view. */
static void scroll_bar(float x, float y, float h, double pos, double view, double total)
{
    if (!(total > view + 1)) return;
    float th = (float)(h * view / total), ty;
    if (th < 40) th = 40;
    ty = y + (float)((h - th) * pos / (total - view));
    round_rect(x, y, 6, h, 3, 0xffffff18);
    round_rect(x, ty, 6, th, 3, 0xffffff90);
    aa_corners(x, ty, 6, th, 3, 0xffffff90);
}

/* The list's layout (stage 10's: the featured card a quarter of the height,
 * then rows of two cards, four rows in view): the place of item i in the
 * content (from 0). */
typedef struct {
    float x, w, top, bot, ch, fh, gw, rh;
} lay;

static void list_layout(lay *L)
{
    L->top = page_top();
    L->bot = page_bar_y() - 12;
    L->x = MG;
    L->w = SW - 2 * MG;
    L->ch = L->bot - L->top;
    L->fh = rnd(L->ch * 0.25f);
    L->gw = (L->w - G) / 2;
    L->rh = (L->ch - L->fh - RG - 3 * RG) / 4;
}

static void item_box(const lay *L, int i, float *x, float *y, float *w, float *h)
{
    if (i == 0) {
        *x = L->x, *y = 0, *w = L->w, *h = L->fh;
        return;
    }
    int r = grid_row(i) - 1, c = (i - 1) % 2;
    *x = rnd(L->x + (float)c * (L->gw + G));
    *y = rnd(L->fh + RG + (float)r * (L->rh + RG));          /* whole pixels: the text at rest lands 1:1 */
    *w = rnd(L->gw);
    *h = rnd(L->rh);
}

static float list_total(const lay *L, int n)
{
    float x, y, w, h;
    item_box(L, n - 1, &x, &y, &w, &h);
    return y + h;
}

/* ---- the list ------------------------------------------------------------------------------------ */

static void list_body(void)
{
    lay L;
    list_layout(&L);
    int n = n_items();
    int dyl = (int)rnd(SA_RISE * (1 - start_k(SA_LEFT))), dyr = (int)rnd(SA_RISE * (1 - start_k(SA_RIGHT)));
    if (!board_have && !list_have) {                         /* before the first reply: stage 10's skeleton */
        float gw = L.gw;
        shift_push(0, dyl);
        glass_tile(L.x, L.top, L.w, L.fh);
        skel_bar(L.x + 28, L.top + 22, 130, 30);
        skel_bar(L.x + 28, L.top + 76, L.w * 0.62f, 40);
        skel_bar(L.x + 28, L.top + 130, L.w * 0.4f, 40);
        shift_pop();
        for (int i = 0; i < 8; i++) {
            shift_push(0, i % 2 ? dyr : dyl);
            float cx = rnd(L.x + (float)(i % 2) * (gw + G)), cy = rnd(L.top + L.fh + RG + (float)(i / 2) * (L.rh + RG));
            glass_tile(cx, cy, rnd(gw), rnd(L.rh));
            skel_bar(cx + 20, cy + 12, 110, 28);
            skel_bar(cx + 20, cy + 54, gw * 0.8f, 26);
            if (L.rh > 110) skel_bar(cx + 20, cy + 90, gw * 0.5f, 26);
            shift_pop();
        }
        return;
    }
    if (!n) {                                                /* no headlines: one wide tile with the reason */
        char line[200];
        const board_feed *f = list_have ? &list.feed : &board_cur.news_feed;
        if (!f->state[0]) snprintf(line, sizeof line, "The Board's Worker sends no news (no NEWS_FEEDS).");
        else snprintf(line, sizeof line, "No news data: %s.", f->reason[0] ? f->reason : "the reply has none");
        shift_push(0, dyl);
        glass_tile(L.x, L.top, L.w, 200);
        if (!icons_rc) icon(IC_S_NEWSPAPER, L.x + 46, L.top + 52, DIM);
        text(L.x + 76, L.top + 34, F_MED, WHITE, "News");
        text_wrap(L.x + 32, L.top + 100, F_BODY, YELLOW, L.w - 64, 2, line);
        shift_pop();
        return;
    }
    if (sel >= n) sel = n - 1;
    float total = list_total(&L, n), view = L.ch;
    if (keep_view) {
        float x, y, w, h;
        item_box(&L, sel, &x, &y, &w, &h);
        ls_target = scroll_keep(ls_target, y, y + h, view, total, KEEP);
        keep_view = 0;
    }
    ls_target = scroll_clamp(ls_target, view, total);
    if (ls_pos > total - view && total > view) ls_pos = total - view;   /* a shorter list: no empty end */
    if (total <= view) ls_pos = 0;
    double s = ls_pos;
    /* The band: 8 px more at each end for the selection's ring. Its fades
     * grow with how far a card crosses its edge (up to FADE): none at rest
     * when no card crosses (the first screen: four whole rows), and no step
     * when one starts to cross. */
    float bt = L.top - 8, bb = L.bot + 8, ft = 0, fb = 0;
    for (int i = 0; i < n; i++) {
        float x, y, w, h;
        item_box(&L, i, &x, &y, &w, &h);
        float y0 = L.top + y - (float)s, y1 = y0 + h;
        if (y0 < bt && y1 > bt && bt - y0 > ft) ft = bt - y0;
        if (y0 < bb && y1 > bb && y1 - bb > fb) fb = y1 - bb;
    }
    ft = ft > FADE ? FADE : ft;
    fb = fb > FADE ? FADE : fb;
    for (int col = 0; col < 2; col++) {                      /* the left column (and the featured card), then the right */
        shift_push(0, col ? dyr : dyl);
        stext_band(bt, bb, ft > 0 ? ft + 8 : 0, fb > 0 ? fb + 8 : 0);
        for (int i = 0; i < n; i++) {
            if ((i == 0 ? 0 : (i - 1) % 2) != col) continue;
            float x, y, w, h;
            item_box(&L, i, &x, &y, &w, &h);
            float sy = L.top + y - (float)s;
            if (sy + h < L.top - 8 || sy > L.bot + 8) continue;
            item_view v;
            item_at(i, &v);
            if (i == 0) featured(x, sy, w, h, &v, i == sel);
            else news_card(x, sy, w, h, &v, i == sel);
            if (i == sel) sel_ring(x, sy, w, h);
        }
        stext_band_off();
        shift_pop();
    }
    scroll_bar(SW - MG + 16, L.top, L.ch, s, view, total);
}

/* ---- the article --------------------------------------------------------------------------------- */

/* The article's layout: the panel over the content area; the text column
 * on its left, the QR code's column on its right. */
typedef struct {
    float x, y, w, h, pad, col_x, col_w, side_x, side_w, bt, bb;
} alay;

static void art_layout(alay *L)
{
    int wide = page_wide();
    L->x = MG;
    L->y = page_top();
    L->w = SW - 2 * MG;
    L->h = page_bar_y() - 12 - L->y;
    L->pad = wide ? 44 : 36;
    L->side_w = wide ? 400 : 340;
    L->side_x = L->x + L->w - L->pad - L->side_w;
    L->col_x = L->x + L->pad + (wide ? 16 : 0);
    L->col_w = L->side_x - 56 - L->col_x;
    if (L->col_w > 1180) L->col_w = 1180;                    /* about 75 characters a line in F_MED */
    L->bt = L->y + 24;
    L->bb = L->y + L->h - 24;
}

#define BODY_FONT F_MED
#define BODY_COLOUR 0xe8edf5ff
static float body_step(void) { return rnd((float)line_h(BODY_FONT) * 1.34f); }   /* the reading line's step */
static float title_step(void) { return (float)wrap_step(F_TITLE); }

/* The rows of the open article for a column of width w. */
static void make_rows(float w)
{
    int off[64], len[64], f = F_TITLE;
    float y = 0, step = body_step();
    nrows = 0;
    a_col = w;
    int k = wrap_lines(A.title, (int)strlen(A.title), w, measure, &f, off, len, 6);
    for (int i = 0; i < k && nrows < AROWS; i++, nrows++) {
        row[nrows].kind = R_TITLE, row[nrows].off = off[i], row[nrows].len = len[i], row[nrows].y = y;
        y += title_step();
    }
    y += (float)(line_h(F_TITLE) - wrap_step(F_TITLE)) + 18;
    row[nrows].kind = R_META, row[nrows].y = y, nrows++;
    y += 30 + 22;
    if (!strcmp(A.state, "summary") || !strcmp(A.state, "cut") || A.text_cut) {
        row[nrows].kind = R_STATE, row[nrows].y = y, nrows++;
        y += (float)line_h(F_BODY) + 18;
    }
    row[nrows].kind = R_RULE, row[nrows].y = y, nrows++;
    y += 26;
    f = BODY_FONT;
    static int poff[ART_TEXT / 8], plen[ART_TEXT / 8];
    for (int p = 0; p < A.n_para && nrows < AROWS - 1; p++) {
        const char *s = A.text + A.para_off[p];
        int m = wrap_lines(s, (int)strlen(s), w, measure, &f, poff, plen, (int)(sizeof poff / sizeof poff[0]));
        for (int i = 0; i < m && nrows < AROWS - 1; i++, nrows++) {
            row[nrows].kind = R_LINE, row[nrows].para = (short)p, row[nrows].off = A.para_off[p] + poff[i], row[nrows].len = plen[i];
            row[nrows].y = y;
            y += step;
        }
        y += rnd(step * 0.45f);                              /* between paragraphs */
    }
    row[nrows].kind = R_END, row[nrows].y = y + 10, nrows++;
    a_total = y + 10 + 60;
    a_step = step;
    wrapped_w = (int)w;
}

static const char *state_words(char *buf, int n)
{
    if (!strcmp(A.state, "summary"))
        snprintf(buf, (size_t)n, "Only the feed's summary: the page could not be read (%s).", A.reason[0] ? A.reason : "no reason given");
    else snprintf(buf, (size_t)n, "The article is cut at 12 KB: the QR code opens the whole text.");
    return buf;
}

/* The meta row: the pill, the category, the date and the age. */
static void meta(float x, float y, const char *source, const char *cat, const char *date, long age_s)
{
    char a[24], line[96];
    float k = stext_alpha(y, y + 30);
    float xe = pill(x, y, 30, source[0] ? source : "-", k);
    xe = tag(xe + 12, y, 30, cat, k);
    line[0] = 0;
    if (age_s >= 0) age_words(a, sizeof a, age_s);
    if (date && date[0]) snprintf(line, sizeof line, "%s%s%s%s", date, age_s >= 0 ? ", " : "", age_s >= 0 ? a : "", age_s >= 0 ? " ago" : "");
    else if (age_s >= 0) snprintf(line, sizeof line, "%s ago", a);
    if (line[0]) stext(xe + 16, y + (30 - (float)line_h(F_SMALL)) / 2, F_SMALL, vis(GREY, k), line, (int)strlen(line));
}

/* The QR code's column: the code on a white card, the words under it. */
static void side(const alay *L)
{
    float box = L->side_w - 60, x = L->side_x + 30, y = L->y + 40;
    if (art == ART_LOADING) {
        round_rect(x, y, box, box, 14, 0xffffff12);
        skel_bar(x + 20, y + box + 24, box - 40, 22);
        return;
    }
    if (art != ART_SHOWN) return;
    if (qr_ok) {
        float m = (float)(int)(box / (float)(qr_side + 8));
        if (m < 1) m = 1;
        float s = m * (float)(qr_side + 8), qx = rnd(L->side_x + (L->side_w - s) / 2);
        round_rect(qx, y, s, s, 14, WHITE);
        aa_corners(qx, y, s, s, 14, WHITE);
        batch_begin(BLACK);
        for (int k = 0; k < qr_nruns; k++) batch_rect(qx + (float)(qr_run[k].c + 4) * m, y + (float)(qr_run[k].r + 4) * m, (float)qr_run[k].len * m, m);
        batch_end();
        y += s + 22;
        text_wrap(L->side_x + 10, y, F_SMALL, GREY, L->side_w - 20, 3, "Scan it with a phone to open the whole article.");
    } else {
        text_wrap(L->side_x + 10, y, F_BODY, DIM, L->side_w - 20, 3,
                  A.link[0] ? "The link is too long for a QR code." : "The Worker sent no link for this article.");
    }
}

static void article_body(void)
{
    alay L;
    art_layout(&L);
    int dy = (int)rnd(SA_RISE * (1 - start_k(SA_LEFT)));
    shift_push(0, dy);
    glass_tile(L.x, L.y, L.w, L.h);
    side(&L);
    float view = L.bb - L.bt;
    a_view = view;
    if (art != ART_SHOWN) {                                  /* the loading and the error: the list's title, then the skeleton or one line */
        int off[8], len[8], f = F_TITLE;
        int k = wrap_lines(art_title, (int)strlen(art_title), L.col_w, measure, &f, off, len, 3);
        float y = L.bt;
        stext_band(L.bt, L.bb, 0, 0);
        for (int i = 0; i < k; i++, y += title_step()) stext(L.col_x, y, F_TITLE, WHITE, art_title + off[i], len[i]);
        y += (float)(line_h(F_TITLE) - wrap_step(F_TITLE)) + 18;
        meta(L.col_x, y, art_source, art_cat, NULL, -1);
        stext_band_off();
        y += 30 + 30;
        if (art == ART_LOADING) {
            float step = body_step();
            for (int i = 0; i < 7 && y + 26 < L.bb; i++, y += step) skel_bar(L.col_x, y + 6, L.col_w * (i == 6 ? 0.55f : i % 3 == 2 ? 0.86f : 0.97f), 26);
        } else {
            int k2 = text_wrap(L.col_x, y, F_BODY, YELLOW, L.col_w, 2, art_err);
            text(L.col_x, y + (float)(k2 > 0 ? k2 : 1) * (float)wrap_step(F_BODY) + 10, F_SMALL, GREY, "CIRCLE goes back to the list.");
        }
        shift_pop();
        return;
    }
    if (!wrapped_w || (int)L.col_w != wrapped_w) make_rows(L.col_w);
    as_target = scroll_clamp(as_target, view, a_total);
    if (as_pos > as_target && as_pos > (a_total > view ? a_total - view : 0)) as_pos = as_target;
    double s = as_pos, rest = a_total - view - s;
    stext_band(L.bt, L.bb, s > 0.5 ? (float)(s < FADE ? s : FADE) : 0, rest > 0.5 ? (float)(rest < FADE ? rest : FADE) : 0);
    char w[200];
    for (int i = 0; i < nrows; i++) {
        float y = L.bt + row[i].y - (float)s;
        if (y > L.bb + 4) break;
        if (y + 70 < L.bt) continue;
        switch (row[i].kind) {
        case R_TITLE: stext(L.col_x, y, F_TITLE, WHITE, A.title + row[i].off, row[i].len); break;
        case R_META: {
            char cat[20];
            snprintf(cat, sizeof cat, "%s", A.category);
            if (cat[0] >= 'a' && cat[0] <= 'z') cat[0] = (char)(cat[0] - 32);
            meta(L.col_x, y, A.source, cat, A.date, A.age);
            break;
        }
        case R_STATE: stext_fit(L.col_x, y, F_BODY, YELLOW, L.col_w, state_words(w, sizeof w)); break;
        case R_RULE: {
            float k = stext_alpha(y, y + 1);
            if (k > 0) rect(L.col_x, y, L.col_w, 1, alpha_times(0xffffff30, k));
            break;
        }
        case R_LINE: stext(L.col_x, y, BODY_FONT, BODY_COLOUR, A.text + row[i].off, row[i].len); break;
        case R_END: {
            float k = stext_alpha(y, y + 30);
            if (k > 0) aa_disc(L.col_x + 4, y + 14, 4, alpha_times(DIM, k));
            const char *e = !strcmp(A.state, "ok") && !A.text_cut ? "The end of the article." : "The end of what came: the QR code opens the rest.";
            stext(L.col_x + 20, y + 3, F_SMALL, DIM, e, (int)strlen(e));
            break;
        }
        }
    }
    stext_band_off();
    scroll_bar(L.side_x - 30, L.bt, view, s, view, a_total);
    shift_pop();
}

/* ---- page.h's News page ------------------------------------------------------------------------ */

void news_body(void)
{
    if (art != ART_NONE) article_body();
    else list_body();
}

void news_bar(float y)
{
    static const char hints_list[] = "UP/DOWN select  CROSS read  L1/R1 pages  SQUARE refresh  START exit";
    static const char hints_art[] = "UP/DOWN scroll  L2/R2 page  CIRCLE back  L1/R1 pages  START exit";
    char line[300], a[24], t[24];
    u32 c = GREY;
    const board_feed *f = list_have ? &list.feed : &board_cur.news_feed;
    long list_age = list_have ? (long)((sysGetSystemTime() - list_at) / 1000000) : board_age();
    if (art == ART_LOADING) {
        snprintf(line, sizeof line, "Fetching the article from %s ...", art_source[0] ? art_source : "the Worker");
    } else if (art == ART_SHOWN) {
        int words = 0;
        for (int i = 0; i < A.n_para; i++)
            for (const char *p = A.text + A.para_off[i]; *p; p++) words += (*p == ' ');
        words += A.n_para;
        snprintf(line, sizeof line, "%s: %d paragraph%s, about %d min to read.", A.source[0] ? A.source : "The article", A.n_para,
                 A.n_para == 1 ? "" : "s", words / 200 + 1);
    } else if (art == ART_ERROR) {
        c = board_status_words(line, sizeof line);           /* the error is the panel's line */
    } else if (sky_time() - note_t < 3.0) {
        snprintf(line, sizeof line, "%s", note);
        c = YELLOW;
    } else if (!board_have && !list_have) c = board_status_words(line, sizeof line);
    else if (!list_last_ok && list_fetches) {
        if (list_have) {
            clock_words(t, sizeof t, clock_utc() - list_age, 0);
            snprintf(line, sizeof line, "The news list did not update: %s. It is the list of %s.", list_words, t);
        } else snprintf(line, sizeof line, "The news list did not load: %s.%s", list_words, n_items() ? " These are the Board's headlines." : "");
        c = YELLOW;
    } else if (!strcmp(f->state, "stale")) {
        age_words(a, sizeof a, (f->age >= 0 ? f->age : 0) + list_age);
        snprintf(line, sizeof line, "%s. The news is %s old.", f->reason[0] ? f->reason : "The feeds failed", a);
        c = YELLOW;
    } else if (n_items() && f->reason[0]) {
        snprintf(line, sizeof line, "%s", f->reason);
        c = YELLOW;
    } else if (n_items()) {
        clock_words(t, sizeof t, clock_utc() - list_age, 0);
        snprintf(line, sizeof line, "Headline %d of %d, %s %s.", sel + 1, n_items(), list_have ? "the list of" : "the Board's headlines of", t);
    } else c = board_status_words(line, sizeof line);
    bar_draw(y, line, c, board_url_note, art != ART_NONE ? hints_art : hints_list);
}
