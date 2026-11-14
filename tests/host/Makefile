# ETCH DS host tests.  Run from the top level with `make test`.
HOSTCC ?= cc
CFLAGS := -O2 -Wall -Wno-unused-function -I../../common -I../../corrosion -I../../arm9/source
CORR   := ../../corrosion/corrosion.c ../../corrosion/pattern.c ../../corrosion/names.c
ENGINE := ../../arm9/source/acid.arm.c ../../arm9/source/foundry.arm.c ../../arm9/source/fx.arm.c \
          ../../arm9/source/render.arm.c ../../arm9/source/dsp_tables.c \
          ../../arm9/source/demo.c ../../arm9/source/evolve.c
OUT    := ../../build/host

.PHONY: all
all: $(OUT)/test_generator $(OUT)/test_seq $(OUT)/test_dsp
	@echo "== ETCH's CORROSION tests (tests/test_generator.c, unmodified) against corrosion/ =="
	@$(OUT)/test_generator | tail -1
	@echo "== sequencer core =="
	@$(OUT)/test_seq
	@echo "== sound engine =="
	@cd $(OUT) && ./test_dsp

$(OUT)/test_generator: test_generator.c $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(OUT)/test_seq: test_seq.c ../../common/seq_core.c $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^

$(OUT)/test_dsp: test_dsp.c ../../common/seq_core.c $(ENGINE) $(CORR)
	@mkdir -p $(OUT)
	$(HOSTCC) $(CFLAGS) -o $@ $^ -lm
