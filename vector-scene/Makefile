# Build pipeline: C source -> cl65 (cc65) -> .prg -> c1541 -> .d64 -> VICE.
# See README.md for what this folder is testing and why.
#
#   make bench-pair-baseline-d64   - baseline draw+erase pair benchmark
#   make run-bench-pair-baseline   - launch VICE, autostarting it
#   make clean                    - remove build/

CL65     := cl65
C1541    := c1541
EMULATOR := x64sc

TARGET   := c64
BUILD    := build
CFG      := cfg/lowmem.cfg
CFLAGS   := -Oirs -Isrc -Itests

HDRS := src/hires.h src/dirtylist.h src/drawline_asm.h src/doublebuf.h \
        src/circle_table.h tests/bench_report.h tests/cia_timer.h

.PHONY: all clean \
        bench-pair-baseline bench-pair-baseline-d64 run-bench-pair-baseline \
        bench-pair-dirtylist bench-pair-dirtylist-d64 run-bench-pair-dirtylist \
        bench-pair-asm bench-pair-asm-d64 run-bench-pair-asm \
        bench-pair-smc bench-pair-smc-d64 run-bench-pair-smc \
        bench-angle-sweep bench-angle-sweep-d64 run-bench-angle-sweep \
        correctness-check correctness-check-d64 run-correctness-check \
        rotating-square rotating-square-d64 run-rotating-square \
        bench-rotating-square bench-rotating-square-d64 run-bench-rotating-square \
        rotating-square-smc rotating-square-smc-d64 run-rotating-square-smc \
        bench-rotating-square-smc bench-rotating-square-smc-d64 run-bench-rotating-square-smc

all: bench-pair-baseline-d64 bench-pair-dirtylist-d64 bench-pair-asm-d64 \
     bench-pair-smc-d64 bench-angle-sweep-d64 correctness-check-d64 \
     rotating-square-d64 bench-rotating-square-d64 \
     rotating-square-smc-d64 bench-rotating-square-smc-d64

$(BUILD):
	mkdir -p $(BUILD)

# --- baseline: draw+erase via two draw_line() calls ---

BASELINE_NAME := bench-pair-baseline
BASELINE_SRCS := tests/bench_pair_baseline.c src/hires.c
BASELINE_PRG  := $(BUILD)/$(BASELINE_NAME).prg
BASELINE_D64  := $(BUILD)/$(BASELINE_NAME).d64

bench-pair-baseline: $(BASELINE_PRG)

