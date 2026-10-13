/* Couch's pages (page.h): the header every page shares, the ambient mode
 * and the News page. Only the main thread runs here. */
#include <stdio.h>
#include <string.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include "couch.h"
#include "page.h"
#include "board.h"
#include "clock.h"
#include "http.h"
#include "parse.h"

#define DIM_LAYER 0x000000a8         /* black at 66 %: the picture at a third of its light */
#define DOT_R 7
#define DOT_STEP 24

int page = PAGE_BOARD;
const char *const page_name[PAGE_N] = {"Board", "News", "Info"};
int ambient;

static s64 idle_since, amb_since;
static int amb_saved, amb_dx, amb_dy, shifted;
static long amb_step = -1;
static int keep_l, keep_t, keep_r, keep_b;

/* ---- the header ------------------------------------------------------------------ */

/* The internet's dot colour and words, from the Board thread's last fetches. */
static u32 net_words(char *buf, int n)
{
    char t[24];
    if (net_state != NET_UP) {
        snprintf(buf, (size_t)n, "No network");
        return RED;
    }
    if (board_fetches && !net_has_ip()) {
        snprintf(buf, (size_t)n, "No network: no IP address");
        return RED;
    }
    switch (board_net_q) {
    case NQ_OK: snprintf(buf, (size_t)n, "Internet ok"); return GREEN;
    case NQ_SLOW: snprintf(buf, (size_t)n, "Internet slow (%.1f s)", (double)board_net_us / 1e6); return YELLOW;
    case NQ_DOWN:
        clock_words(t, sizeof t, board_net_down_utc, 0);
        snprintf(buf, (size_t)n, "No internet since %s", t);
        return RED;
    default: snprintf(buf, (size_t)n, "Internet: checking"); return DIM;
    }
}

/* The right column: the page's name and its dots on the clock's row, the city
 * and the internet on the row under it. Its width, so the clock's row knows
 * where to stop. */
static float header_right(float y)
{
    char net[64];
    float r = SW - MG, dots = PAGE_N * DOT_STEP - (DOT_STEP - 2 * DOT_R);
    float y1 = y + (float)base_dy(F_LARGE, F_MED), y2 = y + (float)line_h(F_LARGE) + 2;
    for (int i = 0; i < PAGE_N; i++) {
        float cx = r - dots + DOT_R + i * DOT_STEP, cy = y1 + (float)line_h(F_MED) / 2;
        if (i == page) disc(cx, cy, DOT_R, BLUE);
        else ring(cx, cy, DOT_R - 1, 2, DIM);
    }
    float w1 = (float)text_w(F_MED, page_name[page]) + 18 + dots;
    text_r(r - dots - 18, y1, F_MED, WHITE, "%s", page_name[page]);
    u32 c = net_words(net, sizeof net);
    text_r(r, y2, F_SMALL, GREY, "%s", net);
    float nw = (float)text_w(F_SMALL, net);
    disc(r - nw - 16, y2 + (float)line_h(F_SMALL) / 2, 6, c);
    float w2 = nw + 28;
    if (board_have && board_cur.city[0]) {   /* at most 560 px: a long CITY ends in "..." and leaves the clock its room */
        float cw = (float)text_w(F_SMALL, board_cur.city);
        if (cw > 560) cw = 560;
        text_fit(r - w2 - 20 - cw, y2, F_SMALL, WHITE, cw, "%s", board_cur.city);
        w2 += 20 + cw;
    }
    return w1 > w2 ? w1 : w2;
}

static void header(void)
{
    const float y = 20;
    float rw = header_right(y);
    clock_draw(MG, y, SW - 2 * MG - rw - 40);
    rect(MG, CONTENT_Y - 16, SW - 2 * MG, 2, LINE);
}

float page_begin(void)
{
    if (ambient && (amb_dx || amb_dy)) {     /* the whole picture moves: the viewport, as the visible area does */
        keep_l = safe_l, keep_t = safe_t, keep_r = safe_r, keep_b = safe_b;
        safe_l += amb_dx, safe_r += amb_dx, safe_t += amb_dy, safe_b += amb_dy;
        safe_apply();
        shifted = 1;
    }
    begin_frame();
    header();
    return CONTENT_Y;
}

void page_end(void)
{
    if (ambient) {
        float ox, oy;
        screen_origin(&ox, &oy);
        rect(ox - AMBIENT_PX, oy - AMBIENT_PX, 1920 + 2 * AMBIENT_PX, 1080 + 2 * AMBIENT_PX, DIM_LAYER);
    }
    ui_flip();
    if (shifted) {
        safe_l = keep_l, safe_t = keep_t, safe_r = keep_r, safe_b = keep_b;
        safe_apply();
        shifted = 0;
    }
}

