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
 * wraps to at most 3 lines; a row that does not fit its tile is left off.
 * Since 0.5.0 the Console tile also has the Cell and RSX temperatures and
 * the fan duty (PS3 Health's journaled reads, every 10 s while the page
 * shows) and night mode's hours, which TRIANGLE edits in place. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/journal.h>
#include <ps3gfx/fx.h>
#include <ps3gfx/icons.h>
#include <ppu-lv2.h>
#include <sys/systime.h>
#include <lv2/systime.h>
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

/* ---- the console's temperatures and fan duty (0.5.0) ------------------------------------- */

#define TEMPS_US 10000000LL          /* a read every 10 s while the page shows */

/* PS3 Health's calls (ata.c), read-only. 383 sys_game_get_temperature: 0 =
 * Cell, 1 = RSX; degrees in the top byte. */
static s32 get_temperature(u32 id, u32 *t) { lv2syscall2(383, id, (u64)t); return_to_user_prog(s32); }
/* 409 sys_sm_get_fan_policy: the read half of what webMAN uses; speed 0..255. */
static s32 sm_get_fan_policy(u8 *st, u8 *mode, u8 *speed, u8 *unk)
{
    lv2syscall5(409, 0, (u64)st, (u64)mode, (u64)speed, (u64)unk);
    return_to_user_prog(s32);
}

static int cpu_c = -1, rsx_c = -1, fan_pct = -1, reads;   /* the last read: degrees C and %, -1 none */
static s32 temps_rc = -1, fan_rc = -1;
static s64 read_at;                  /* the last read's system time, 0: none yet */
static int fan_ask;                  /* the fan's first-run prompt came this session and said run */

/* Health's console_temps: journaled while the state is not known this session. */
static s32 read_temps(void)
{
    u32 t = 0;
    s32 rc;
    cpu_c = rsx_c = -1;
    int journal = journal_needed("temps");
    if (journal && !journal_start("temps")) return -1;
    rc = get_temperature(0, &t);
    if (rc == 0) cpu_c = (int)(t >> 24);
    if (rc == 0 && get_temperature(1, &t) == 0) rsx_c = (int)(t >> 24);
    if (journal) journal_done("temps", rc, rc == 0);
    return rc;
}

/* Health's fan_policy_read: -1 and no value for a frozen or skipped entry. */
static s32 read_fan(void)
{
    static u8 v[4] __attribute__((aligned(16)));
    s32 rc;
    fan_pct = -1;
    int journal = journal_needed("fan_policy");
    if (journal && !journal_start("fan_policy")) return -1;
    rc = sm_get_fan_policy(&v[0], &v[1], &v[2], &v[3]);
    if (rc == 0) fan_pct = v[2] * 100 / 255;
    if (journal) journal_done("fan_policy", rc, rc == 0);
    return rc;
}

/* The fan is read once the journal has it (an earlier run or this
 * session's prompt said run); never in a session that skipped the prompt
 * (an unattended round), and not without a journal. */
static int fan_allowed(void)
{
    int js = journal_state("fan_policy");
    return fs_ok && js != J_FROZEN && js != J_SKIPPED && (js != J_NONE || fan_ask);
}

void info_tick(void)
{
    s64 now = sysGetSystemTime();
    if (page != PAGE_INFO || !fs_ok || (read_at && now - read_at < TEMPS_US)) return;   /* no journal, no system call */
    read_at = now ? now : 1;
    temps_rc = read_temps();
    if (fan_allowed()) fan_rc = read_fan();
    frame_ev |= EV_JOURNAL;
    if (reads++ == 0)                        /* couch.txt: the first read of the start */
        couch_log("console: Cell %d C, RSX %d C (383 rc 0x%08x), fan %d %% (409 %s 0x%08x)", cpu_c, rsx_c, (unsigned)temps_rc, fan_pct,
                  fan_allowed() ? "rc" : "not run,", (unsigned)fan_rc);
}

/* The kit's first_run_prompt in Couch's look (clock.c's tz_prompt): 1 run,
 * 0 skip, -1 exit. PS3 Health puts this read behind the same prompt: 409
 * is a system manager call that games do not make (383 is a game's call,
 * read with no prompt), so the first one on a console asks first. */
