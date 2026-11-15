# ETCH DS -- by Synth Centric
# One `make` builds etch_ds.nds (ARM9 + ARM7) with BlocksDS.
#   make            the ROM
#   make test       host tests: CORROSION checksums + ETCH's 44 generator tests,
#                   sequencer and DSP tests (needs a host C compiler), and the
#                   no-division/no-float audio-path check if the ROM is built
#   make tables     regenerate arm9/source/dsp_tables.c (Python 3 + NumPy)
# Based on BlocksDS' rom_arm9arm7 template (CC0).

ifeq ($(origin BLOCKSDS), undefined)
  ifneq ($(wildcard /opt/blocksds/core),)
    BLOCKSDS := /opt/blocksds/core
  else
    BLOCKSDS := /opt/wonderful/thirdparty/blocksds/core
  endif
endif
BLOCKSDSEXT	?= /opt/blocksds/external
WONDERFUL_TOOLCHAIN	?= /opt/wonderful
export BLOCKSDS BLOCKSDSEXT WONDERFUL_TOOLCHAIN

NAME		:= etch_ds
GAME_TITLE	:= ETCH DS
GAME_SUBTITLE	:= It squelches (milestone 1)
GAME_AUTHOR	:= Synth Centric
GAME_ICON	:= assets/icon.bmp

MAKE		:= make
RM		:= rm -rf
HOSTCC		?= cc
PYTHON		?= python3

ifeq ($(VERBOSE),1)
V		:=
else
V		:= @
endif

ROM		:= $(NAME).nds

.PHONY: all clean arm9 arm7 test check tables

all: check $(ROM)

clean:
	@echo "  CLEAN"
	$(V)$(MAKE) -f Makefile.arm9 clean --no-print-directory
	$(V)$(MAKE) -f Makefile.arm7 clean --no-print-directory
	$(V)$(RM) $(ROM) build

# CORROSION must stay byte-identical to ETCH 2.5: refuse to build otherwise
check:
	@echo "  CHECK   corrosion/SHA256SUMS"
	$(V)cd corrosion && sha256sum --quiet -c SHA256SUMS
	$(V)cd tests/host && sha256sum --quiet -c SHA256SUMS

arm9: check
	$(V)+$(MAKE) -f Makefile.arm9 --no-print-directory

arm7: check
	$(V)+$(MAKE) -f Makefile.arm7 --no-print-directory

GAME_FULL_TITLE := $(GAME_TITLE);$(GAME_SUBTITLE);$(GAME_AUTHOR)

$(ROM): arm9 arm7
	@echo "  NDSTOOL $@"
	$(V)$(BLOCKSDS)/tools/ndstool/ndstool -c $@ \
		-7 build/arm7.elf -9 build/arm9.elf \
		-b $(GAME_ICON) "$(GAME_FULL_TITLE)"

tables:
	$(PYTHON) tools/gen_tables.py

test: check
	$(V)$(MAKE) -C tests/host --no-print-directory HOSTCC=$(HOSTCC)
	@if [ -f build/arm9.elf ]; then echo "== audio path (build/arm9.elf) =="; \
		$(PYTHON) tools/check_audio_path.py | tail -1; fi
