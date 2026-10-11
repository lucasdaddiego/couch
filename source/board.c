/* The Board (board.h). The thread makes only the calls of PS3 Health's
 * download, which ran on this console in its job thread: DNS, a blocking
 * connect with SO_RCVTIMEO and SO_SNDTIMEO, one send, recv until the close
 * or the end line. It reads want and board_on every 50 ms and between the
 * steps of a fetch. The exit (board_stop) shuts its socket down and joins
 * it with no time limit, so no network call is left when net_end runs. */
#include <stdio.h>
#include <string.h>
#include <sys/thread.h>
#include <sys/systime.h>
#include <lv2/systime.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fs.h>
#include <ps3gfx/journal.h>
#include "couch.h"
#include "board.h"
#include "clock.h"
#include "http.h"

#define PERIOD_US 300000000LL        /* a fetch every 5 min */
#define RETRY_US 60000000LL          /* 60 s after a failure */
#define GAP_US 10000000LL            /* SQUARE: at most once in 10 s */
#define LOG_FETCHES 50               /* fetch lines in couch.txt per start */

char board_url[200];
int board_url_file;
char board_url_note[160];
static http_url url;                 /* set before the thread starts, only read after */

/* The handoff: the thread writes the slot only while ready is 0 and sets
 * ready after a fence; the main thread parses it and clears ready after a
 * fence (Cell Bench's run.c). */
static struct {
    char body[BOARD_MAX + 1];
    http_result r;
    s64 at, us;                      /* the reply's system time, the fetch's duration */
} slot;
static volatile int ready, want, board_on, board_left, busy;
static volatile s64 next_us, last_us;   /* the thread's schedule: the next fetch, the last one's start */
static sys_ppu_thread_t tid;
static int started;

board_data board_cur;
int board_have;
int board_fetches, board_fails, board_last_ok;
char board_last_words[200];
long board_last_utc;
s64 board_last_us;
int board_last_http, board_last_len;
int board_has_skew;
long board_skew;
static board_data tmp;               /* the parse of the last reply, good or not */
static s64 cur_at;                   /* the system time of the reply in board_cur */
static int logged;
/* The words of the stale status line: a redirect without its Location, so
 * that the line keeps the data's time at 1600 wide (the Info screen and
 * couch.txt keep the Location). */
static char stale_words[200];

void board_init(void)
{
    char line[200];
    int n = fs_read_line(APP_DIR "/board_url.txt", line, sizeof line);
    board_url_file = 0;
    board_url_note[0] = 0;
    if (n > 0) {
        int rc = url_split(line, url.host, sizeof url.host, &url.port, url.path, sizeof url.path);
        if (rc == 0) {
            board_url_file = 1;
            snprintf(board_url, sizeof board_url, "%s", line);
            return;
        }
        snprintf(board_url_note, sizeof board_url_note, "%s",
                 rc == -2 ? "board_url.txt: Couch has no TLS, so only http:// works. The default is in use."
                          : "board_url.txt holds no http:// URL. The default is in use.");
    }
    url_split(BOARD_DEFAULT_URL, url.host, sizeof url.host, &url.port, url.path, sizeof url.path);
    snprintf(board_url, sizeof board_url, "%s", BOARD_DEFAULT_URL);
}

/* ---- the thread -------------------------------------------------------------- */

static void board_thread(void *unused)
{
    (void)unused;
    s64 next = 0;                            /* the first fetch at once */
    while (board_on) {
        s64 now = sysGetSystemTime();
        if (!ready && (now >= next || (want && now - last_us >= GAP_US))) {
            want = 0;
            last_us = now;
            busy = 1;
            http_get(&url, slot.body, BOARD_MAX, &slot.r, &board_on);
            slot.at = sysGetSystemTime();
            slot.us = slot.at - now;
            busy = 0;
            if (!board_on) break;
            /* a whole 200 reply waits 5 min, also one of another version: a retry would not change it */
            int whole = slot.r.step == H_OK && board_has_end(slot.body, slot.r.len);
            next = slot.at + (whole ? PERIOD_US : RETRY_US);
            next_us = next;
            __sync_synchronize();
            ready = 1;
        }
        sysUsleep(50000);
    }
    board_left = 1;
    sysThreadExit(0);
}

