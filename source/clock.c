/* The Board's clock (clock.h). PS3 Health 2.0.3 reads TIMEZONE and
 * SUMMERTIME at start (ata.c, console_timezone) and its console round of
 * 2026-10-10 got -180 for Buenos Aires: minutes east of UTC. Couch also
 * reads TIME_FORMAT (0x0115), which no app has read on this console; 0 is
 * taken as 12 h and 1 as 24 h (not checked). The three calls are one
 * journal entry, behind the first-run prompt as the design says. */
#include <stdio.h>
#include <string.h>
#include <sys/systime.h>
#include <sysutil/sysutil.h>
#include <ps3gfx/ui.h>
#include <ps3gfx/journal.h>
#include "couch.h"
#include "clock.h"
#include "board.h"
#include "parse.h"

int tz_state = TZ_NOT_RUN;
s32 tz_rc[3] = {-1, -1, -1}, tz_val[3];
static const s32 tz_id[3] = {SYSUTIL_SYSTEMPARAM_ID_TIME_FORMAT, SYSUTIL_SYSTEMPARAM_ID_TIMEZONE, SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME};

static void tz_read(void)
{
    for (int i = 0; i < 3; i++) {
        s32 v = 0;
        tz_rc[i] = sysUtilGetSystemParamInt(tz_id[i], &v);
        tz_val[i] = tz_rc[i] ? 0 : v;
    }
}

/* couch.txt gets one line per state the read reaches in a session. */
static void tz_log(void)
{
    static int logged = -1;
    if (tz_state == logged) return;
    logged = tz_state;
    if (tz_state == TZ_READ)
        couch_log("tz_params: TIME_FORMAT %d (rc 0x%08x), TIMEZONE %d (rc 0x%08x), SUMMERTIME %d (rc 0x%08x)", (int)tz_val[0],
                  (unsigned)tz_rc[0], (int)tz_val[1], (unsigned)tz_rc[1], (int)tz_val[2], (unsigned)tz_rc[2]);
    else if (tz_state == TZ_SKIPPED) couch_log("tz_params: skipped at the prompt");
    else if (tz_state == TZ_FROZEN) couch_log("tz_params: not run, it froze this console before");
    else if (tz_state == TZ_NO_JOURNAL) couch_log("tz_params: not run, no journal (the folder is not writable)");
}

/* The kit's first_run_prompt (ps3gfx ui.c), drawn here with its layout,
 * keys and return values: 1 run, 0 skip, -1 exit. The kit's paragraph keeps
 * a skip "until SQUARE twice on the home screen", and Couch's home screen is
 * the Board, where SQUARE is the refresh: this paragraph names the Info
 * screen, where info_input clears the journal. */
static int tz_prompt(void)
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
        text_fit(x, y + 34, F_MED, YELLOW, w, "New on this console: the clock settings");
        text_fit(x, y + 112, F_BODY, WHITE, w, "Couch reads three system settings: the time zone, summer time and the 12 or 24-hour clock.");
        text_fit(x, y + 156, F_BODY, WHITE, w, "Without them, the clock takes the Board's offset for its city and shows 24 hours.");
        text_fit(x, y + 200, F_BODY, GREY, w, "Read-only: sysUtilGetSystemParamInt, one journal entry named tz_params.");
        text_wrap(x, y + 290, F_BODY, GREY, w, 4,
                  "The call is written to the journal first. If it freezes the console, hold the power button and "
                  "start the app again: it skips the call. A skip is kept until SQUARE twice on the Info screen "
                  "(SELECT on the Board opens it).");
        footer("CROSS run it  CIRCLE skip it  START exit");
        ui_flip();
    }
}

int clock_tz(void)
{
    if (tz_state == TZ_READ) return 0;
    int js = journal_state("tz_params");
    if (js == J_FROZEN) tz_state = TZ_FROZEN;
    else if (js == J_SKIPPED) tz_state = TZ_SKIPPED;
    else if (!fs_ok) tz_state = TZ_NO_JOURNAL;      /* the prompt promises a journal: the Board's offset instead */
    else {
        if (js == J_NONE && unattended < 0) {
            int r = tz_prompt();
            if (r < 0) return -1;
            if (r == 0) {
                journal_skip("tz_params");
                tz_state = TZ_SKIPPED;
                tz_log();
                return 0;
            }
        }
        int j = journal_needed("tz_params");
        if (j && !journal_start("tz_params")) tz_state = TZ_FROZEN;
        else {
            tz_read();
            if (j) journal_done("tz_params", tz_rc[1] ? tz_rc[1] : tz_rc[0] ? tz_rc[0] : tz_rc[2], tz_rc[1] == 0);
            tz_state = TZ_READ;
        }
    }
    tz_log();
    return 0;
}

