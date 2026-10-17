/* The Futbol page (futbol.h, 0.5.0): page.h's futbol_body and futbol_bar,
 * the pad, the schedule and the replies. One glass tile: the two days'
 * tabs and the day's counts on top, then the list, a row per league and a
 * row per game, which scrolls by fractions of a pixel (eased, parse.h's
 * scroll_follow) and draws through stext.h, cut and faded at the edges of
 * its band, as the reader's list. The main thread only. */
#include <stdio.h>
#include <stddef.h>
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
#include "futbol.h"

#define PERIOD_US 600000000LL        /* the list every 10 min while the page shows */
#define LIVE_US 60000000LL           /* every 60 s while a game of the day is live or due */
#define RETRY_US 60000000LL          /* 60 s after a failure */
#define OPEN_US 60000000LL           /* the page's opening asks unless the day came in the last 60 s */
#define GAP_US 10000000LL            /* SQUARE: at most once in 10 s */
#define TAU 0.075                    /* the scroll's ease, as the reader's */
#define FADE 36.0f                   /* the band's fade at an edge where the list goes on */
#define LOG_FUT 40                   /* futbol lines in couch.txt per start */
#define PI_F 3.14159265f

static float rnd(float x) { return floorf(x + 0.5f); }

static futbol_data D[2];             /* the last good reply of each day: 0 today, 1 tomorrow */
static int have[2], last_ok[2] = {1, 1}, fetches[2], pending = -1;   /* pending: the day asked for, -1 none */
static s64 got_at[2], asked_at[2];   /* the reply's system time; the last request's (0: none) */
static long ask_date[2], got_date[2];   /* the local date asked for and the reply's, days since 1970-01-01 */
static char words[2][160];           /* the last fetch's result in words, per day */
static int day;                      /* the day on the TV: 0 today, 1 tomorrow */
static int was_fut, logged;
static double pos, target, t_last = -1;
static float view = 1, total = 1, row_step = 50;   /* the band, the list and a game row, from the last draw */
static pad_rep rep;

/* The console's local date, days since 1970-01-01 (the clock's offset). */
static long local_date(void)
{
    int off;
    clock_offset(&off);
    long t = clock_utc() + off;
    return t >= 0 ? t / 86400 : (t - 86399) / 86400;
}

/* A game of today not started that starts in the next 10 min or started
 * up to 2 h ago by its time: its kind turns live soon, so the list comes
 * every 60 s, as for a live game. */
static int due(const futbol_data *d)
{
    int off;
    clock_offset(&off);
    long t = clock_utc() + off, m = ((t % 86400) + 86400) % 86400 / 60;
    for (int i = 0; i < d->n_game; i++) {
        int s = d->game[i].kind == FK_PRE ? hm_parse(d->game[i].when) : -1;
        if (s >= 0 && m >= s - 10 && m <= s + 120) return 1;
    }
    return 0;
}

/* ---- the schedule and the pad -------------------------------------------------------------- */

static void ask(int k, s64 now)
{
    asked_at[k] = now ? now : 1;
    ask_date[k] = local_date() + k;
    pending = k;
    board_ask_futbol(k);
}

void futbol_tick(void)
{
    double t = sky_time(), dt = t_last < 0 ? 0 : t - t_last;
    t_last = t;
    if (dt > 0.1) dt = 0.1;                  /* a long frame (a shot): no jump */
    pos = scroll_follow(pos, target, dt, TAU);
    long today = local_date();
    for (int k = 0; k < 2; k++)
        if ((asked_at[k] && ask_date[k] != today + k) || (have[k] && got_date[k] != today + k)) {   /* midnight: the day is another one, asked again */
            have[k] = 0;
            asked_at[k] = 0;
            last_ok[k] = 1;
            words[k][0] = 0;
            if (k == day) pos = target = 0;
        }
    int opened = page == PAGE_FUTBOL && !was_fut;
    was_fut = page == PAGE_FUTBOL;
    if (page != PAGE_FUTBOL || ambient || pending >= 0 || net_state != NET_UP || !board_running()) return;
    s64 now = sysGetSystemTime(), since = now - asked_at[day];
    s64 period = !last_ok[day] ? RETRY_US : have[day] && (futbol_live(&D[day]) || (day == 0 && due(&D[day]))) ? LIVE_US : PERIOD_US;
    if (!asked_at[day] || since >= period || (opened && since >= OPEN_US)) ask(day, now);
}

