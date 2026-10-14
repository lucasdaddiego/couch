#!/bin/zsh
# Build couch.gnpdrm.pkg with PS3 Health's SDK and checks (Lucas,
# 2026-10-09: no copies), as Cell Bench's build.sh. Health's sdk_setup.sh
# makes $PS3DEV (the 2020 runtime, ps3gfx at the commit it pins: the stamp
# check below, PS3GFX_DEV=1 skips it), its tools/syscalls.py audits the ELF
# against this app's tools/syscalls.allow, and its verify_self.py checks the
# signed EBOOT against the ELF. PS3_HEALTH names the Health folder.
set -e
cd "${0:A:h}"
H=${PS3_HEALTH:-$HOME/code/ps3-hdd-ssd-health}
[[ -f $H/sdk_setup.sh && -f $H/verify_self.py && -f $H/tools/syscalls.py ]] \
  || { print -u2 "no PS3 Health checkout at $H (set PS3_HEALTH)"; exit 1 }
export PS3DEV=${PS3DEV:-$HOME/ps3dev}
export PSL1GHT=$PS3DEV
export PATH=$PATH:$PS3DEV/bin:$PS3DEV/ppu/bin
[[ -x $PS3DEV/ppu/bin/ppu-gcc ]] || { print -u2 "no toolchain in $PS3DEV: run $H/sdk_setup.sh"; exit 1 }
[[ -f $PS3DEV/.runtime-2020 ]] || { print -u2 "$PS3DEV has the 2021+ PSL1GHT runtime, whose apps die before main on this console: run $H/sdk_setup.sh"; exit 1 }
[[ -f $PS3DEV/portlibs/ppu/lib/libps3gfx.a ]] || { print -u2 "no ps3gfx in $PS3DEV/portlibs/ppu: run $H/sdk_setup.sh"; exit 1 }
rev=$(sed -n 's/^PS3GFX_REV=\([0-9a-f]*\).*/\1/p' $H/sdk_setup.sh); stamp=$(cat $PS3DEV/portlibs/ppu/share/ps3gfx/COMMIT 2>/dev/null)
[[ $PS3GFX_DEV == 1 || $stamp == $rev ]] \
  || { print -u2 "ps3gfx in $PS3DEV is ${stamp:-unstamped}, Health's sdk_setup.sh pins $rev: run it, or PS3GFX_DEV=1 to build against the installed one"; exit 1 }
mkdir -p pkgfiles/USRDIR
cp $PS3DEV/portlibs/ppu/share/ps3gfx/OFL.txt pkgfiles/USRDIR/OFL.txt   # every pkg on ps3gfx ships the fonts' license
cp $PS3DEV/portlibs/ppu/share/ps3gfx/PHOSPHOR.txt pkgfiles/USRDIR/PHOSPHOR.txt   # and the icons' (Phosphor, MIT), beside it
rm -f couch.pkg couch.gnpdrm.pkg
make pkg
${PYTHON:-python3} -I $H/tools/syscalls.py --allow tools/syscalls.allow build/couch.elf
${PYTHON:-python3} -I $H/verify_self.py build/pkg/USRDIR/EBOOT.BIN build/couch.elf
ls -la couch.gnpdrm.pkg
