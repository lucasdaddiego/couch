#---------------------------------------------------------------------------------
# Couch: PSL1GHT's app Makefile, as Cell Bench's without the SPU program. The
# network libraries come after ps3gfx (libnet, libnetctl, libsysmodule: the
# Board's HTTP client and the network start, as PS3 Health's Transfer module).
#---------------------------------------------------------------------------------
.SUFFIXES:
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

include $(PSL1GHT)/ppu_rules

TARGET		:=	couch
BUILD		:=	build
SOURCES		:=	source
DATA		:=
INCLUDES	:=
PKGFILES	:=	$(CURDIR)/pkgfiles
SFOXML		:=	$(CURDIR)/sfo.xml

TITLE		:=	Couch
APPID		:=	COUC00001
CONTENTID	:=	EP0001-$(APPID)_00-0000000000000000

CFLAGS		=	-O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
# the installed ps3gfx's commit stamp, for couch.txt's start line: the hash cut
# to 12 characters, with its -dirty suffix kept (Cell Bench's line)
CFLAGS		+=	-DPS3GFX_COMMIT=\"$(shell sed 's/^\([0-9a-f]\{12\}\)[0-9a-f]*/\1/;q' $(PSL1GHT)/portlibs/ppu/share/ps3gfx/COMMIT 2>/dev/null)\"
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

# ps3gfx first: GNU ld resolves left to right
LIBS	:=	-lps3gfx -lgcm_sys -lio -lm -lsysutil -lnet -lnetctl -lsysmodule
LIBDIRS	:=	$(PSL1GHT)/portlibs/ppu

ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR	:=	$(CURDIR)/$(BUILD)
export BUILDDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))

export LD	:=	$(CC)
export OFILES	:=	$(CFILES:.c=.o)
export INCLUDE	:=	$(foreach dir,$(INCLUDES), -I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					$(LIBPSL1GHT_INC) \
					-I$(CURDIR)/$(BUILD)
export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					$(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean pkg

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) *.elf *.self *.pkg *.map

pkg:	$(BUILD) $(OUTPUT).pkg

#---------------------------------------------------------------------------------
else

DEPENDS	:=	$(OFILES:.o=.d)

# The CEX self is not used (as PS3 Health's Makefile): the pkg rule signs the
# ELF again with make_self_npdrm, and the macOS ARM64 make_self crashes under
# make. The empty .self is a stamp for ppu_rules' "%.pkg: %.self".
$(OUTPUT).self: $(OUTPUT).elf
	@mkdir -p $(BUILDDIR)
	@$(STRIP) $< -o $(BUILDDIR)/$(notdir $<)
	@$(SPRX) $(BUILDDIR)/$(notdir $<)
	@touch $@
$(OUTPUT).elf:	$(OFILES)

-include $(DEPENDS)

endif