static int fan_prompt(void)
{
    while (1) {
        read_pad();
        if (quit || (pressed & BTN_START)) return -1;
        if (pressed & BTN_CROSS) return 1;
        if (pressed & BTN_CIRCLE) return 0;
        begin_frame();
        title();
        float x = MG + 40, w = SW - 2 * MG - 80, y = TOP + 10;
        card(MG, y, SW - 2 * MG, 500);
        round_rect(MG, y, 8, 500, 4, YELLOW);
        text_fit(x, y + 34, F_MED, YELLOW, w, "New on this console: the fan duty");
        text_fit(x, y + 112, F_BODY, WHITE, w, "The Info page reads the fan policy: the duty the system set, 0 to 100 %%.");
        text_fit(x, y + 156, F_BODY, WHITE, w, "The read half of what webMAN uses for its fan control. Nothing is written.");
        text_fit(x, y + 200, F_BODY, GREY, w, "Read-only: sys_sm_get_fan_policy (syscall 409), one journal entry named fan_policy.");
        text_wrap(x, y + 290, F_BODY, GREY, w, 4,
                  "The call is written to the journal first. If it freezes the console, hold the power button and "
                  "start the app again: it skips the call. A skip is kept until SQUARE twice on the Info screen. "
                  "Without it the page shows the temperatures only.");
        footer("CROSS run it  CIRCLE skip it  START exit");
        ui_flip();
    }
}

/* ---- night mode's edit (0.5.0) ----------------------------------------------------------- */

static int edit, e_field, e_on, e_from, e_to;   /* the edit: on, its field (0 on or off, 1 the start, 2 the end), its values */
static pad_rep rep;

int info_editing(void) { return edit; }

/* An hour one step up or down, past the other end (a start equal to the end is no span). */
static int hour_step(int h, int d, int other)
{
    h = (h + d + 24) % 24;
    return h == other ? (h + d + 24) % 24 : h;
}

static int night_edit(void)
{
    if (pressed & BTN_CIRCLE) {
        edit = 0;
        snprintf(msg, sizeof msg, "Night mode not changed.");
        msg_color = GREY;
        return 0;
    }
    if (pressed & BTN_CROSS) {
        int rc = night_save(e_on, e_from, e_to);
        edit = 0;
        if (rc == 0 && e_on) snprintf(msg, sizeof msg, "Night mode saved: %02d:00 to %02d:00, the waiting screen is a dim clock then.", e_from, e_to);
        else if (rc == 0) snprintf(msg, sizeof msg, "Night mode saved: off.");
        else snprintf(msg, sizeof msg, "Could not write night.txt: rc %d.", rc);
        msg_color = rc == 0 ? GREEN : RED;
        return 0;
    }
    if (pressed & (BTN_LEFT | BTN_RIGHT)) e_field = (e_field + (pressed & BTN_RIGHT ? 1 : 2)) % 3;
    unsigned d = pad_repeat(&rep, BTN_UP | BTN_DOWN);
    if (d) {
        int s = d & BTN_UP ? 1 : -1;
        if (e_field == 0) e_on = !e_on;
        else if (e_field == 1) e_from = hour_step(e_from, s, e_to);
        else e_to = hour_step(e_to, s, e_from);
        if (e_field) e_on = 1;               /* an hour changed: night mode on */
    }
    return 0;
}

/* No address read here: the Board thread may be in a fetch, and Couch makes
 * no two network calls at once (board_take reads it after each fetch). */
int info_open(void)
{
    armed = 0;
    msg[0] = 0;
    edit = 0;
    read_at = 0;                             /* the temperatures at once */
    if (fs_ok && unattended < 0 && journal_state("fan_policy") == J_NONE && !fan_ask) {   /* the first time, and again after SQUARE twice */
        int r = fan_prompt();
        if (r < 0) return -1;
        if (r == 0) journal_skip("fan_policy");
        else fan_ask = 1;
        couch_log("fan_policy: %s at the prompt", r ? "run" : "skipped");
    }
    return 0;
}

