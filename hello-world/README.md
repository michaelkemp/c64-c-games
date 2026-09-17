# hello-world

The first piece of the pipeline: prove that C source can be compiled for the
C64, packed onto a disk image, and booted in an emulator, before any actual
game logic gets written. Every later program in this repo (starting with
`asteroids/`) reuses this same build/run sequence.

## Toolchain

- [`cc65`](https://cc65.github.io/) — cross-compiler suite targeting 6502
  machines including the C64. Provides `cl65`, the compiler+linker driver
  used here, and `c1541`, the Commodore disk-image tool.
- [VICE](https://vice-emu.sourceforge.io/) — C64 emulator. `x64sc` is the
  accurate C64 machine emulator.

On Debian/Ubuntu: `sudo apt install cc65 vice`.

## Pipeline

```
src/main.c  --cl65-->  build/hello-world.prg  --c1541-->  build/hello-world.d64  --x64sc-->  running on screen
```

1. **Compile.** `cl65 -t c64 -o build/hello-world.prg src/main.c` builds a
   `.prg` — a C64 program file (2-byte load address + machine code) —
   using cc65's C runtime and startup code for the `c64` target.
2. **Pack onto a disk image.** `c1541` formats a blank `.d64` (a sector-for-
   sector image of a 1541 floppy) and writes the `.prg` onto it as a file
   named `hello-world`.
3. **Boot in VICE.** `x64sc -autostart build/hello-world.d64` starts the
   emulator, inserts the disk, and does the equivalent of typing
   `LOAD "hello-world",8` and `RUN` on a real C64.

## Usage

```sh
make        # compile + pack the disk image (build/hello-world.d64)
make run    # also launch VICE and autostart it
make clean  # remove build/
```

`make run` opens a VICE window. You should see:

```
hello, world!
c64-c-games pipeline is alive.
```

The program loops forever afterwards so the text stays on screen instead of
dropping back to the BASIC `READY.` prompt.