void futbol_refresh(void)
{
    s64 now = sysGetSystemTime();
    if (pending < 0 && net_state == NET_UP && board_running() && (!asked_at[day] || now - asked_at[day] >= GAP_US)) ask(day, now);
}

int futbol_input(void)
{
    if (pressed & (BTN_LEFT | BTN_RIGHT)) {              /* today or tomorrow, from the top */
        int to = pressed & BTN_RIGHT ? 1 : 0;
        if (to != day) {
            day = to;
            pos = target = 0;
        }
        return 1;
    }
    unsigned d = pad_repeat(&rep, BTN_UP | BTN_DOWN);    /* a row a press, held: again every 30 ms */
    if (d) {
        target = scroll_clamp(target + (d & BTN_DOWN ? 1 : -1) * row_step, view, total);
        return 1;
    }
    if (pressed & (BTN_L2 | BTN_R2)) {                   /* a page */
        target = scroll_clamp(target + (pressed & BTN_R2 ? 1 : -1) * (view - row_step), view, total);
        return 1;
    }
    return 0;
}

/* ---- the replies ------------------------------------------------------------------------------- */

/* A reply is filed by the date it was asked for (0.5.0's review): one that
 * comes after midnight is not today's. Tomorrow's list asked before
 * midnight is today's (j 0); today's is a day gone by, dropped (the tick
 * asks again). */
void futbol_take(const board_reply *b)
{
    int k = b->day ? 1 : 0, j = (int)(ask_date[k] - local_date());   /* the asked day now: 0 today, 1 tomorrow, else gone */
    char what[200] = "", cn[24] = "no connection";
    if (pending == k) pending = -1;
    fetches[k]++;
    if (j == 0 || j == 1) {
        last_ok[j] = b->prc == BP_OK;
        snprintf(words[j], sizeof words[j], "%s", b->words);
        if (j != k) {                                    /* the schedule of the day it is now */
            asked_at[j] = b->at - b->us > 0 ? b->at - b->us : 1;
            ask_date[j] = ask_date[k];
        }
    } else snprintf(what, sizeof what, "a list of a day gone by (asked before midnight): dropped");
    if ((j == 0 || j == 1) && last_ok[j]) {
        int first = !have[j], ng = b->fut->n_game < 0 ? 0 : b->fut->n_game > FUT_GAMES ? FUT_GAMES : b->fut->n_game;
        /* up to its last game (game[] is last): a typical day fills a third of the 91 KB, every 60 s while a game is live */
        memcpy(&D[j], b->fut, offsetof(futbol_data, game) + (size_t)ng * sizeof(fut_game));
        have[j] = 1;
        got_at[j] = b->at;
        got_date[j] = ask_date[k];
        if (first && j == day) pos = target = 0;
        const futbol_data *d = &D[j];
        snprintf(what, sizeof what, "%d leagues, %d games, %d live%s, state %s%s%s%s", d->n_league, d->n_game, futbol_live(d),
                 d->more_games ? " (more left out)" : "", d->feed.state[0] ? d->feed.state : "-", d->feed.reason[0] ? ", " : "", d->feed.reason,
                 j != k ? "; tomorrow's list, today's since midnight" : "");
    }
    if (logged >= LOG_FUT) return;
    logged++;
    if (b->r->connect_us >= 0) snprintf(cn, sizeof cn, "connect %.2f s", (double)b->r->connect_us / 1e6);
    couch_log("futbol %s %d at %.1f s: %.2f s (%s), HTTP %d, %d bytes: %s%s%s", k ? "tomorrow" : "today", fetches[k], (double)(b->at - t_start) / 1e6,
              (double)b->us / 1e6, cn, b->r->http, b->r->len, b->words, what[0] ? "; " : "", what);
}

/* ---- the page ---------------------------------------------------------------------------------- */

typedef struct {
    float x, y, w, h;                /* the tile */
    float bt, bb;                    /* the list's band */
    float xl, xr;                    /* the rows' inner edges */
    float lh, gh, gap;               /* a league row, a game row, the room before a league */
    float when_w, tv_w;              /* the time's column, the TV's column */
} flay;

static void layout(flay *L)
{
    int wide = page_wide();
    L->x = MG;
    L->y = page_top();
    L->w = SW - 2 * MG;
    L->h = page_bar_y() - 12 - L->y;
    L->bt = L->y + (wide ? 92 : 80);
    L->bb = L->y + L->h - 14;
    L->xl = L->x + 36;
    L->xr = L->x + L->w - 36;
    L->lh = wide ? 58 : 50;
    L->gh = wide ? 52 : 44;
    L->gap = wide ? 14 : 10;
    L->when_w = wide ? 180 : 176;
    L->tv_w = wide ? 340 : 260;
}

