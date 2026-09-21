# Build pipeline: C source -> cl65 (cc65) -> .prg; song.json -> .tune;
# both -> c1541 -> .d64 -> VICE. See README.md for why this directory
# is one player + swappable data files, not one binary per song.
#
#   make prg    - compile src/*.c to build/player.prg
#   make tune   - compile songs/$(SONG).json to build/$(SONG).tune (needs no extra deps)
#   make d64    - pack player.prg + TUNE onto a fresh build/tune-player.d64
#   make run    - launch VICE (x64sc), autostarting the .d64
#   make clean  - remove build/
#
# SONG picks which songs/*.json becomes the disk's TUNE file, e.g.:
#   make run SONG=chord_test

CL65     := cl65
C1541    := c1541
EMULATOR := x64sc

TARGET   := c64
NAME     := tune-player
BUILD    := build
SONG     ?= oh-come-all-ye-faithful

SRCS     := src/main.c src/sid.c src/note_table.c
HDRS     := src/sid.h src/note_table.h

PRG      := $(BUILD)/player.prg
TUNE     := $(BUILD)/$(SONG).tune
D64      := $(BUILD)/$(NAME).d64

.PHONY: all prg tune d64 run clean

all: d64

$(BUILD):
	mkdir -p $(BUILD)

prg: $(PRG)

$(PRG): $(SRCS) $(HDRS) | $(BUILD)
	$(CL65) -t $(TARGET) -o $(PRG) $(SRCS)

tune: $(TUNE)

$(TUNE): songs/$(SONG).json tools/gen_tune.py | $(BUILD)
	tools/gen_tune.py songs/$(SONG).json $(TUNE)

d64: $(D64)

$(D64): $(PRG) $(TUNE)
	$(C1541) -format "$(NAME),id" d64 $(D64) \
		-write $(PRG) player \
		-write $(TUNE) TUNE

run: $(D64)
	$(EMULATOR) -autostart $(D64)

clean:
	rm -rf $(BUILD)