int info_input(void)
{
    if (edit) return night_edit();
    if (fs_ok && gesture(BTN_SQUARE) == BTN_SQUARE) {   /* no writable folder: nothing to clear */
        int rc = journal_clear();
        if (rc == 0) snprintf(msg, sizeof msg, "Freeze journal cleared. Frozen and skipped calls run again when the Board opens.");
        else snprintf(msg, sizeof msg, "Could not clear the freeze journal: rc 0x%08x", (unsigned)rc);
        msg_color = rc == 0 ? GREEN : RED;
        fan_ask = 0;
        fan_pct = -1;                        /* the fan is not read until its prompt says run again: no frozen value (0.5.0's review) */
    } else if (pressed & BTN_TRIANGLE) {
        armed = 0;
        if (!fs_ok) {
            snprintf(msg, sizeof msg, "The app cannot write " APP_DIR ", so night.txt cannot be saved.");
            msg_color = RED;
        } else {
            edit = 1;
            e_field = 0;
            e_on = night_set;
            e_from = night_from;
            e_to = night_to;
            msg[0] = 0;
        }
    } else if (pressed & ~BTN_SQUARE) msg[0] = 0;
    return 0;
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
    if (board_url[0]) y = row(x, y, w, yb, "URL", WHITE, "%s (board_url.txt)", board_url);
    else y = row(x, y, w, yb, "URL", YELLOW, "none: board_url.txt in " APP_DIR " needs one http:// line");
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
            y = row(x, y, w, yb, "Internet", board_net_q == NQ_SLOW ? YELLOW : WHITE, "%s: %s, the reply %.2f s (slow from a %.1f s connect)",
                    board_net_q == NQ_SLOW ? "slow" : "ok", cn, (double)board_net_us / 1e6, (double)NQ_SLOW_US / 1e6);
    }
    if (board_have && board_has_skew) {
        long s = board_skew < 0 ? -board_skew : board_skew;
        age_words(c, sizeof c, s);
        utc_label(b, sizeof b, board_cur.offset);
        row(x, y, w, yb, "Worker", WHITE, "now %ld, offset %d s (%s). The console's clock %s%s%s.", board_cur.now, board_cur.offset, b,
            s < 2 ? "matches it" : "is ", s < 2 ? "" : c, s < 2 ? "" : board_skew > 0 ? " ahead" : " behind");
    } else row(x, y, w, yb, "Worker", DIM, "no reply yet");
}

/* The Hardware row: "Cell 58 C, RSX 54 C, fan 32 %", or why a value is
 * missing; the temperatures only while the fan's prompt has not run since
 * SQUARE twice cleared the journal (it comes at the page's next opening). */
static float hardware_row(float x, float y, float w, float yb)
{
    char t[96], f[96];
    const char *sep = utf8_rc == 0 ? " \xc2\xb7 " : ", ";   /* a middle dot, a comma when the text is folded */
    int fj = journal_state("fan_policy");
    if (!fs_ok) return row(x, y, w, yb, "Hardware", DIM, "not read: the folder is not writable, so no journal");
    if (journal_state("temps") == J_FROZEN) return row(x, y, w, yb, "Hardware", RED, "not read: the temperature read froze this console before");
    if (!reads) return row(x, y, w, yb, "Hardware", DIM, "reading ...");
    if (cpu_c >= 0 && rsx_c >= 0) snprintf(t, sizeof t, "Cell %d C%sRSX %d C", cpu_c, sep, rsx_c);
    else if (cpu_c >= 0) snprintf(t, sizeof t, "Cell %d C%sRSX not read", cpu_c, sep);
    else snprintf(t, sizeof t, "temperatures not read (383 rc 0x%08x)", (unsigned)temps_rc);
    if (fan_pct >= 0) snprintf(f, sizeof f, "fan %d %%", fan_pct);
    else if (fj == J_SKIPPED) snprintf(f, sizeof f, "fan skipped at the prompt");
    else if (fj == J_FROZEN) snprintf(f, sizeof f, "fan read froze before");
    else if (!fan_allowed() && unattended >= 0) snprintf(f, sizeof f, "fan not read (unattended)");
    else if (!fan_allowed()) f[0] = 0;       /* the journal was cleared: the prompt first */
    else snprintf(f, sizeof f, "fan not read (409 rc 0x%08x)", (unsigned)fan_rc);
    int warm = cpu_c >= 70 || rsx_c >= 70;   /* PS3 Health's mark: 70 C and up */
    return row(x, y, w, yb, "Hardware", cpu_c < 0 || warm ? YELLOW : WHITE, "%s%s%s", t, f[0] ? sep : "", f);
}

