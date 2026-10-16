/* The boot log (boot.c): APP_DIR/boot.txt, one line per mark of the start
 * and of the exit, written and synced at once, so a start that hangs the
 * console shows its last step. Safe before ui_init and in leave(). */
#ifndef BOOT_H
#define BOOT_H

void boot_mark(const char *name);    /* "main" first: it begins a start (and moves a file over 32 KB to boot.old.txt) */

#endif
