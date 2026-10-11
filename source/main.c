/* Couch 0.1.0: the Board on the living-room TV, drawn by ps3gfx. The clock
 * in the console's time zone, the dollar rates and the weather from the
 * Worker at couch.daddiego.com.ar (design:
 * ~/.claude/plans/2026-10-10-couch-design.md, cut to the Board on
 * 2026-10-10). Couch opens on the Board; SELECT opens the Info screen. Each
 * start appends a block to couch.txt in the app folder.
 *
 * The frame loop: read_pad, the exit, board_take (a new reply from the
 * Board thread), the input, the draw. Only this thread calls journal_* and
 * writes files: the Board thread fills one record (board.c). */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <lv2/systime.h>
#include <sys/file.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/fs.h>
#include <ps3gfx/journal.h>
#include "couch.h"
#include "http.h"
#include "board.h"
#include "clock.h"
#include "info.h"
#include "parse.h"

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

/* Every exit path: START, Quit Game from the PS button menu, and exit(). */
static void leave(void)
{
    board_stop();
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

/* The Board opens, at start and after the Info screen: the network start
 * (also after a clear of the journal), the clock's settings, the thread.
 * -1 for START at the clock's prompt. */
static int board_enter(void)
{
    if (net_state != NET_UP) {
        int before = net_state;
        net_start(fs_ok);
        if (net_state == NET_UP) couch_log("net_start: rc 0x%08x, address %s", (unsigned)net_rc, net_ip);
        else if (net_state == NET_FAILED) couch_log("net_start: rc 0x%08x", (unsigned)net_rc);
        else if (before != NET_BLOCKED) couch_log("net_start: not run, the journal holds it as %s", journal_state("net_start") == J_SKIPPED ? "skipped" : "frozen");
    }
    if (clock_tz() < 0) return -1;
    if (net_state == NET_UP && !board_running()) board_start();
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
 * screenshot, the Info screen and its screenshot, the Board again. The
 * round ends at auto.txt's seconds (60 to 600). */
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
    int info = 0, step = 0;
    while (!quit) {
        read_pad();
        int lv = ui_leave();
        if (lv < 0) break;
        board_take();
        if (info) {
            if (lv == 0) {                   /* CIRCLE: back to the Board */
                info = 0;
                if (board_enter() < 0) break;
                continue;
            }
            info_input();
        } else if (pressed & BTN_SELECT) {
            info = 1;
            info_open();
            continue;
        } else if ((pressed & BTN_SQUARE) && square() < 0) break;
        s64 t = sysGetSystemTime() - t_start;
        if (unattended > 0) {
            if (t >= (s64)unattended * 1000000) break;
            if (step == T_INFO && t >= t_sec[T_INFO] * 1000000LL) {
                info = 1;
                info_open();
                step++;
            } else if (step == T_BOARD && t >= t_sec[T_BOARD] * 1000000LL) {
                info = 0;
                step++;
            }
        }
        if (info) info_draw();
        else board_draw(NULL, 0);
        if (unattended > 0 && (step == T_SHOT_BOARD || step == T_SHOT_INFO) && t >= t_sec[step] * 1000000LL) {
            shot(step == T_SHOT_BOARD ? APP_DIR "/board.ppm" : APP_DIR "/info.ppm");   /* between ui_flip and the next begin_frame */
            step++;
        }
    }
    return 0;                                /* exit() runs leave() */
}
