/* The Info page (info.h, page 3 since 0.2.0): what Couch uses and how it
 * went. The Board URL and its source, the last fetch and its result, the
 * internet (the last fetch's class, its time and connect time), the
 * Worker's clock; the clock's source and the console's three settings; the
 * console's address, the journal states, text_utf8's rc. SQUARE twice clears the freeze journal
 * (the kit's home-screen gesture, as on PS3 Health's home screen): the
 * frozen or skipped calls then run again when the Board opens. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/journal.h>
#include "couch.h"
#include "info.h"
#include "board.h"
#include "clock.h"
#include "http.h"
#include "page.h"
#include "parse.h"
#include "spu_fx_run.h"

#define KEY_W 250                    /* the key column */
#define ROW 40

static char msg[160];
static u32 msg_color;

/* No address read here: the Board thread may be in a fetch, and Couch makes
 * no two network calls at once (board_take reads it after each fetch). */
void info_open(void)
{
    armed = 0;
    msg[0] = 0;
}

void info_input(void)
{
    if (fs_ok && gesture(BTN_SQUARE) == BTN_SQUARE) {   /* no writable folder: nothing to clear */
        int rc = journal_clear();
        if (rc == 0) snprintf(msg, sizeof msg, "Freeze journal cleared. Frozen and skipped calls run again when the Board opens.");
        else snprintf(msg, sizeof msg, "Could not clear the freeze journal: rc 0x%08x", (unsigned)rc);
        msg_color = rc == 0 ? GREEN : RED;
    } else if (pressed & ~BTN_SQUARE) msg[0] = 0;
}

static float row(float x, float y, float w, const char *key, u32 c, const char *fmt, ...) __attribute__((format(printf, 6, 7)));
static float row(float x, float y, float w, const char *key, u32 c, const char *fmt, ...)
{
    va_list ap;
    text(x, y, F_BODY, GREY, "%s", key);
    va_start(ap, fmt);
    vtext_fit(x + KEY_W, y, F_BODY, c, w - KEY_W, fmt, ap);
    va_end(ap);
    return y + ROW;
}

static const char *jword(const char *name)
{
    switch (journal_state(name)) {
    case J_PENDING: return "running";
    case J_OK: return "ok";
    case J_BAD: return "failed";
    case J_FROZEN: return "froze";
    case J_SKIPPED: return "skipped";
    default: return "not run";
    }
}

