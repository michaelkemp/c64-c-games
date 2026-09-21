# Build pipeline: C source -> cl65 (cc65) -> .prg -> c1541 -> .d64 -> VICE.
# See README.md for why this directory exists.
#
#   make prg     - compile src/*.c to build/we-three-kings.prg
#   make d64     - pack the .prg onto a fresh build/we-three-kings.d64
#   make run     - launch VICE (x64sc), autostarting the .d64
#   make pattern - regenerate src/pattern.c from the reference MIDI (needs `mido`)
#   make clean   - remove build/

CL65     := cl65
C1541    := c1541
EMULATOR := x64sc

TARGET   := c64
NAME     := we-three-kings
BUILD    := build

SRCS     := src/main.c src/sid.c src/note_table.c src/pattern.c
HDRS     := src/sid.h src/note_table.h src/pattern.h

PRG      := $(BUILD)/$(NAME).prg
D64      := $(BUILD)/$(NAME).d64

.PHONY: all prg d64 run pattern clean

all: d64

$(BUILD):
	mkdir -p $(BUILD)

prg: $(PRG)

$(PRG): $(SRCS) $(HDRS) | $(BUILD)
	$(CL65) -t $(TARGET) -o $(PRG) $(SRCS)

d64: $(D64)

$(D64): $(PRG)
	$(C1541) -format "$(NAME),id" d64 $(D64) -write $(PRG) $(NAME)

run: $(D64)
	$(EMULATOR) -autostart $(D64)

pattern:
	tools/gen_pattern.py > src/pattern.c

clean:
	rm -rf $(BUILD)
