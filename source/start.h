/* The start (start.c): the loading screen, the times of the start's steps,
 * the start's job screen, and the start animation.
 *
 * Lucas, 2026-10-13: "it had a nice black delay. can we add a loading
 * screen". The first frame comes right after ui_init, before text_utf8,
 * digits_init, icons_init and the SPU start: Couch's name and "Starting"
 * with the steps under it, the step that runs marked. Each step is timed
 * from main's first statement, and couch.txt gets the times (after
 * fs_selftest, since couch.txt needs the folder): the black delay measured.
 * Each step also writes its mark to boot.txt (boot.h). The screen uses only
 * the kit's ASCII fonts and shapes: nothing else is loaded yet.
 *
 * The effects' and the network's starts run their calls in a job thread
 * while the main thread draws this screen with a counter (the kit's
 * run_job, in Couch's look: a counter that stops is the freeze signal).
 *
 * Then the loop's first frame begins the start animation (design 3.9): the
 * sky fades in from the loading screen's colour, the header moves down into
 * place, the two columns and the bottom rise into place, a little apart,
 * and the clock's digits flip from 00:00 to the time. Any button ends it.
 * The main thread only. */
#ifndef START_H
#define START_H

#include <ppu-types.h>

/* The steps, in their order; start_name gives couch.txt's and boot.txt's names. */
enum { SS_UI, SS_UTF8, SS_DIGITS, SS_ICONS, SS_FS, SS_FX, SS_NET, SS_FRAME, SS_FETCH, SS_N };
extern const char *const start_name[SS_N];
extern s64 t_main;                   /* sysGetSystemTime at main's first statement */

void start_main(void);               /* main's first statement after boot_mark("main") */
/* Before a step: a loading frame that shows it running (none before
 * ui_init). After it: its time and its boot.txt mark. start_end of SS_FRAME
 * and SS_FETCH count only their first time. */
void start_begin(int step);
void start_end(int step);
/* fn in a job thread while the loading screen (or, after the start, the
 * same look with only that step) draws a counter; step names the line
 * (SS_FX, SS_NET). As the kit's run_job: no thread, one frame and the call
 * here. */
void start_job(int step, void (*fn)(void));
/* couch.txt: the steps to fs_selftest (after log_start), and the rest once
 * the first fetch is in (or at the exit). */
void start_log_early(void);
void start_log_late(void);

/* The start animation: start_anim_begin at the loop's first frame; any
 * button ends it (start_anim_input). start_k(part) is how far a part is, 0
 * (where it starts) to 1 (in place), eased; 1 when the animation is off. */
enum { SA_FADE, SA_HEADER, SA_LEFT, SA_RIGHT, SA_BOTTOM, SA_CLOCK };
void start_anim_begin(void);
void start_anim_input(unsigned pressed_now);
float start_k(int part);
int start_anim_on(void);
#define SA_RISE 30                   /* px the columns rise */
#define SA_DROP 24                   /* px the header comes down */
#define LOADING_TOP 0x0a1222ff       /* the loading screen's colours (the kit's background), the start's fade from them */
#define LOADING_BOT 0x13264cff

#endif