/* The Night row: night.txt's hours, or the edit (TRIANGLE): the three
 * fields, the chosen one on BLUE. */
static float night_row(float x, float y, float w, float yb)
{
    char a[96];
    if (!edit) {
        if (night_set)
            return row(x, y, w, yb, "Night", WHITE, "%02d:00 to %02d:00: the waiting screen is a dim clock%s", night_from, night_to, night_now() ? " (now)" : "");
        return row(x, y, w, yb, "Night", night_note[0] ? YELLOW : WHITE, "%s", night_note[0] ? night_note : "off: the waiting screen as by day");
    }
    int font = page_wide() ? F_BODY : F_SMALL;
    float lh = (float)wrap_step(font), xx = x + key_w, bh = (float)line_h(font) + 6;
    if (y + lh + 8 > yb) return y;
    text(x, y, font, GREY, "%s", "Night");
    for (int i = 0; i < 3; i++) {
        if (i == 0) snprintf(a, sizeof a, "%s", e_on ? "On" : "Off");
        else snprintf(a, sizeof a, "%02d:00", i == 1 ? e_from : e_to);
        float bw = (float)text_w(font, a) + 28, by = y - 3;
        if (i == e_field) {
            round_rect(xx, by, bw, bh, bh / 2, BLUE);
            aa_corners(xx, by, bw, bh, bh / 2, BLUE);
        } else round_frame(xx, by, bw, bh, bh / 2, 1, 0xffffff50);
        text(rnd(xx + 14), y, font, i == e_field ? 0x0b1426ff : !e_on && i ? DIM : WHITE, "%s", a);
        xx += bw + 10;
        if (i == 1) xx = text(xx + 4, y, font, GREY, "to") + 14;
    }
    return y + lh + 14;
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
    y = hardware_row(x, y, w, yb);
    y = night_row(x, y, w, yb);
    y = row(x, y, w, yb, "Journal", journal_io_failed() ? YELLOW : WHITE, "net_start %s, tz_params %s, fx_first %s, temps %s, fan_policy %s%s",
            jword("net_start"), jword("tz_params"), jword("fx_first"), jword("temps"), jword("fan_policy"),
            journal_io_failed() ? "; not written: a freeze may go unrecorded" : "");   /* ps3gfx 3ea00e1 */
    if (utf8_rc != 0)                        /* 0.5.0: only when off (the Hardware and Night rows took its room) */
        y = row(x, y, w, yb, "UTF-8 text", YELLOW, "off: text_utf8 rc %d, so the Board's accents are folded to ASCII", utf8_rc);
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
    const char *hints = fs_ok ? "TRIANGLE night  SQUARE twice clear journal  CIRCLE back  START exit" : "CIRCLE back  START exit";
    if (edit) bar_draw(y, "Night mode: the hours of the waiting screen's dim clock.", GREY, NULL, "LEFT/RIGHT field  UP/DOWN change  CROSS save  CIRCLE cancel");
    else if (armed == BTN_SQUARE) bar_draw(y, "Press SQUARE again to clear the freeze journal. Any other button cancels.", YELLOW, NULL, hints);
    else if (msg[0]) bar_draw(y, msg, msg_color, NULL, hints);
    else bar_draw(y, fs_ok ? "Each start appends to couch.txt in " APP_DIR "." : "The app cannot write " APP_DIR ": no journal and no couch.txt.",
                  fs_ok ? GREY : RED, NULL, hints);
}
