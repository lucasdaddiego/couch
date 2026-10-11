/* What the library's stub headers lack for Couch, force-included into every
 * file of the preview by build.zsh (-include), as PS3 Health's pv_health.h:
 * the library's stub/ comes first on the include path, so a
 * sysmodule/sysmodule.h here would never be read. pv_app.c defines it. */
#ifndef PV_COUCH_H
#define PV_COUCH_H
#include "ppu-types.h"
s32 sysModuleUnload(u32 id);                 /* net_end: the netctl module */
#endif