int board_start(void)
{
    if (started) return 0;
    board_on = 1;
    board_left = 0;
    s32 rc = sysThreadCreate(&tid, board_thread, NULL, 1500, 0x20000, THREAD_JOINABLE, "board");
    if (rc) {
        board_on = 0;
        couch_log("board thread: sysThreadCreate rc 0x%08x", (unsigned)rc);
        return rc;
    }
    started = 1;
    return 0;
}

int board_running(void) { return started; }

/* No time limit on the join: one that gave up would leave the thread in a
 * network call while net_end and the process exit end the network under it.
 * The wait, by where the thread is: in a recv or a send, the shutdown ends
 * the call at once (SO_RCVTIMEO and SO_SNDTIMEO end it within 10 s in any
 * case); in its 50 ms sleep, the sleep; in a DNS lookup or a connect, which
 * a shutdown does not end, the resolver's or the TCP stack's own timeout,
 * not measured on this console. http_get reads board_on after each step and
 * makes no further call. */
int board_stop(void)
{
    if (!started) return 0;
    int in_fetch = busy;
    s64 t0 = sysGetSystemTime();
    couch_log("board thread: the exit stops it%s", in_fetch ? ", it is in a fetch" : "");   /* the mark of a hang in the join */
    board_on = 0;
    http_wake();                             /* the fence, then netShutdown on the open socket */
    u64 rv = 0;
    s32 rc = sysThreadJoin(tid, &rv);
    double sec = (double)(sysGetSystemTime() - t0) / 1e6;
    if (rc == 0 || board_left) started = 0;  /* a thread that may still run stays started: a later call joins again, no net_end */
    if (rc == 0) couch_log("board thread: joined in %.2f s%s", sec, in_fetch ? ", it was in a fetch" : "");
    else couch_log("board thread: sysThreadJoin rc 0x%08x after %.2f s, %s", (unsigned)rc, sec, board_left ? "the thread had left" : "the thread may still run");
    return rc == 0 || board_left ? 0 : -1;
}

void board_refresh(void)
{
    if (started) want = 1;
}

/* ---- the main thread's side ---------------------------------------------------- */

/* brief: a redirect without its Location, for the stale status line. */
static void fetch_words(char *w, int n, const http_result *r, int prc, int brief)
{
    char e[24];
    switch (r->step) {
    case H_OK:
        if (prc == BP_OK) snprintf(w, (size_t)n, "ok");
        else if (prc == BP_VERSION) snprintf(w, (size_t)n, "Board format %d: update Couch", tmp.version);
        else if (prc == BP_CUT) snprintf(w, (size_t)n, "the reply was cut (no end line)");
        else snprintf(w, (size_t)n, "the answer is not a Board reply");
        break;
    case H_DNS: snprintf(w, (size_t)n, "DNS failed for %s", url.host); break;
    case H_SOCKET: snprintf(w, (size_t)n, "no socket (%s)", net_err_words((int)r->err, e, sizeof e)); break;
    case H_CONNECT: snprintf(w, (size_t)n, "no connection to %s:%d (%s)", url.host, url.port, net_err_words((int)r->err, e, sizeof e)); break;
    case H_SEND: snprintf(w, (size_t)n, "the request was not sent (%s)", net_err_words((int)r->err, e, sizeof e)); break;
    case H_RECV:
        if (r->err == 35 || r->err == 60) snprintf(w, (size_t)n, "no answer in 10 s");
        else snprintf(w, (size_t)n, "the answer broke off (%s)", net_err_words((int)r->err, e, sizeof e));
        break;
    case H_CLOSED: snprintf(w, (size_t)n, "the connection closed before an answer"); break;
    case H_NOT_HTTP: snprintf(w, (size_t)n, "the answer is not HTTP"); break;
    case H_HEAD: snprintf(w, (size_t)n, "the answer's head is over 4 KB"); break;
    case H_STATUS:
        if (r->http >= 300 && r->http < 400 && r->location[0])
            snprintf(w, (size_t)n, "HTTP %d%s%s: the host still redirects plain HTTP", r->http, brief ? "" : " to ", brief ? "" : r->location);
        else snprintf(w, (size_t)n, "HTTP %d", r->http);
        break;
    case H_CHUNKED: snprintf(w, (size_t)n, "the answer is chunked, Couch reads a plain body"); break;
    case H_OVER: snprintf(w, (size_t)n, "the reply is over 4 KB"); break;
    default: snprintf(w, (size_t)n, "stopped"); break;
    }
}