/* The list's rows: each league with games (its row, then its games, the
 * gap before every league but the first), then the "more" row; the content's height. */
static float content_h(const flay *L, const futbol_data *d)
{
    float y = 0;
    int first = 1;
    for (int i = 0; i < d->n_league; i++) {
        if (!d->league[i].n) continue;
        y += (first ? 0 : L->gap) + L->lh + (float)d->league[i].n * L->gh;
        first = 0;
    }
    if (d->more_leagues || d->more_games) y += L->gap + L->gh;
    return y;
}

/* A text cut to maxw, right-aligned at xr: its left x. */
static float fit_r(float xr, float y, int font, u32 c, float maxw, const char *s)
{
    float w = stext_w(font, s, (int)strlen(s));
    if (w > maxw) w = maxw;
    stext_fit(rnd(xr - w), y, font, c, maxw, s);
    return xr - w;
}

static void league_row(const flay *L, float y, const fut_league *g, const futbol_data *d)
{
    char cnt[32];
    int live = 0;
    for (int j = 0; j < g->n; j++) live += d->game[g->first + j].kind == FK_LIVE;
    float ty = y + L->lh - 10 - (float)line_h(F_BODY), k = stext_alpha(ty, ty + (float)line_h(F_BODY));
    if (k > 0) round_rect(L->xl, ty + 4, 5, (float)line_h(F_BODY) - 8, 2, alpha_times(source_colour(g->name), k));
    float xe = stext_fit(L->xl + 20, ty, F_BODY, WHITE, L->xr - L->xl - 360, g->name);
    if (g->country[0]) stext_fit(xe + 14, ty + (float)base_dy(F_BODY, F_SMALL), F_SMALL, GREY, L->xr - 160 - xe - 14, g->country);
    if (live) snprintf(cnt, sizeof cnt, "%d live of %d", live, g->n);
    else snprintf(cnt, sizeof cnt, "%d game%s", g->n, g->n == 1 ? "" : "s");
    stext_r(L->xr, ty + (float)base_dy(F_BODY, F_SMALL), F_SMALL, live ? GREEN : DIM, cnt);
    float ry = y + L->lh - 1;
    if (stext_alpha(ry, ry + 1) > 0) rect(L->xl, ry, L->xr - L->xl, 1, alpha_times(0xffffff24, stext_alpha(ry, ry + 1)));
}

static void game_row(const flay *L, float y, const fut_game *g, double t)
{
    char sc[24];
    float fh = (float)line_h(F_BODY), ty = rnd(y + (L->gh - fh) / 2), k = stext_alpha(ty, ty + fh);
    float x0 = L->xl + 20, mid0 = L->xl + L->when_w, mid1 = L->xr - L->tv_w, cx = rnd((mid0 + mid1) / 2), half = page_wide() ? 54 : 46;
    /* the time or the minute: a live game's minute in GREEN after a dot that breathes */
    if (g->kind == FK_LIVE) {
        float a = fx_reduced() ? 1 : 0.55f + 0.45f * sinf((float)t * 2 * PI_F / 1.6f);
        if (k > 0) aa_disc(x0 + 6, ty + fh / 2, 6, alpha_times(GREEN, k * a));
        stext_fit(x0 + 22, ty, F_BODY, GREEN, L->when_w - 46, g->when);
    } else
        stext_fit(x0, ty, F_BODY, g->kind == FK_PRE ? WHITE : g->kind == FK_END ? GREY : YELLOW, L->when_w - 24, g->when);
    /* the score between the names; before the game a dim "vs" */
    int scored = g->sh >= 0 && g->sa >= 0;
    if (scored) snprintf(sc, sizeof sc, "%d - %d", g->sh, g->sa);
    else snprintf(sc, sizeof sc, "%s", g->kind == FK_PRE ? "vs" : "-");
    int sf = scored ? F_MED : F_SMALL;
    float sw = stext_w(sf, sc, (int)strlen(sc)), sy = rnd(y + (L->gh - (float)line_h(sf)) / 2);
    if (scored && k > 0) {
        u32 pc = g->kind == FK_LIVE ? (GREEN & ~0xffu) | 0x30 : 0xffffff16;
        round_rect(cx - half + 4, y + 7, 2 * half - 8, L->gh - 14, (L->gh - 14) / 2, alpha_times(pc, k));
    }
    stext(rnd(cx - sw / 2), sy, sf, scored ? WHITE : DIM, sc, (int)strlen(sc));
    /* the names: the loser of a finished game in GREY */
    u32 hc = g->kind == FK_END && scored && g->sh < g->sa ? GREY : WHITE, ac = g->kind == FK_END && scored && g->sa < g->sh ? GREY : WHITE;
    if (g->kind == FK_OFF) hc = ac = GREY;
    fit_r(cx - half - 14, ty, F_BODY, hc, cx - half - 14 - mid0 - 10, g->home);
    float ae = stext_fit(cx + half + 14, ty, F_BODY, ac, mid1 - 10 - (cx + half + 14), g->away);
    if (g->tv[0]) fit_r(L->xr, ty + (float)base_dy(F_BODY, F_SMALL), F_SMALL, DIM, L->xr - ae - 30, g->tv);   /* the TV: the room the away name leaves */
}

