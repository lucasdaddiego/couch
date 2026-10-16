/* The Info page (info.h, page 3 since 0.2.0; in 0.3.0's look since stage
 * 10, design 1.6): what Couch uses and how it went, in three glass tiles.
 * "Board": the Board URL and its source, the last fetch and its result, the
 * internet (the last fetch's class, its time and connect time), the
 * Worker's clock. "Console": the clock's source and the three settings, the
 * address, the journal states, text_utf8's rc, the folder. "Effects", the
 * whole width under them: the SPUs' state (or why RSX only), the pictures a
 * second and the frame budget's step, the busy time of each job, the rain,
 * the display's rate (ui_stats). SQUARE twice clears the freeze journal
 * (the kit's home-screen gesture, as on PS3 Health's home screen): the
 * frozen or skipped calls then run again when the Board opens. A value
 * wraps to at most 3 lines; a row that does not fit its tile is left off. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/journal.h>
#include <ps3gfx/icons.h>
#include "couch.h"
#include "info.h"
#include "board.h"
#include "clock.h"
#include "http.h"
#include "page.h"
#include "parse.h"
#include "spu_fx_run.h"
#include "start.h"


static float rnd(float x) { return floorf(x + 0.5f); }
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

/* A row: the key in GREY, the value wrapped in its colour (at most 3
 * lines), within the tile's bottom yb; the y under it (unchanged when the
 * row does not fit). */
