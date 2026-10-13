/* The Info page (info.c): page 3; SELECT on the Board or the News opens it,
 * CIRCLE goes back to the Board, L1 and R1 to the other pages. */
#ifndef INFO_H
#define INFO_H

void info_open(void);                /* the screen opens: the gesture and the message cleared */
void info_input(void);               /* after read_pad: SQUARE twice clears the freeze journal */
void info_draw(void);

#endif