void board_take(void)
{
    if (!ready) return;
    __sync_synchronize();
    const http_result *r = &slot.r;
    int prc = -1;
    if (r->step == H_OK) {
        prc = board_parse(slot.body, r->len, utf8_rc != 0, &tmp);
        if (prc == BP_OK) {
            board_cur = tmp;                 /* swapped in only whole: version 1 and the end line */
            board_have = 1;
            cur_at = slot.at;
            board_has_skew = tmp.now > 0;
            if (board_has_skew) board_skew = clock_utc() - tmp.now;
        }
    }
    board_fetches++;
    board_last_ok = prc == BP_OK;
    if (!board_last_ok) board_fails++;
    fetch_words(board_last_words, sizeof board_last_words, r, prc, 0);
    fetch_words(stale_words, sizeof stale_words, r, prc, 1);
    board_last_utc = clock_utc();
    board_last_us = slot.us;
    board_last_http = r->http;
    board_last_len = r->len;
    /* The address again, for the status line and the Info screen (DHCP may have
     * answered since): here the thread is idle, since it starts no fetch while
     * ready is set, so no two network calls overlap. */
    if (net_state == NET_UP) net_addr();
    if (logged < LOG_FETCHES) {
        logged++;
        char t[24];
        clock_words(t, sizeof t, board_last_utc, 1);
        if (board_last_ok)
            couch_log("fetch %d at %s (%.1f s): %.2f s, HTTP %d, %d bytes: ok, now %ld, offset %d, console - Worker %+ld s", board_fetches, t,
                      (double)(slot.at - t_start) / 1e6, (double)slot.us / 1e6, r->http, r->len, tmp.now, tmp.offset, board_skew);
        else
            couch_log("fetch %d at %s (%.1f s): %.2f s, HTTP %d, %d bytes: %s", board_fetches, t, (double)(slot.at - t_start) / 1e6,
                      (double)slot.us / 1e6, r->http, r->len, board_last_words);
    }
    __sync_synchronize();
    ready = 0;
}

int board_offset(int *offset)
{
    if (!board_have || !board_cur.has_offset) return 0;
    *offset = board_cur.offset;
    return 1;
}

long board_age(void) { return board_have ? (long)((sysGetSystemTime() - cur_at) / 1000000) : 0; }

void board_next_words(char *buf, int n)
{
    char a[24];
    s64 now = sysGetSystemTime();
    buf[0] = 0;
    if (!started) return;
    if (busy) snprintf(buf, (size_t)n, "Fetching the Board");
    else if (want && now - last_us < GAP_US) {
        age_words(a, sizeof a, (long)((last_us + GAP_US - now + 999999) / 1000000));
        snprintf(buf, (size_t)n, "Refresh in %s", a);
    } else if (board_fetches && next_us > now) {
        age_words(a, sizeof a, (long)((next_us - now + 999999) / 1000000));
        snprintf(buf, (size_t)n, "%s in %s", board_last_ok ? "Next update" : "Next try", a);
    }
}

/* ---- the page -------------------------------------------------------------------- */