$(BASELINE_PRG): $(BASELINE_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(BASELINE_PRG) $(BASELINE_SRCS)

bench-pair-baseline-d64: $(BASELINE_D64)

$(BASELINE_D64): $(BASELINE_PRG)
	$(C1541) -format "$(BASELINE_NAME),id" d64 $(BASELINE_D64) -write $(BASELINE_PRG) $(BASELINE_NAME)

run-bench-pair-baseline: $(BASELINE_D64)
	$(EMULATOR) -autostart $(BASELINE_D64)

# --- trick #1: dirty-list erase ---

DIRTY_NAME := bench-pair-dirtylist
DIRTY_SRCS := tests/bench_pair_dirtylist.c src/hires.c src/dirtylist.c
DIRTY_PRG  := $(BUILD)/$(DIRTY_NAME).prg
DIRTY_D64  := $(BUILD)/$(DIRTY_NAME).d64

bench-pair-dirtylist: $(DIRTY_PRG)

$(DIRTY_PRG): $(DIRTY_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(DIRTY_PRG) $(DIRTY_SRCS)

bench-pair-dirtylist-d64: $(DIRTY_D64)

$(DIRTY_D64): $(DIRTY_PRG)
	$(C1541) -format "$(DIRTY_NAME),id" d64 $(DIRTY_D64) -write $(DIRTY_PRG) $(DIRTY_NAME)

run-bench-pair-dirtylist: $(DIRTY_D64)
	$(EMULATOR) -autostart $(DIRTY_D64)

# --- trick #2: hand-written 6502 ---

ASM_NAME := bench-pair-asm
ASM_SRCS := tests/bench_pair_asm.c src/hires.c src/drawline_asm.s
ASM_PRG  := $(BUILD)/$(ASM_NAME).prg
ASM_D64  := $(BUILD)/$(ASM_NAME).d64

bench-pair-asm: $(ASM_PRG)

$(ASM_PRG): $(ASM_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(ASM_PRG) $(ASM_SRCS)

bench-pair-asm-d64: $(ASM_D64)

$(ASM_D64): $(ASM_PRG)
	$(C1541) -format "$(ASM_NAME),id" d64 $(ASM_D64) -write $(ASM_PRG) $(ASM_NAME)

run-bench-pair-asm: $(ASM_D64)
	$(EMULATOR) -autostart $(ASM_D64)

# --- trick #2b: hand-written 6502, self-modified absolute addressing ---

SMC_NAME := bench-pair-smc
SMC_SRCS := tests/bench_pair_smc.c src/hires.c src/drawline_asm.s src/drawline_asm_smc.s
SMC_PRG  := $(BUILD)/$(SMC_NAME).prg
SMC_D64  := $(BUILD)/$(SMC_NAME).d64

bench-pair-smc: $(SMC_PRG)

$(SMC_PRG): $(SMC_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(SMC_PRG) $(SMC_SRCS)

bench-pair-smc-d64: $(SMC_D64)

$(SMC_D64): $(SMC_PRG)
	$(C1541) -format "$(SMC_NAME),id" d64 $(SMC_D64) -write $(SMC_PRG) $(SMC_NAME)

run-bench-pair-smc: $(SMC_D64)
	$(EMULATOR) -autostart $(SMC_D64)

# --- angle sweep: trick #2 vs #2b cost at each slope, same line each time ---

ANGLE_NAME := bench-angle-sweep
ANGLE_SRCS := tests/bench_angle_sweep.c src/hires.c src/circle_table.c src/drawline_asm.s src/drawline_asm_smc.s
ANGLE_PRG  := $(BUILD)/$(ANGLE_NAME).prg
ANGLE_D64  := $(BUILD)/$(ANGLE_NAME).d64

bench-angle-sweep: $(ANGLE_PRG)

$(ANGLE_PRG): $(ANGLE_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(ANGLE_PRG) $(ANGLE_SRCS)

bench-angle-sweep-d64: $(ANGLE_D64)

$(ANGLE_D64): $(ANGLE_PRG)
	$(C1541) -format "$(ANGLE_NAME),id" d64 $(ANGLE_D64) -write $(ANGLE_PRG) $(ANGLE_NAME)

run-bench-angle-sweep: $(ANGLE_D64)
	$(EMULATOR) -autostart $(ANGLE_D64)

# --- correctness regression test for drawline_asm.s and drawline_asm_smc.s ---

CORR_NAME := correctness-check
CORR_SRCS := tests/correctness_check.c src/hires.c src/drawline_asm.s src/drawline_asm_smc.s
CORR_PRG  := $(BUILD)/$(CORR_NAME).prg
CORR_D64  := $(BUILD)/$(CORR_NAME).d64

correctness-check: $(CORR_PRG)

$(CORR_PRG): $(CORR_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(CORR_PRG) $(CORR_SRCS)

correctness-check-d64: $(CORR_D64)

$(CORR_D64): $(CORR_PRG)
	$(C1541) -format "$(CORR_NAME),id" d64 $(CORR_D64) -write $(CORR_PRG) $(CORR_NAME)

run-correctness-check: $(CORR_D64)
	$(EMULATOR) -autostart $(CORR_D64)

# --- rotating square demo: trick #2 (hand-asm draw) + trick #3 (double buffer) ---

DEMO_NAME := rotating-square
DEMO_SRCS := src/main.c src/doublebuf.c src/circle_table.c src/drawline_asm.s
DEMO_PRG  := $(BUILD)/$(DEMO_NAME).prg
DEMO_D64  := $(BUILD)/$(DEMO_NAME).d64

rotating-square: $(DEMO_PRG)

$(DEMO_PRG): $(DEMO_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(DEMO_PRG) $(DEMO_SRCS)

rotating-square-d64: $(DEMO_D64)

$(DEMO_D64): $(DEMO_PRG)
	$(C1541) -format "$(DEMO_NAME),id" d64 $(DEMO_D64) -write $(DEMO_PRG) $(DEMO_NAME)

run-rotating-square: $(DEMO_D64)
	$(EMULATOR) -autostart $(DEMO_D64)

# --- rotating square, measured (fixed frame count, reports cycles) ---

RSB_NAME := bench-rotating-square
RSB_SRCS := tests/bench_rotating_square.c src/doublebuf.c src/circle_table.c src/drawline_asm.s
RSB_PRG  := $(BUILD)/$(RSB_NAME).prg
RSB_D64  := $(BUILD)/$(RSB_NAME).d64

bench-rotating-square: $(RSB_PRG)

$(RSB_PRG): $(RSB_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(RSB_PRG) $(RSB_SRCS)

bench-rotating-square-d64: $(RSB_D64)

$(RSB_D64): $(RSB_PRG)
	$(C1541) -format "$(RSB_NAME),id" d64 $(RSB_D64) -write $(RSB_PRG) $(RSB_NAME)

run-bench-rotating-square: $(RSB_D64)
	$(EMULATOR) -autostart $(RSB_D64)

# --- rotating square demo, upgraded to trick #2b (self-modified code) ---

DEMOSMC_NAME := rotating-square-smc
DEMOSMC_SRCS := src/main_smc.c src/doublebuf.c src/circle_table.c src/drawline_asm.s src/drawline_asm_smc.s
DEMOSMC_PRG  := $(BUILD)/$(DEMOSMC_NAME).prg
DEMOSMC_D64  := $(BUILD)/$(DEMOSMC_NAME).d64

rotating-square-smc: $(DEMOSMC_PRG)

$(DEMOSMC_PRG): $(DEMOSMC_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(DEMOSMC_PRG) $(DEMOSMC_SRCS)

rotating-square-smc-d64: $(DEMOSMC_D64)

$(DEMOSMC_D64): $(DEMOSMC_PRG)
	$(C1541) -format "$(DEMOSMC_NAME),id" d64 $(DEMOSMC_D64) -write $(DEMOSMC_PRG) $(DEMOSMC_NAME)

run-rotating-square-smc: $(DEMOSMC_D64)
	$(EMULATOR) -autostart $(DEMOSMC_D64)

# --- rotating square, measured, trick #2b ---

RSBSMC_NAME := bench-rotating-square-smc
RSBSMC_SRCS := tests/bench_rotating_square_smc.c src/doublebuf.c src/circle_table.c src/drawline_asm.s src/drawline_asm_smc.s
RSBSMC_PRG  := $(BUILD)/$(RSBSMC_NAME).prg
RSBSMC_D64  := $(BUILD)/$(RSBSMC_NAME).d64

bench-rotating-square-smc: $(RSBSMC_PRG)

$(RSBSMC_PRG): $(RSBSMC_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(RSBSMC_PRG) $(RSBSMC_SRCS)

bench-rotating-square-smc-d64: $(RSBSMC_D64)

$(RSBSMC_D64): $(RSBSMC_PRG)
	$(C1541) -format "$(RSBSMC_NAME),id" d64 $(RSBSMC_D64) -write $(RSBSMC_PRG) $(RSBSMC_NAME)

run-bench-rotating-square-smc: $(RSBSMC_D64)
	$(EMULATOR) -autostart $(RSBSMC_D64)

clean:
	rm -rf $(BUILD)