void info_draw(void)
{
    char a[200], b[48], c[24];
    float y = page_begin(), x = MG, w = SW - 2 * MG;
    card(x, y, w, BOTTOM - y);
    x += 32;
    w -= 64;
    y = heading(x, y + 20, "BOARD");
    y = row(x, y, w, "URL", WHITE, "%s (%s)", board_url, board_url_file ? "board_url.txt" : "the default");
    board_next_words(b, sizeof b);
    if (board_fetches) {
        char h[40] = "no HTTP answer";
        clock_words(c, sizeof c, board_last_utc, 1);
        if (board_last_http) snprintf(h, sizeof h, "HTTP %d, %d bytes", board_last_http, board_last_len);
        y = row(x, y, w, "Last fetch", WHITE, "%s, %.2f s, %s. %d %s, %d failed.%s%s%s", c, (double)board_last_us / 1e6, h, board_fetches,
                board_fetches == 1 ? "fetch" : "fetches", board_fails, b[0] ? " " : "", b, b[0] ? "." : "");
        snprintf(a, sizeof a, "%s", board_last_ok ? "A good reply" : board_last_words);
        if (a[0] >= 'a' && a[0] <= 'z') a[0] = (char)(a[0] - 32);     /* the words start a sentence here */
        y = row(x, y, w, "Result", board_last_ok ? GREEN : YELLOW, "%s.", a);
    } else {
        y = row(x, y, w, "Last fetch", DIM, "%s",
                net_state == NET_FAILED ? "none: the network start failed" : net_state != NET_UP ? "none: the network is not started"
                : b[0] ? b : "none yet");
        y = row(x, y, w, "Result", DIM, "-");
    }
    if (board_fetches) {                     /* the internet: the class of the last fetch and its times */
        char cn[32] = "no connection";
        if (board_connect_us >= 0) snprintf(cn, sizeof cn, "connect %.2f s", (double)board_connect_us / 1e6);
        if (board_net_q == NQ_DOWN) {
            clock_words(c, sizeof c, board_net_down_utc, 0);
            y = row(x, y, w, "Internet", RED, "no answer since %s (%s)", c, cn);
        } else
            y = row(x, y, w, "Internet", board_net_q == NQ_SLOW ? YELLOW : WHITE, "%s: %.2f s, %s (slow from %.0f s)",
                    board_net_q == NQ_SLOW ? "slow" : "ok", (double)board_net_us / 1e6, cn, (double)NQ_SLOW_US / 1e6);
    }
    if (board_have && board_has_skew) {
        long s = board_skew < 0 ? -board_skew : board_skew;
        age_words(c, sizeof c, s);
        utc_label(b, sizeof b, board_cur.offset);
        y = row(x, y, w, "Worker", WHITE, "now %ld, offset %d s (%s). The console's clock %s%s%s.", board_cur.now, board_cur.offset, b,
                s < 2 ? "matches it" : "is ", s < 2 ? "" : c, s < 2 ? "" : board_skew > 0 ? " ahead" : " behind");
    } else y = row(x, y, w, "Worker", DIM, "no reply yet");
    y = heading(x, y + 14, "CLOCK");
    clock_source(a, sizeof a);
    y = row(x, y, w, "Source", WHITE, "%s", a);
    if (tz_state == TZ_READ) {
        static const char *const name[3] = {"TIME_FORMAT", "TIMEZONE", "SUMMERTIME"};
        int k = 0;
        for (int i = 0; i < 3; i++) {                /* each call: its value, or its rc when it failed */
            if (tz_rc[i]) k += snprintf(a + k, sizeof a - k, "%s%s rc 0x%08x", i ? ", " : "", name[i], (unsigned)tz_rc[i]);
            else if (i == 0) k += snprintf(a + k, sizeof a - k, "%s %d (%s)", name[i], (int)tz_val[i], clock_h24() ? "24-hour" : "12-hour");
            else k += snprintf(a + k, sizeof a - k, ", %s %d%s", name[i], (int)tz_val[i], i == 1 ? " min" : "");
        }
        y = row(x, y, w, "Settings", tz_rc[0] || tz_rc[1] || tz_rc[2] ? YELLOW : WHITE, "%s", a);
    } else
        y = row(x, y, w, "Settings", DIM, "not read: %s",
                tz_state == TZ_SKIPPED ? "skipped at the prompt" : tz_state == TZ_FROZEN ? "tz_params froze this console before"
                : tz_state == TZ_NO_JOURNAL ? "the folder is not writable, so no journal" : "not yet");
    y = heading(x, y + 14, "CONSOLE");
    if (net_state == NET_UP) y = row(x, y, w, "Address", net_has_ip() ? WHITE : YELLOW, "%s", net_ip);
    else if (net_state == NET_FAILED) y = row(x, y, w, "Address", YELLOW, "none: netInitialize rc 0x%08x", (unsigned)net_rc);
    else y = row(x, y, w, "Address", DIM, "none: the network is not started");
    y = row(x, y, w, "Journal", WHITE, "net_start %s, tz_params %s", jword("net_start"), jword("tz_params"));
    fx_words(a, sizeof a);
    y = row(x, y, w, "Effects", fx_state == FXS_SPU ? WHITE : fx_state == FXS_RSX ? YELLOW : DIM, "%s", a);
    if (utf8_rc == 0) y = row(x, y, w, "UTF-8 text", WHITE, "on: text_utf8 rc 0");
    else y = row(x, y, w, "UTF-8 text", YELLOW, "off: text_utf8 rc %d, so the Board's accents are folded to ASCII", utf8_rc);
    if (!fs_ok)
        row(x, y, w, "Folder", RED, "not writable: mkdir rc 0x%08x, open rc 0x%08x, write rc 0x%08x", (unsigned)fs_mk, (unsigned)fs_op,
            (unsigned)fs_wr);
    if (armed == BTN_SQUARE) status_line("Press SQUARE again to clear the freeze journal. Any other button cancels.", YELLOW);
    else if (msg[0]) status_line(msg, msg_color);
    else status_line(fs_ok ? "Each start appends to couch.txt in " APP_DIR "." : "The app cannot write " APP_DIR ": no journal and no couch.txt.",
                     fs_ok ? GREY : RED);
    footer(fs_ok ? "SQUARE twice clear journal  L1/R1 pages  CIRCLE back  START exit" : "L1/R1 pages  CIRCLE back  START exit");
    page_end();
}
