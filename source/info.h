/* The Info screen (info.c): SELECT on the Board opens it, CIRCLE goes back. */
#ifndef INFO_H
#define INFO_H

void info_open(void);                /* the screen opens: the gesture and the message cleared */
void info_input(void);               /* after read_pad: SQUARE twice clears the freeze journal */
void info_draw(void);

#endif
