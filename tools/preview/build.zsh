#!/bin/zsh
# Build the host preview through ps3gfx's preview, as Cell Bench's build.zsh:
# the library's tools/preview/build.zsh compiles this app's source/ and
# tools/preview/pv_app.c (the Board's HTTP peer, the clock settings) with the
# library's own sources, against its stub headers. Two binaries:
#   build/pv           the app
#   build/pv_utf8_off  the app built with -DCOUCH_UTF8_OFF: text_utf8 is not
#                      called, as on a console where it failed (the
#                      utf8_off scenario: the byte text and the fold)
# The checkout is $PS3GFX, else $PS3DEV/src/ps3gfx, the clone PS3 Health's
# sdk_setup.sh makes at the pinned commit; a checkout at another commit is
# announced, since the pkg links the pin.
#   PV_CFLAGS='-DGFX_VTX_BYTES=8192' zsh build.zsh    (extra flags, passed through)
H=${PS3_HEALTH:-$HOME/code/ps3-hdd-ssd-health}
L=${PS3GFX:-${PS3DEV:-$HOME/ps3dev}/src/ps3gfx}
[[ -f $L/tools/preview/build.zsh ]] || { print -u2 "no ps3gfx checkout in $L: run $H/sdk_setup.sh, or set PS3GFX"; exit 1 }
rev=$(sed -n 's/^PS3GFX_REV=\([0-9a-f]*\).*/\1/p' $H/sdk_setup.sh 2>/dev/null)
head=$(git -C $L rev-parse HEAD 2>/dev/null); head=${head:-unknown}
[[ $head == $rev ]] || print "preview on ps3gfx ${head[1,7]}, the pkg links ${rev[1,7]:-?}"
set -e
A=${0:A:h:h:h}
O=${0:A:h}/build
PV_CFLAGS="-DCOUCH_UTF8_OFF ${PV_CFLAGS:-}" zsh $L/tools/preview/build.zsh APP=$A > /dev/null
mv $O/pv $O/pv_utf8_off
print "built $O/pv_utf8_off"
PV_CFLAGS="${PV_CFLAGS:-}" exec zsh $L/tools/preview/build.zsh APP=$A
