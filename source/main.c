/* Couch 0.2.0: the Board on the living-room TV, drawn by ps3gfx. The clock
 * in the console's time zone, the dollar rates and the money extras, the
 * weather with the rain, the sun and the moon, and the news, from the Worker
 * at couch.daddiego.com.ar (design: ~/.claude/plans/2026-10-10-couch-design.md,
 * cut to the Board on 2026-10-10). Three pages: the Board, the News, the
 * Info screen; L1 and R1 move between them, SELECT opens the Info page.
 * After 10 min with no button the ambient mode dims the screen (page.c).
 * Each start appends a block to couch.txt in the app folder.
 *
 * The frame loop: read_pad, the ambient mode's wake, the exit, board_take
 * (a new reply from the Board thread), the input, the ambient mode's step,
 * the draw. Only this thread calls journal_* and writes files: the Board
 * thread fills one record (board.c). */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <lv2/systime.h>
#include <sys/file.h>
#include <sysutil/sysutil.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fs.h>
#include <ps3gfx/journal.h>
#include "couch.h"
#include "http.h"
#include "board.h"
#include "clock.h"
#include "info.h"
#include "page.h"
#include "parse.h"
#include "spu_fx_run.h"

#ifndef PS3GFX_COMMIT
#define PS3GFX_COMMIT "unknown"      /* the preview: the Makefile defines it on the console */
#endif

#define LOG APP_DIR "/couch.txt"
#define AUTO APP_DIR "/auto.txt"
#define LOG_MAX 65536                /* couch.txt is emptied at start past 64 KB */
#define HOLD_US 30000000             /* an unattended start stays in game at least 30 s (the Mac's round polls every 15 s) */

int fs_ok, fs_mk, fs_op, fs_wr;
int utf8_rc = -1;
int unattended = -1;
s64 t_start;

void couch_log(const char *fmt, ...)
{
    char line[400];
    va_list ap;
    if (!fs_ok) return;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line - 1, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
    line[n++] = '\n';
    fs_write_file(LOG, line, (u64)n, 1);
}

/* Every exit path: START, Quit Game from the PS button menu, and exit().
 * exit() runs this once (atexit), after main returned. The Board thread
 * ends first (its socket shut down, then the join), then the SPU group
 * (fx_stop: the exit reports and the join, or the terminate and the join;
 * its threads write into textures and the vertex area, so it is down
 * before ui_end), then the network (net_end), then the picture (ui_end). A
 * Board thread that did not end keeps the network as it is: no net_end
 * under a network call. */
static void leave(void)
{
    int board = board_stop();
    fx_stop();
    if (board == 0) net_end();
    else couch_log("net_end: not run, the Board thread did not end");
    if (fs_ok) {
        ui_stat s;
        ui_stats(&s, 0);
        double sec = (double)(s.busy_sum_us + s.wait_sum_us) / 1e6, n = s.frames ? (double)s.frames : 1;
        couch_log("ui_stats: %u frames in %.1f s, %.2f fps; busy %.3f ms mean, %.3f max; wait %.3f ms mean; period %.3f ms max; %u over 25 ms",
                  (unsigned)s.frames, sec, sec > 0 ? s.frames / sec : 0.0, (double)s.busy_sum_us / n / 1000, s.busy_max_us / 1000.0,
                  (double)s.wait_sum_us / n / 1000, s.period_max_us / 1000.0, (unsigned)s.slow);
        couch_log("exit at %.1f s\n", (double)(sysGetSystemTime() - t_start) / 1e6);
    }
    ui_end();
}

/* auto.txt (Cell Bench's unattended start): the seconds of a round, read
 * before the visible-area screen, which waits for the pad. */
static int auto_read(void)
{
    char b[32];
    int n = fs_read_file(AUTO, b, sizeof b - 1);
    return n <= 0 ? -1 : auto_parse(b, n);
}

static void log_start(void)
{
    sysFSStat st;
    civil_time c;
    if (!fs_ok) return;
    if (sysLv2FsStat(LOG, &st) == 0 && st.st_size > LOG_MAX) fs_write_file(LOG, "", 0, 0);
    civil_from_unix(clock_utc(), &c);
    couch_log("couch %s on ps3gfx %s: start at %04d-%02d-%02d %02d:%02d:%02d UTC, %s", APP_VERSION, PS3GFX_COMMIT, c.year, c.month, c.day,
              c.hour, c.min, c.sec, unattended > 0 ? "unattended" : unattended == 0 ? "unattended, auto.txt is not a number" : "by hand");
    couch_log("text_utf8: rc %d", utf8_rc);
    couch_log("board: %s (%s)%s%s", board_url, board_url_file ? "board_url.txt" : "the default", board_url_note[0] ? ". " : "", board_url_note);
}

/* The Board opens, at start and after the Info screen: the effects' start
 * (design 6.3: once, and again after a clear of the journal), the network
 * start (also after a clear of the journal), the clock's settings, the
 * thread.
 * -1 for a quit (Quit Game from the PS button menu) and for START at the
 * clock's prompt. The quit is read before the network start and again
 * after it, since the start's job screen draws frames, and each frame reads
 * the system's events. */