/* ---- the ambient mode ------------------------------------------------------------ */

int ambient_input(void)
{
    s64 now = sysGetSystemTime();
    if (!idle_since) idle_since = now;
    if (!pressed) return 0;
    idle_since = now;
    if (!ambient) return 0;
    ambient = 0;
    amb_step = -1;
    amb_dx = amb_dy = 0;
    page = amb_saved;
    couch_log("ambient: off after %.0f s, a button", (double)(now - amb_since) / 1e6);
    pressed = 0;                             /* the press woke the screen: no action, no exit */
    armed = 0;
    return 1;
}

void ambient_tick(void)
{
    s64 now = sysGetSystemTime();
    if (!idle_since) idle_since = now;
    if (!ambient && now - idle_since >= AMBIENT_SEC * 1000000LL) {
        ambient = 1;
        amb_since = now;
        amb_saved = page;
        amb_step = -1;
        couch_log("ambient: on after %d min with no button", AMBIENT_SEC / 60);
    }
    if (!ambient) return;
    long step = (long)((now - amb_since) / (AMBIENT_STEP_SEC * 1000000LL));
    if (step == amb_step) return;
    amb_step = step;
    ambient_shift(step, &amb_dx, &amb_dy);
    page = ambient_page(step, board_have && board_cur.n_news > 0) ? PAGE_NEWS : PAGE_BOARD;
}

/* ---- the News page --------------------------------------------------------------- */

/* "Infobae and Clarin": the sources of the headlines, each once, in order. */
static void news_sources(char *buf, int n)
{
    const board_data *b = &board_cur;
    int k = 0, cnt = 0;
    buf[0] = 0;
    for (int i = 0; i < b->n_news; i++) {
        int seen = 0;
        for (int j = 0; j < i; j++) seen |= !strcmp(b->news[j].source, b->news[i].source);
        if (seen) continue;
        cnt++;
        k += snprintf(buf + k, (size_t)(n - k > 0 ? n - k : 0), "%s%s", k ? ", " : "", b->news[i].source);
        if (k >= n) break;
    }
    if (!cnt) snprintf(buf, (size_t)n, "News");
}

void news_draw(void)
{
    const board_data *b = &board_cur;
    char src[96], a[24];
    float y = page_begin(), x = MG, w = SW - 2 * MG, h = BOTTOM - y;
    float ix = x + 32, iw = w - 64, fy = y + h - 22 - (float)line_h(F_SMALL);
    card(x, y, w, h);
    round_rect(x + 16, y + 26, 5, 30, 2, C_SQUARE);
    text(ix, y + 20, F_MED, WHITE, "News");
    news_sources(src, sizeof src);
    if (!board_have || !b->n_news) {
        char line[200];
        if (!board_have) snprintf(line, sizeof line, "The news shows after the first reply.");
        else if (!b->news_feed.state[0]) snprintf(line, sizeof line, "The Board's Worker sends no news (no NEWS_FEEDS).");
        else snprintf(line, sizeof line, "No news data: %s.", b->news_feed.reason[0] ? b->news_feed.reason : "the reply has none");
        text_wrap(ix, y + 90, F_BODY, board_have ? YELLOW : DIM, iw, 4, line);
        if (board_have && b->news_feed.state[0]) board_foot(ix, fy, iw, src, &b->news_feed);
    } else {
        /* a row: the source and the age on the left (the age right-aligned in
         * its column), the title in up to two lines; a row that does not fit
         * whole is left off (8 one-line rows fit at 1786 wide) */
        const float col = 270, tx = ix + col, tw = iw - col;
        const float dy = (float)base_dy(F_BODY, F_SMALL);
        float ry = y + 84;
        for (int i = 0; i < b->n_news; i++) {
            const board_news *nw = &b->news[i];
            int lines = text_wrap_lines(F_BODY, tw, 2, nw->title);
            float rh = (float)(lines * wrap_step(F_BODY));
            if (ry + rh > fy - 20) break;
            if (i) rect(ix, ry - 11, iw, 1, LINE);
            float sw = nw->age >= 0 ? 0 : col - 24;
            if (nw->age >= 0) {
                age_words(a, sizeof a, nw->age + board_age());
                sw = col - 34 - (float)text_w(F_SMALL, a) - 16;
                text_r(ix + col - 34, ry + dy, F_SMALL, GREY, "%s", a);
            }
            text_fit(ix, ry + dy, F_SMALL, BLUE, sw, "%s", nw->source);
            text_wrap(tx, ry, F_BODY, WHITE, tw, 2, nw->title);
            ry += rh + 22;
        }
        board_foot(ix, fy, iw, src, &b->news_feed);
    }
    board_status();
    footer("SQUARE refresh  L1/R1 pages  SELECT info  START exit");
    board_note();
    page_end();
}
