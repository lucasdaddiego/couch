/* The Info page (info.c): SELECT on the Board, the News or the Futbol page
 * opens it, CIRCLE goes back to the Board. Since 0.5.0 also the console's
 * temperatures and fan duty, and night mode's hours (TRIANGLE edits them). */
#ifndef INFO_H
#define INFO_H

/* The screen opens: the gesture and the message cleared; the first time
 * the journal does not know the fan read, its first-run prompt (not in an
 * unattended round: no fan read then). -1 for START at the prompt. */
int info_open(void);
/* After read_pad: SQUARE twice clears the freeze journal; TRIANGLE edits
 * the night's hours, then LEFT and RIGHT pick a field, UP and DOWN change
 * it, CROSS saves night.txt, CIRCLE cancels. 0 (no exit here). */
int info_input(void);
int info_editing(void);              /* 1 during the night's edit: CIRCLE is its cancel */
/* Each frame: while the Info page shows, the Cell and RSX temperatures
 * (syscall 383) and the fan duty (409) every 10 s, journaled as temps and
 * fan_policy (PS3 Health's calls). */
void info_tick(void);

#endif