static int board_enter(void)
{
    sysUtilCheckCallback();                  /* a Quit Game sent before the first frame (no read_pad since the start) */
    if (quit) {
        couch_log("quit before the Board opened: %s", net_state == NET_UP ? "the network was up" : "no network start");
        return -1;
    }
    fx_start();                              /* the SPUs behind their job screen, or RSX only */
    if (net_state != NET_UP) {
        int before = net_state;
        net_start(fs_ok);
        if (net_state == NET_UP || net_state == NET_FAILED)
            couch_log("net_start: rc 0x%08x, address %s; the netctl module rc 0x%08x, netCtlInit rc 0x%08x", (unsigned)net_rc, net_ip,
                      (unsigned)net_mod_rc, (unsigned)net_ctl_rc);
        else if (before != NET_BLOCKED) couch_log("net_start: not run, the journal holds it as %s", journal_state("net_start") == J_SKIPPED ? "skipped" : "frozen");
    }
    if (quit) {
        couch_log("quit during the network start: the Board thread not started");
        return -1;
    }
    if (clock_tz() < 0) return -1;
    if (net_state == NET_UP && !board_running()) board_start();
    return 0;
}

/* A page change by the pad. Leaving the Info page opens the Board as CIRCLE
 * did in 0.1.0 (the network start runs again after a clear of the journal):
 * -1 for a quit there. */
static int go(int to)
{
    int from = page;
    if (to == from) return 0;
    page = to;
    if (to == PAGE_INFO) info_open();
    if (from == PAGE_INFO) return board_enter();
    return 0;
}

/* SQUARE on the Board: a refresh, or the network start and the thread again. */
static int square(void)
{
    if (net_state == NET_UP && board_running()) {
        board_refresh();
        return 0;
    }
    return board_enter();
}

static void shot(const char *path)
{
    s32 rc = ui_shot(path);
    couch_log("shot %s: rc 0x%08x", path, (unsigned)rc);
}

/* An unattended round's steps, in seconds after the start: the Board's
 * screenshot, the Info page and its screenshot, the Board again. The round
 * ends at auto.txt's seconds (60 to 600): the ambient mode, at 600 s with no
 * button, cannot start before. */
enum { T_SHOT_BOARD, T_INFO, T_SHOT_INFO, T_BOARD, T_DONE };
static const int t_sec[T_DONE] = {30, 40, 45, 50};

int main(void)
{
    ui_init(&(const ui_app){"Couch", APP_VERSION, APP_DIR});
    t_start = sysGetSystemTime();
#ifdef COUCH_UTF8_OFF
    utf8_rc = -1;                            /* the preview's utf8_off build: a console whose text_utf8 failed */
#else
    utf8_rc = text_utf8();                   /* the Board's text is UTF-8 (the degree sign, a city's accents) */
#endif
    fs_ok = fs_selftest(APP_DIR, &fs_mk, &fs_op, &fs_wr) == 0;
    unattended = auto_read();
    if (unattended >= 0 && fs_ok) fs_write_file(AUTO, "0\n", 2, 0);   /* at once: a later start by hand, also after a freeze, is not a round */
    if (!safe_load() && fs_ok && unattended < 0) safe_calibrate(1);   /* the first start: the visible area of this TV */
    atexit(leave);
    board_init();
    log_start();
    if (unattended == 0 || (unattended > 0 && !fs_ok)) {
        const char *why = !fs_ok ? "the app cannot write its folder" : "auto.txt does not hold a number of seconds";
        char line[160];
        couch_log("unattended start, no run: %s", why);
        snprintf(line, sizeof line, "Unattended start, no run: %s. Couch quits to the XMB.", why);
        while (!quit && sysGetSystemTime() - t_start < HOLD_US) {
            read_pad();
            if (ui_leave() < 0) break;
            board_draw(line, YELLOW);
        }
        return 0;                            /* to the XMB: exit() runs leave() */
    }
    if (board_enter() < 0) return 0;
    ui_stat st;
    ui_stats(&st, 1);                        /* the frame times from here: the Board's loop */
    int step = 0;
    page = PAGE_BOARD;
    while (!quit) {
        read_pad();
        ambient_input();                     /* a press that wakes the screen is cleared: START then does not exit */
        int lv = ui_leave();
        if (lv < 0) break;
        board_take();
        if (pressed & (BTN_L1 | BTN_R1)) {
            if (go((page + (pressed & BTN_R1 ? 1 : PAGE_N - 1)) % PAGE_N) < 0) break;
        } else if (page == PAGE_INFO) {
            if (lv == 0) {                   /* CIRCLE: back to the Board */
                if (go(PAGE_BOARD) < 0) break;
            } else info_input();
        } else if (pressed & BTN_SELECT) {
            go(PAGE_INFO);
        } else if ((pressed & BTN_SQUARE) && square() < 0) break;
        s64 t = sysGetSystemTime() - t_start;
        if (unattended > 0) {
            if (t >= (s64)unattended * 1000000) break;
            if (step == T_INFO && t >= t_sec[T_INFO] * 1000000LL) {
                page = PAGE_INFO;
                info_open();
                step++;
            } else if (step == T_BOARD && t >= t_sec[T_BOARD] * 1000000LL) {
                page = PAGE_BOARD;
                step++;
            }
        }
        ambient_tick();
        fx_tick(ambient ? FXM_AURORA : FXM_SKY);   /* after the last ui_flip: a picture's swap, the next picture */
        if (page == PAGE_INFO) info_draw();
        else if (page == PAGE_NEWS) news_draw();
        else board_draw(NULL, 0);
        if (unattended > 0 && (step == T_SHOT_BOARD || step == T_SHOT_INFO) && t >= t_sec[step] * 1000000LL) {
            shot(step == T_SHOT_BOARD ? APP_DIR "/board.ppm" : APP_DIR "/info.ppm");   /* between ui_flip and the next begin_frame */
            step++;
        }
    }
    return 0;                                /* exit() runs leave() */
}