/* A card's foot: its source and the data's age, or the reason in YELLOW. */
static void feed_foot(float x, float y, float w, const char *src, const board_feed *f)
{
    char a[24];
    age_words(a, sizeof a, f->age + board_age());
    if (!strcmp(f->state, "stale")) text_fit(x, y, F_SMALL, YELLOW, w, "%s. The data is %s old.", f->reason[0] ? f->reason : src, a);
    else if (!strcmp(f->state, "none")) text_fit(x, y, F_SMALL, GREY, w, "%s: no data", src);
    else if (f->age < 0) text_fit(x, y, F_SMALL, GREY, w, "%s", src);
    else text_fit(x, y, F_SMALL, GREY, w, "%s, %s old", src, a);
}

/* The card's body when it has no data: the reason, or the wait for the first reply. */
static void no_data(float x, float y, float w, const char *what, const board_feed *f)
{
    char line[200];
    if (!board_have) snprintf(line, sizeof line, "The %s shows after the first reply.", what);
    else snprintf(line, sizeof line, "No %s data: %s.", what, f->reason[0] ? f->reason : "the reply has none");
    text_wrap(x, y, F_BODY, board_have ? YELLOW : DIM, w, 4, line);
}

/* A row's stripe, centred on its text. */
static void stripe(float x, float ty, float w, float step)
{
    rect(x, ty - (step - (float)line_h(F_BODY)) / 2, w, step, PANEL2);
}

static void usd_card(float x, float y, float w, float h)
{
    const board_data *b = &board_cur;
    float ix = x + 28, iw = w - 56, fy = y + h - 20 - (float)line_h(F_SMALL);
    card(x, y, w, h);
    text(ix, y + 20, F_MED, WHITE, "Dollar");
    if (!board_have || !b->n_usd) {
        no_data(ix, y + 86, iw, "dollar", &b->usd_feed);
        if (board_have) feed_foot(ix, fy, iw, "dolarapi.com", &b->usd_feed);
        return;
    }
    float ru = x + w - 28, rs = ru - 150, rb = rs - 150, hy = y + 82;
    text_r(rb, hy, F_SMALL, GREY, "Buy");
    text_r(rs, hy, F_SMALL, GREY, "Sell");
    text_r(ru, hy, F_SMALL, GREY, "Updated");
    float ry = hy + (float)line_h(F_SMALL) + 16, step = (fy - 16 - ry) / (float)b->n_usd;
    if (step > 48) step = 48;
    for (int i = 0; i < b->n_usd; i++, ry += step) {
        const board_usd *u = &b->usd[i];
        if (i % 2 == 0) stripe(x + 12, ry, w - 24, step);
        text_fit(ix, ry, F_BODY, WHITE, rb - 126 - ix, "%s", u->label);
        text_r(rb, ry, F_BODY | TNUM, WHITE, "%s", u->buy);
        text_r(rs, ry, F_BODY | TNUM, WHITE, "%s", u->sell);
        text_r(ru, ry, F_BODY | TNUM, GREY, "%s", u->when);
    }
    feed_foot(ix, fy, iw, "dolarapi.com", &b->usd_feed);
}