static float key_w;
static float row(float x, float y, float w, float yb, const char *key, u32 c, const char *fmt, ...) __attribute__((format(printf, 7, 8)));
static float row(float x, float y, float w, float yb, const char *key, u32 c, const char *fmt, ...)
{
    char v[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(v, sizeof v, fmt, ap);
    va_end(ap);
    int font = page_wide() ? F_BODY : F_SMALL;
    int lines = text_wrap_lines(font, w - key_w, 3, v);
    float h = (float)(lines * wrap_step(font));
    if (y + h > yb) return y;
    text(x, y, font, GREY, "%s", key);
    text_wrap(x + key_w, y, font, c, w - key_w, 3, v);
    return y + h + 6;
}

/* A tile's head: an icon and the name in F_MED; the y of its first row. */
static float head(float x, float y, int ic, const char *name)
{
    if (!icons_rc) icon(ic, x + 16, y + 20, BLUE);
    text(x + (icons_rc ? 0 : 42), y + 2, F_MED, WHITE, "%s", name);
    return y + (float)line_h(F_MED) + 14;
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

static void tile_board(float x0, float y0, float w0, float h0)
{
    char a[200], b[48], c[24];
    float x = x0 + 24, w = w0 - 48, yb = y0 + h0 - 12;
    glass_tile(x0, y0, w0, h0);
    float y = head(x, y0 + 18, IC_S_ARROWS_CLOCKWISE, "Board");
    y = row(x, y, w, yb, "URL", WHITE, "%s (%s)", board_url, board_url_file ? "board_url.txt" : "the default");
    board_next_words(b, sizeof b);
    if (board_fetches) {
        char h[40] = "no HTTP answer";
        clock_words(c, sizeof c, board_last_utc, 1);
        if (board_last_http) snprintf(h, sizeof h, "HTTP %d, %d bytes", board_last_http, board_last_len);
        y = row(x, y, w, yb, "Last fetch", WHITE, "%s, %.2f s, %s. %d %s, %d failed.%s%s%s", c, (double)board_last_us / 1e6, h, board_fetches,
                board_fetches == 1 ? "fetch" : "fetches", board_fails, b[0] ? " " : "", b, b[0] ? "." : "");
        snprintf(a, sizeof a, "%s", board_last_ok ? "A good reply" : board_last_words);
        if (a[0] >= 'a' && a[0] <= 'z') a[0] = (char)(a[0] - 32);     /* the words start a sentence here */
        y = row(x, y, w, yb, "Result", board_last_ok ? GREEN : YELLOW, "%s.", a);
    } else {
        y = row(x, y, w, yb, "Last fetch", DIM, "%s",
                net_state == NET_FAILED ? "none: the network start failed" : net_state != NET_UP ? "none: the network is not started"
                : b[0] ? b : "none yet");
        y = row(x, y, w, yb, "Result", DIM, "-");
    }
    if (board_fetches) {                     /* the internet: the class of the last fetch and its times */
        char cn[32] = "no connection";
        if (board_connect_us >= 0) snprintf(cn, sizeof cn, "connect %.2f s", (double)board_connect_us / 1e6);
        if (board_net_q == NQ_DOWN) {
            clock_words(c, sizeof c, board_net_down_utc, 0);
            y = row(x, y, w, yb, "Internet", RED, "no answer since %s (%s)", c, cn);
        } else
            y = row(x, y, w, yb, "Internet", board_net_q == NQ_SLOW ? YELLOW : WHITE, "%s: %.2f s, %s (slow from %.0f s)",
                    board_net_q == NQ_SLOW ? "slow" : "ok", (double)board_net_us / 1e6, cn, (double)NQ_SLOW_US / 1e6);
    }
    if (board_have && board_has_skew) {
        long s = board_skew < 0 ? -board_skew : board_skew;
        age_words(c, sizeof c, s);
        utc_label(b, sizeof b, board_cur.offset);
        row(x, y, w, yb, "Worker", WHITE, "now %ld, offset %d s (%s). The console's clock %s%s%s.", board_cur.now, board_cur.offset, b,
            s < 2 ? "matches it" : "is ", s < 2 ? "" : c, s < 2 ? "" : board_skew > 0 ? " ahead" : " behind");
    } else row(x, y, w, yb, "Worker", DIM, "no reply yet");
}

static void tile_console(float x0, float y0, float w0, float h0)
{
    char a[200];
    float x = x0 + 24, w = w0 - 48, yb = y0 + h0 - 12;
    glass_tile(x0, y0, w0, h0);
    float y = head(x, y0 + 18, IC_S_INFO, "Console");
    clock_source(a, sizeof a);
    y = row(x, y, w, yb, "Clock", WHITE, "%s", a);
    if (tz_state == TZ_READ) {
        static const char *const name[3] = {"TIME_FORMAT", "TIMEZONE", "SUMMERTIME"};
        int k = 0;
        for (int i = 0; i < 3; i++) {                /* each call: its value, or its rc when it failed */
            if (tz_rc[i]) k += snprintf(a + k, sizeof a - k, "%s%s rc 0x%08x", i ? ", " : "", name[i], (unsigned)tz_rc[i]);
            else if (i == 0) k += snprintf(a + k, sizeof a - k, "%s %d (%s)", name[i], (int)tz_val[i], clock_h24() ? "24-hour" : "12-hour");
            else k += snprintf(a + k, sizeof a - k, ", %s %d%s", name[i], (int)tz_val[i], i == 1 ? " min" : "");
        }
        y = row(x, y, w, yb, "Settings", tz_rc[0] || tz_rc[1] || tz_rc[2] ? YELLOW : WHITE, "%s", a);
    } else
        y = row(x, y, w, yb, "Settings", DIM, "not read: %s",
                tz_state == TZ_SKIPPED ? "skipped at the prompt" : tz_state == TZ_FROZEN ? "tz_params froze this console before"
                : tz_state == TZ_NO_JOURNAL ? "the folder is not writable, so no journal" : "not yet");
    if (net_state == NET_UP) y = row(x, y, w, yb, "Address", net_has_ip() ? WHITE : YELLOW, "%s", net_ip);
    else if (net_state == NET_FAILED) y = row(x, y, w, yb, "Address", YELLOW, "none: netInitialize rc 0x%08x", (unsigned)net_rc);
    else y = row(x, y, w, yb, "Address", DIM, "none: the network is not started");
    y = row(x, y, w, yb, "Journal", journal_io_failed() ? YELLOW : WHITE, "net_start %s, tz_params %s, fx_first %s%s", jword("net_start"), jword("tz_params"),
            jword("fx_first"), journal_io_failed() ? "; not written: a freeze may go unrecorded" : "");   /* ps3gfx 3ea00e1 */
    if (utf8_rc == 0) y = row(x, y, w, yb, "UTF-8 text", WHITE, "on: text_utf8 rc 0");
    else y = row(x, y, w, yb, "UTF-8 text", YELLOW, "off: text_utf8 rc %d, so the Board's accents are folded to ASCII", utf8_rc);
    if (!fs_ok)
        row(x, y, w, yb, "Folder", RED, "not writable: mkdir rc 0x%08x, open rc 0x%08x, write rc 0x%08x", (unsigned)fs_mk, (unsigned)fs_op,
            (unsigned)fs_wr);
}

static void tile_effects(float x0, float y0, float w0, float h0)
{
    fx_stats st;
    ui_stat u;
    float x = x0 + 24, w = w0 - 48, yb = y0 + h0 - 12;
    glass_tile(x0, y0, w0, h0);
    float y = head(x, y0 + 18, IC_S_SUN_HORIZON, "Effects");
    fx_get_stats(&st);
    ui_stats(&u, 0);
    if (fx_state == FXS_SPU) {
        y = row(x, y, w, yb, "State", WHITE, "6 SPUs running: the sky and the glass on 5, the rain on 1 (step %d: %s)", fx_step, st.step_words);
        y = row(x, y, w, yb, "Pictures", WHITE, "the sky %d a second (%u shown), the aurora %d a second (%u shown)", st.sky_rate,
                (unsigned)st.sky_n, st.aur_rate, (unsigned)st.aur_n);
        y = row(x, y, w, yb, "Busy", WHITE, "sky and glass %.2f ms a picture (%.2f ms most), aurora %.2f ms, rain %.3f ms a job (the slowest SPU's mean)",
                st.sky_ms, st.sky_max, st.aur_ms, st.rain_ms);
        y = row(x, y, w, yb, "Rain", WHITE, "%d %s now; %u jobs, %u reports over 2 ms", st.drops, st.flakes ? "flakes" : "drops",
                (unsigned)st.rain_jobs, (unsigned)st.rain_late);
    } else if (fx_state == FXS_RSX) {
        y = row(x, y, w, yb, "State", YELLOW, "RSX only: %s", fx_why);
        y = row(x, y, w, yb, "Pictures", WHITE, "the gradient, four cloud sprites, the PPU's rain (300 drops), solid panels, no aurora");
    } else y = row(x, y, w, yb, "State", DIM, "not started");
    double sec = (double)(u.busy_sum_us + u.wait_sum_us) / 1e6, n = u.frames ? (double)u.frames : 1;
    row(x, y, w, yb, "Display", WHITE, "%.2f frames a second; busy %.2f ms mean, %.2f ms most; %u periods over 25 ms", sec > 0 ? u.frames / sec : 0.0,
        (double)u.busy_sum_us / n / 1000, u.busy_max_us / 1000.0, (unsigned)u.slow);
}

void info_body(void)
{
    int wide = page_wide();
    float top = page_top(), bot = page_bar_y() - 12, x = MG, w = SW - 2 * MG, ch = bot - top;
    float eh = rnd(ch * (wide ? 0.40f : 0.42f)), th = ch - eh - RG, hw = (w - G) / 2;
    key_w = wide ? 180 : 150;
    shift_push(0, (int)rnd(SA_RISE * (1 - start_k(SA_LEFT))));
    tile_board(x, top, rnd(hw), th);
    shift_pop();
    shift_push(0, (int)rnd(SA_RISE * (1 - start_k(SA_RIGHT))));
    tile_console(rnd(x + hw + G), top, rnd(hw), th);
    shift_pop();
    shift_push(0, (int)rnd(SA_RISE * (1 - start_k(SA_BOTTOM))));
    tile_effects(x, top + th + RG, w, eh);
    shift_pop();
}

void info_bar(float y)
{
    const char *hints = fs_ok ? "SQUARE twice clear journal  CIRCLE back  START exit" : "CIRCLE back  START exit";
    if (armed == BTN_SQUARE) bar_draw(y, "Press SQUARE again to clear the freeze journal. Any other button cancels.", YELLOW, NULL, hints);
    else if (msg[0]) bar_draw(y, msg, msg_color, NULL, hints);
    else bar_draw(y, fs_ok ? "Each start appends to couch.txt in " APP_DIR "." : "The app cannot write " APP_DIR ": no journal and no couch.txt.",
                  fs_ok ? GREY : RED, NULL, hints);
}