/* The tabs, the day's words and its counts, on the tile's top row. */
static void head_row(const flay *L)
{
    static const char *const tab[2] = {"Today", "Tomorrow"};
    float x = L->xl, y = L->y + 22, ph = 40;
    for (int i = 0; i < 2; i++) {
        float w = (float)text_w(F_BODY, tab[i]) + 40;
        if (i == day) {
            round_rect(x, y, w, ph, ph / 2, BLUE);
            aa_corners(x, y, w, ph, ph / 2, BLUE);
        } else round_frame(x, y, w, ph, ph / 2, 1, 0xffffff50);
        text(rnd(x + 20), rnd(y + (ph - (float)line_h(F_BODY)) / 2), F_BODY, i == day ? 0x0b1426ff : GREY, "%s", tab[i]);
        x += w + 12;
    }
    if (!have[day]) return;
    const futbol_data *d = &D[day];
    const char *dw = strstr(d->day, ", ");                   /* "Today, Fri 16 Oct": the date after the tab's word */
    text_fit(x + 16, rnd(y + (ph - (float)line_h(F_MED)) / 2), F_MED, WHITE, L->xr - x - 420, "%s", dw ? dw + 2 : d->day);
    int live = futbol_live(d);
    char c[64];
    if (!d->n_game) return;                                  /* an empty day: the tile's words say it */
    snprintf(c, sizeof c, "%d game%s, %d league%s", d->n_game, d->n_game == 1 ? "" : "s", d->n_league, d->n_league == 1 ? "" : "s");
    float xe = text_r(L->xr, rnd(y + (ph - (float)line_h(F_BODY)) / 2), F_BODY, GREY, "%s", c);
    if (live) {
        float lx = text_r(xe - 24, rnd(y + (ph - (float)line_h(F_BODY)) / 2), F_BODY, GREEN, "%d live", live);
        aa_disc(lx - 16, y + ph / 2, 6, GREEN);
    }
}

/* One line in the tile's middle: an empty day, an error, the setup. */
static void tile_words(const flay *L, const char *line, u32 c)
{
    if (!icons_rc) icon(IC_S_INFO, L->xl + 14, L->bt + 40, DIM);
    text_wrap(L->xl + (icons_rc ? 0 : 40), L->bt + 24, F_MED, c, L->xr - L->xl - 40, 3, line);
}