static void wx_card(float x, float y, float w, float h)
{
    const board_data *b = &board_cur;
    float ix = x + 28, iw = w - 56, fy = y + h - 20 - (float)line_h(F_SMALL);
    card(x, y, w, h);
    text(ix, y + 20, F_MED, WHITE, "Weather");
    if (!board_have || (!b->has_wx && !b->n_day)) {
        no_data(ix, y + 86, iw, "weather", &b->wx_feed);
        if (board_have) feed_foot(ix, fy, iw, "Open-Meteo", &b->wx_feed);
        return;
    }
    float ny = y + 80;
    if (b->has_wx) {
        float tx = text(ix, ny, F_LARGE | TNUM, WHITE, "%s", b->temp);
        text_fit(tx + 28, ny + (float)base_dy(F_LARGE, F_MED), F_MED, WHITE, x + w - 28 - tx - 28, "%s, %s", b->words, b->is_day ? "day" : "night");
        ny += (float)line_h(F_LARGE) + 2;
        text_fit(ix, ny, F_BODY, GREY, iw, "Feels like %s, humidity %s, wind %s", b->feels, b->humidity, b->wind);
        ny += (float)line_h(F_BODY) + 26;
    }
    if (b->n_day) {
        float rr = x + w - 28, rmin = rr - 120, rmax = rmin - 100, wx0 = ix + 130;
        text_r(rmax, ny, F_SMALL, GREY, "Max");
        text_r(rmin, ny, F_SMALL, GREY, "Min");
        text_r(rr, ny, F_SMALL, GREY, "Rain");
        float ry = ny + (float)line_h(F_SMALL) + 16, step = (fy - 16 - ry) / (float)b->n_day;
        if (step > 48) step = 48;
        for (int i = 0; i < b->n_day; i++, ry += step) {
            const board_day *d = &b->day[i];
            if (i % 2 == 0) stripe(x + 12, ry, w - 24, step);
            text_fit(ix, ry, F_BODY, WHITE, wx0 - 10 - ix, "%s", d->label);
            text_fit(wx0, ry, F_BODY, GREY, rmax - 80 - wx0, "%s", d->words);
            text_r(rmax, ry, F_BODY | TNUM, WHITE, "%s", d->max);
            text_r(rmin, ry, F_BODY | TNUM, WHITE, "%s", d->min);
            text_r(rr, ry, F_BODY | TNUM, BLUE, "%s", d->rain);
        }
    }
    feed_foot(ix, fy, iw, "Open-Meteo", &b->wx_feed);
}

static void status(void)
{
    char t[24];
    switch (net_state) {
    case NET_BLOCKED:
        status_line(journal_state("net_start") == J_SKIPPED
                        ? "Network not started: net_start was skipped before. SELECT info, then SQUARE twice clears the journal."
                        : "Network not started: net_start froze this console before. SELECT info, then SQUARE twice clears the journal.",
                    YELLOW);
        return;
    case NET_FAILED: status_linef(RED, "No network: netInitialize failed (rc 0x%08x). SQUARE tries again.", (unsigned)net_rc); return;
    case NET_NONE: status_line("The network is not started yet.", GREY); return;
    }
    if (!started) status_line("The Board thread did not start (couch.txt has the rc). SQUARE tries again.", RED);
    else if (!board_fetches) status_line("Fetching the Board ...", GREY);
    else if (!board_last_ok && !net_has_ip()) status_line("No network: the console has no IP address.", YELLOW);
    else if (!board_last_ok && board_have) {
        clock_words(t, sizeof t, clock_utc() - board_age(), 0);
        status_linef(YELLOW, "The last fetch failed: %s. The Board shows the data of %s.", stale_words, t);
    } else if (!board_last_ok) status_linef(YELLOW, "No Board data yet: %s.", board_last_words);
    else {
        clock_words(t, sizeof t, clock_utc() - board_age(), 0);
        status_linef(GREY, "Board updated at %s.", t);
    }
}

void board_draw(const char *msg, u32 msg_color)
{
    char next[48];
    begin_frame();
    title();
    /* the city when it fits beside the app's name (title_right has no width limit) */
    if (board_have && board_cur.city[0] && text_w(F_BODY, board_cur.city) <= SW - 2 * MG - 480) title_right("%s", board_cur.city);
    else title_right("Board");
    float cy = clock_draw(MG, TOP + 6) + 26, gap = 32, w = (SW - 2 * MG - gap) / 2;
    usd_card(MG, cy, w, BOTTOM - cy);
    wx_card(MG + w + gap, cy, w, BOTTOM - cy);
    if (msg) status_line(msg, msg_color);
    else status();
    footer("SQUARE refresh  SELECT info  START exit");
    board_next_words(next, sizeof next);
    float nw = next[0] ? (float)text_w(F_SMALL, next) + 40 : 0;
    if (next[0]) text_r(SW - MG, Y_NOTE, F_SMALL, GREY, "%s", next);
    if (board_url_note[0]) text_fit(MG, Y_NOTE, F_SMALL, YELLOW, SW - 2 * MG - nw, "%s", board_url_note);
    else if (board_url_file) text_fit(MG, Y_NOTE, F_SMALL, GREY, SW - 2 * MG - nw, "Board URL from board_url.txt: %s", board_url);
    ui_flip();
}
