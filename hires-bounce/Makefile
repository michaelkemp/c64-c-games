# Build pipeline: C source -> cl65 (cc65) -> .prg -> c1541 -> .d64 -> VICE.
# See README.md in this folder for the VIC-II/hires details.
#
#   make prg              - compile the bouncing-line demo to build/hires-bounce.prg
#   make d64              - pack it onto a fresh build/hires-bounce.d64
#   make run              - launch VICE (x64sc), autostarting the demo
#   make bench            - compile the draw_line() benchmark to build/bench-line.prg
#   make bench-d64        - pack it onto a fresh build/bench-line.d64
#   make run-bench        - launch VICE, autostarting the benchmark
#   make bench-scene      - compile the ship+asteroids scene benchmark
#   make bench-scene-d64  - pack it onto a fresh build/bench-scene.d64
#   make run-bench-scene  - launch VICE, autostarting the scene benchmark
#   make clean            - remove build/

CL65     := cl65
C1541    := c1541
EMULATOR := x64sc

TARGET   := c64
BUILD    := build
CFG      := cfg/lowmem.cfg
CFLAGS   := -Oirs -Isrc

NAME        := hires-bounce
MAIN_SRCS   := src/main.c src/hires.c
PRG         := $(BUILD)/$(NAME).prg
D64         := $(BUILD)/$(NAME).d64

BENCH_NAME  := bench-line
BENCH_SRCS  := tests/bench_line.c src/hires.c
BENCH_PRG   := $(BUILD)/$(BENCH_NAME).prg
BENCH_D64   := $(BUILD)/$(BENCH_NAME).d64

SCENE_NAME  := bench-scene
SCENE_SRCS  := tests/bench_scene.c src/hires.c
SCENE_PRG   := $(BUILD)/$(SCENE_NAME).prg
SCENE_D64   := $(BUILD)/$(SCENE_NAME).d64

HDRS        := src/hires.h

.PHONY: all prg d64 run bench bench-d64 run-bench bench-scene bench-scene-d64 run-bench-scene clean

all: d64

$(BUILD):
	mkdir -p $(BUILD)

prg: $(PRG)

$(PRG): $(MAIN_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(PRG) $(MAIN_SRCS)

d64: $(D64)

$(D64): $(PRG)
	$(C1541) -format "$(NAME),id" d64 $(D64) -write $(PRG) $(NAME)

run: $(D64)
	$(EMULATOR) -autostart $(D64)

bench: $(BENCH_PRG)

$(BENCH_PRG): $(BENCH_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(BENCH_PRG) $(BENCH_SRCS)

bench-d64: $(BENCH_D64)

$(BENCH_D64): $(BENCH_PRG)
	$(C1541) -format "$(BENCH_NAME),id" d64 $(BENCH_D64) -write $(BENCH_PRG) $(BENCH_NAME)

run-bench: $(BENCH_D64)
	$(EMULATOR) -autostart $(BENCH_D64)

bench-scene: $(SCENE_PRG)

$(SCENE_PRG): $(SCENE_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(SCENE_PRG) $(SCENE_SRCS)

bench-scene-d64: $(SCENE_D64)

$(SCENE_D64): $(SCENE_PRG)
	$(C1541) -format "$(SCENE_NAME),id" d64 $(SCENE_D64) -write $(SCENE_PRG) $(SCENE_NAME)

run-bench-scene: $(SCENE_D64)
	$(EMULATOR) -autostart $(SCENE_D64)

clean:
	rm -rf $(BUILD)
