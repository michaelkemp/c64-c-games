# Build pipeline: C source -> cl65 (cc65) -> .prg -> c1541 -> .d64 -> VICE.
# See README.md for why this directory exists (asteroids-c64/'s edge
# wraparound rendering, redone with no per-segment clipping).
#
#   make ship-d64      - ship demo (IJKL rotate/thrust/hyperspace, space to fire)
#   make run-ship      - launch VICE, autostarting it
#   make clean         - remove build/

CL65     := cl65
C1541    := c1541
EMULATOR := x64sc

TARGET   := c64
BUILD    := build
CFG      := cfg/lowmem.cfg
CFLAGS   := -Oirs -Isrc -Itests

HDRS := src/doublebuf.h src/drawline_asm.h src/clearbitmap_asm.h src/ship_table.h \
        src/dir_table.h

.PHONY: all clean ship ship-d64 run-ship keytest keytest-d64 run-keytest

all: ship-d64

$(BUILD):
	mkdir -p $(BUILD)

# --- ship demo (IJKL rotate/thrust/hyperspace, space to fire) ---

SHIP_NAME := ship
SHIP_SRCS := src/main.c src/doublebuf.c src/ship_table.c src/dir_table.c \
             src/drawline_asm.s src/clearbitmap_asm.s
SHIP_PRG  := $(BUILD)/$(SHIP_NAME).prg
SHIP_D64  := $(BUILD)/$(SHIP_NAME).d64

ship: $(SHIP_PRG)

$(SHIP_PRG): $(SHIP_SRCS) $(HDRS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(SHIP_PRG) $(SHIP_SRCS)

ship-d64: $(SHIP_D64)

$(SHIP_D64): $(SHIP_PRG)
	$(C1541) -format "$(SHIP_NAME),id" d64 $(SHIP_D64) -write $(SHIP_PRG) $(SHIP_NAME)

run-ship: $(SHIP_D64)
	$(EMULATOR) -autostart $(SHIP_D64)

# --- standalone keyboard-matrix tester ---

KT_NAME := keytest
KT_SRCS := tests/keytest.c
KT_PRG  := $(BUILD)/$(KT_NAME).prg
KT_D64  := $(BUILD)/$(KT_NAME).d64

keytest: $(KT_PRG)

$(KT_PRG): $(KT_SRCS) $(CFG) | $(BUILD)
	$(CL65) -t $(TARGET) -C $(CFG) $(CFLAGS) -o $(KT_PRG) $(KT_SRCS)

keytest-d64: $(KT_D64)

$(KT_D64): $(KT_PRG)
	$(C1541) -format "$(KT_NAME),id" d64 $(KT_D64) -write $(KT_PRG) $(KT_NAME)

run-keytest: $(KT_D64)
	$(EMULATOR) -autostart $(KT_D64)

clean:
	rm -rf $(BUILD)