int clock_offset(int *offset)
{
    int off;
    if (tz_state == TZ_READ && tz_rc[1] == 0 && tz_offset(tz_val[1], tz_rc[2] == 0 ? tz_val[2] : 0, &off)) {
        *offset = off;
        return CLK_CONSOLE;
    }
    if (board_offset(&off)) {
        *offset = off;
        return CLK_BOARD;
    }
    *offset = 0;
    return CLK_UTC;
}

int clock_h24(void) { return !(tz_state == TZ_READ && tz_rc[0] == 0 && tz_val[0] == 0); }

long clock_utc(void)
{
    u64 sec = 0, nsec = 0;
    sysGetCurrentTime(&sec, &nsec);
    return (long)sec;
}

void clock_words(char *buf, int n, long utc, int secs)
{
    int off;
    civil_time c;
    const char *ampm;
    char hm[16];
    clock_offset(&off);
    civil_from_unix(utc + off, &c);
    clock_hm(hm, sizeof hm, c.hour, c.min, clock_h24(), &ampm);
    if (secs) snprintf(buf, (size_t)n, "%s:%02d%s%s", hm, c.sec, ampm ? " " : "", ampm ? ampm : "");
    else snprintf(buf, (size_t)n, "%s%s%s", hm, ampm ? " " : "", ampm ? ampm : "");
}

/* Why the console's setting is not the source. */
static const char *tz_why(char *buf, int n)
{
    switch (tz_state) {
    case TZ_SKIPPED: return "skipped at the prompt";
    case TZ_FROZEN: return "tz_params froze this console before";
    case TZ_NO_JOURNAL: return "not read, the folder is not writable";
    case TZ_READ:
        if (tz_rc[1]) snprintf(buf, (size_t)n, "TIMEZONE rc 0x%08x", (unsigned)tz_rc[1]);
        else snprintf(buf, (size_t)n, "TIMEZONE %d is out of range", (int)tz_val[1]);
        return buf;
    default: return "not read yet";
    }
}

void clock_source(char *buf, int n)
{
    int off, src = clock_offset(&off);
    char lab[16], why[48];
    utc_label(lab, sizeof lab, off);
    if (src == CLK_CONSOLE)
        snprintf(buf, (size_t)n, "%s, the console's time zone setting%s", lab, tz_rc[2] == 0 && tz_val[2] ? " with summer time" : "");
    else if (src == CLK_BOARD)
        snprintf(buf, (size_t)n, "%s, the Board's offset for %s. The console's setting: %s.", lab,
                 board_cur.city[0] ? board_cur.city : "its city", tz_why(why, sizeof why));
    else snprintf(buf, (size_t)n, "UTC: no offset yet. The console's setting: %s.", tz_why(why, sizeof why));
}

float clock_draw(float x, float y, float w)
{
    int off, src = clock_offset(&off);
    civil_time c;
    const char *ampm;
    char hm[16], line[200];
    civil_from_unix(clock_utc() + off, &c);
    clock_hm(hm, sizeof hm, c.hour, c.min, clock_h24(), &ampm);
    float dy = (float)base_dy(F_LARGE, F_MED);
    float xe = text(x, y, F_LARGE | TNUM, WHITE, "%s", hm);
    xe = text(xe + 2, y + dy, F_MED | TNUM, GREY, ":%02d", c.sec);
    if (ampm) xe = text(xe + 12, y + dy, F_MED, GREY, "%s", ampm);
    text_fit(xe + 40, y + dy, F_MED, WHITE, x + w - xe - 40, "%s %d %s %d", weekday_name[c.wday], c.day, month_name[c.month - 1], c.year);
    float y2 = y + line_h(F_LARGE) + 2;
    clock_source(line, sizeof line);
    text_fit(x, y2, F_SMALL, src == CLK_UTC ? YELLOW : GREY, w, "%s", line);
    return y2 + line_h(F_SMALL);
}