void futbol_body(void)
{
    flay L;
    layout(&L);
    int dy = (int)rnd(SA_RISE * (1 - start_k(SA_LEFT)));
    shift_push(0, dy);
    glass_tile(L.x, L.y, L.w, L.h);
    head_row(&L);
    rect(L.xl, L.bt - 10, L.xr - L.xl, 1, 0xffffff30);
    const futbol_data *d = &D[day];
    char line[300];
    if (!board_url[0]) {
        tile_words(&L, "No Board URL yet: the Board page has the steps.", YELLOW);
    } else if (!have[day] && !last_ok[day]) {                /* no list of this day: why */
        if (strstr(words[day], "HTTP 404")) snprintf(line, sizeof line, "The Board's Worker has no futbol list (HTTP 404): this page needs a Worker with /futbol.");
        else snprintf(line, sizeof line, "The futbol list did not load: %s. SQUARE tries again.", words[day]);
        tile_words(&L, line, YELLOW);
    } else if (!have[day]) {                                 /* before the first reply: the skeleton */
        float y = L.bt + 10;
        for (int i = 0; i < 9 && y + L.gh < L.bb; i++, y += i % 4 == 0 ? L.lh : L.gh) {
            if (i % 4 == 0) skel_bar(L.xl + 20, y + 14, 380, 26);
            else {
                skel_bar(L.xl + 20, y + 12, 90, 24);
                skel_bar(L.xl + L.when_w + 120, y + 12, (L.xr - L.xl) * 0.42f, 24);
            }
        }
    } else if (!d->n_game) {
        if (!strcmp(d->feed.state, "none") || !strcmp(d->feed.state, "stale"))
            snprintf(line, sizeof line, "No futbol data: %s.", d->feed.reason[0] ? d->feed.reason : "the reply has none");
        else snprintf(line, sizeof line, "No games %s.", day ? "tomorrow" : "today");
        tile_words(&L, line, !strcmp(d->feed.state, "ok") || !d->feed.state[0] ? WHITE : YELLOW);
    } else {
        double t = sky_time();
        view = L.bb - L.bt;
        total = content_h(&L, d);
        row_step = L.gh;
        target = scroll_clamp(target, view, total);
        if (pos > target && pos > (total > view ? total - view : 0)) pos = target;
        double s = pos, rest = total - view - s;
        stext_band(L.bt, L.bb, s > 0.5 ? (float)(s < FADE ? s : FADE) : 0, rest > 0.5 ? (float)(rest < FADE ? rest : FADE) : 0);
        float y = L.bt - (float)s;
        int first = 1;
        for (int i = 0; i < d->n_league && y < L.bb + 4; i++) {
            const fut_league *g = &d->league[i];
            if (!g->n) continue;
            if (!first) y += L.gap;
            first = 0;
            if (y + L.lh > L.bt - 4) league_row(&L, y, g, d);
            y += L.lh;
            for (int j = 0; j < g->n && y < L.bb + 4; j++, y += L.gh)
                if (y + L.gh > L.bt - 4) game_row(&L, y, &d->game[g->first + j], t);
        }
        if ((d->more_leagues || d->more_games) && y < L.bb + 4) {
            y += L.gap;
            snprintf(line, sizeof line, "%d more league%s (%d games) did not fit the list.", d->more_leagues, d->more_leagues == 1 ? "" : "s", d->more_games);
            stext_fit(L.xl + 20, rnd(y + (L.gh - (float)line_h(F_SMALL)) / 2), F_SMALL, DIM, L.xr - L.xl - 40, line);
        }
        stext_band_off();
        if (total > view + 1) {                              /* the scroll bar beside the band */
            float th = view * view / total, ty;
            if (th < 40) th = 40;
            ty = L.bt + (float)((view - th) * s / (total - view));
            round_rect(L.x + L.w - 16, L.bt, 6, view, 3, 0xffffff18);
            round_rect(L.x + L.w - 16, ty, 6, th, 3, 0xffffff90);
        }
    }
    shift_pop();
}

void futbol_bar(float y)
{
    static const char hints[] = "UP/DOWN scroll  LEFT/RIGHT day  L1/R1 pages  SQUARE refresh  START exit";
    char line[300], t[24];
    u32 c = GREY;
    const futbol_data *d = &D[day];
    long age = have[day] ? (long)((sysGetSystemTime() - got_at[day]) / 1000000) : 0;
    if (!board_url[0] || net_state != NET_UP || !board_running()) c = board_status_words(line, sizeof line);
    else if (pending == day && !have[day]) snprintf(line, sizeof line, "Fetching the futbol list of %s ...", day ? "tomorrow" : "today");
    else if (!last_ok[day] && have[day]) {
        clock_words(t, sizeof t, clock_utc() - age, 0);
        snprintf(line, sizeof line, "The futbol list did not update: %s. It is the list of %s.", words[day], t);
        c = YELLOW;
    } else if (!have[day]) c = board_status_words(line, sizeof line);
    else if (!strcmp(d->feed.state, "stale")) {
        char a[24];
        age_words(a, sizeof a, (d->feed.age >= 0 ? d->feed.age : 0) + age);
        snprintf(line, sizeof line, "%s. The list is %s old.", d->feed.reason[0] ? d->feed.reason : "The source failed", a);
        c = YELLOW;
    } else {
        clock_words(t, sizeof t, clock_utc() - age, 0);
        int live = futbol_live(d);
        if (live) snprintf(line, sizeof line, "%d live, the list of %s (each minute).", live, t);
        else snprintf(line, sizeof line, "The list of %s.", t);
    }
    bar_draw(y, line, c, board_url_note, hints);
}
